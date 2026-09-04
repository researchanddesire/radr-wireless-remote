import CoreBluetooth
import Foundation

enum UpstreamProfile: String {
    case mizzzeeV2 = "mizzzee-v2"
    case vibcrafter

    var localName: String {
        switch self {
        case .mizzzeeV2: "XHT"
        case .vibcrafter: "Janna"
        }
    }

    var serviceUUID: CBUUID {
        switch self {
        case .mizzzeeV2: CBUUID(string: "EEA0")
        case .vibcrafter: CBUUID(string: "53300051-0060-4BD4-BBE5-A6920E4C5663")
        }
    }

    var writeUUID: CBUUID {
        switch self {
        case .mizzzeeV2: CBUUID(string: "EE01")
        case .vibcrafter: CBUUID(string: "53300052-0060-4BD4-BBE5-A6920E4C5663")
        }
    }

    var notifyUUID: CBUUID? {
        switch self {
        case .mizzzeeV2: nil
        case .vibcrafter: CBUUID(string: "53300053-0060-4BD4-BBE5-A6920E4C5663")
        }
    }

    var writeProperties: CBCharacteristicProperties {
        switch self {
        case .mizzzeeV2: [.write, .writeWithoutResponse]
        case .vibcrafter:
            // Buttplug requests no-response writes for this protocol. Exposing
            // only `.write` exercises the transport's compatible fallback.
            [.write]
        }
    }

    var handshakeResponse: Data? {
        switch self {
        case .mizzzeeV2: nil
        case .vibcrafter:
            // AES-128-ECB/PKCS#7 encoding of `OK;` with the key in the
            // official VibCrafter protocol implementation.
            Data([
                0x12, 0x0a, 0x6d, 0xef, 0xa7, 0x05, 0xe3, 0x6f,
                0xa0, 0x46, 0xe6, 0xcd, 0xf6, 0x09, 0xf6, 0x7a,
            ])
        }
    }
}

final class UpstreamProfilePeripheral: NSObject, CBPeripheralManagerDelegate {
    private let profile: UpstreamProfile
    private let writeCharacteristic: CBMutableCharacteristic
    private let notifyCharacteristic: CBMutableCharacteristic?
    private var manager: CBPeripheralManager!
    private var handshakeSent = false
    private var pendingNotification: Data?

    init(profile: UpstreamProfile) {
        self.profile = profile
        writeCharacteristic = CBMutableCharacteristic(
            type: profile.writeUUID,
            properties: profile.writeProperties,
            value: nil,
            permissions: [.writeable]
        )
        notifyCharacteristic = profile.notifyUUID.map {
            CBMutableCharacteristic(
                type: $0,
                properties: [.notify],
                value: nil,
                permissions: []
            )
        }
        super.init()
        manager = CBPeripheralManager(delegate: self, queue: nil)
    }

    func peripheralManagerDidUpdateState(_ peripheral: CBPeripheralManager) {
        guard peripheral.state == .poweredOn else {
            print("Bluetooth state: \(peripheral.state.rawValue)")
            return
        }

        let service = CBMutableService(type: profile.serviceUUID, primary: true)
        service.characteristics = [writeCharacteristic] + [notifyCharacteristic].compactMap { $0 }
        peripheral.add(service)
    }

    func peripheralManager(
        _ peripheral: CBPeripheralManager,
        didAdd service: CBService,
        error: Error?
    ) {
        if let error {
            print("Could not add mock service: \(error)")
            exit(1)
        }

        peripheral.startAdvertising([
            CBAdvertisementDataLocalNameKey: profile.localName,
            CBAdvertisementDataServiceUUIDsKey: [profile.serviceUUID],
        ])
    }

    func peripheralManagerDidStartAdvertising(
        _ peripheral: CBPeripheralManager,
        error: Error?
    ) {
        if let error {
            print("Could not advertise mock profile: \(error)")
            exit(1)
        }
        print("Advertising upstream \(profile.rawValue) test profile as \(profile.localName)")
        fflush(stdout)
    }

    func peripheralManager(
        _ peripheral: CBPeripheralManager,
        central: CBCentral,
        didSubscribeTo characteristic: CBCharacteristic
    ) {
        print("Central subscribed to \(characteristic.uuid)")
        fflush(stdout)
        flushPendingNotification(peripheral)
    }

    func peripheralManager(
        _ peripheral: CBPeripheralManager,
        didReceiveWrite requests: [CBATTRequest]
    ) {
        for request in requests {
            let bytes = Array(request.value ?? Data())
            print("Received \(request.characteristic.uuid) write: \(bytes)")
            fflush(stdout)
            if request.characteristic.properties.contains(.write) {
                peripheral.respond(to: request, withResult: .success)
            }

            if !handshakeSent, let response = profile.handshakeResponse {
                handshakeSent = true
                pendingNotification = response
                flushPendingNotification(peripheral)
            }
        }
    }

    func peripheralManagerIsReady(toUpdateSubscribers peripheral: CBPeripheralManager) {
        flushPendingNotification(peripheral)
    }

    private func flushPendingNotification(_ peripheral: CBPeripheralManager) {
        guard
            let pendingNotification,
            let notifyCharacteristic
        else {
            return
        }
        if peripheral.updateValue(
            pendingNotification,
            for: notifyCharacteristic,
            onSubscribedCentrals: nil
        ) {
            print("Sent \(notifyCharacteristic.uuid) notification: \(Array(pendingNotification))")
            fflush(stdout)
            self.pendingNotification = nil
        }
    }
}

let requestedProfile = CommandLine.arguments.dropFirst().first ?? UpstreamProfile.mizzzeeV2.rawValue
guard let profile = UpstreamProfile(rawValue: requestedProfile) else {
    print("Unknown profile \(requestedProfile). Choose mizzzee-v2 or vibcrafter.")
    exit(2)
}

let peripheral = UpstreamProfilePeripheral(profile: profile)
withExtendedLifetime(peripheral) {
    RunLoop.main.run()
}
