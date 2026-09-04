import CoreBluetooth
import Foundation

enum UpstreamProfile: String {
    case mizzzeeV2 = "mizzzee-v2"
    case vibcrafter
    case hismith

    var localName: String {
        switch self {
        case .mizzzeeV2: "XHT"
        case .vibcrafter: "Janna"
        case .hismith: "HISMITH"
        }
    }

    var writeServiceUUID: CBUUID {
        switch self {
        case .mizzzeeV2: CBUUID(string: "EEA0")
        case .vibcrafter: CBUUID(string: "53300051-0060-4BD4-BBE5-A6920E4C5663")
        case .hismith: CBUUID(string: "FFE5")
        }
    }

    var readServiceUUID: CBUUID? {
        switch self {
        case .mizzzeeV2, .vibcrafter: nil
        case .hismith: CBUUID(string: "FF90")
        }
    }

    var advertisedServiceUUIDs: [CBUUID] {
        [writeServiceUUID] + [readServiceUUID].compactMap { $0 }
    }

    var readUUID: CBUUID? {
        switch self {
        case .mizzzeeV2, .vibcrafter: nil
        case .hismith: CBUUID(string: "FF96")
        }
    }

    var readValue: Data? {
        switch self {
        case .mizzzeeV2, .vibcrafter: nil
        case .hismith:
            // The official identifier formats these bytes as `1001`, selecting
            // the Hismith Sex Machine definition from the upstream config.
            Data([0x10, 0x01])
        }
    }

    var writeUUID: CBUUID {
        switch self {
        case .mizzzeeV2: CBUUID(string: "EE01")
        case .vibcrafter: CBUUID(string: "53300052-0060-4BD4-BBE5-A6920E4C5663")
        case .hismith: CBUUID(string: "FFE9")
        }
    }

    var notifyUUID: CBUUID? {
        switch self {
        case .mizzzeeV2, .hismith: nil
        case .vibcrafter: CBUUID(string: "53300053-0060-4BD4-BBE5-A6920E4C5663")
        }
    }

    var writeProperties: CBCharacteristicProperties {
        switch self {
        case .mizzzeeV2: [.write, .writeWithoutResponse]
        case .hismith: [.writeWithoutResponse]
        case .vibcrafter:
            // Buttplug requests no-response writes for this protocol. Exposing
            // only `.write` exercises the transport's compatible fallback.
            [.write]
        }
    }

    var handshakeResponse: Data? {
        switch self {
        case .mizzzeeV2, .hismith: nil
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
    private let readCharacteristic: CBMutableCharacteristic?
    private let services: [CBMutableService]
    private var manager: CBPeripheralManager!
    private var addedServiceCount = 0
    private var handshakeSent = false
    private var pendingNotification: Data?

    init(profile: UpstreamProfile) {
        self.profile = profile
        let writeCharacteristic = CBMutableCharacteristic(
            type: profile.writeUUID,
            properties: profile.writeProperties,
            value: nil,
            permissions: [.writeable]
        )
        self.writeCharacteristic = writeCharacteristic
        let notifyCharacteristic = profile.notifyUUID.map {
            CBMutableCharacteristic(
                type: $0,
                properties: [.notify],
                value: nil,
                permissions: []
            )
        }
        self.notifyCharacteristic = notifyCharacteristic
        let readCharacteristic = profile.readUUID.map {
            CBMutableCharacteristic(
                type: $0,
                properties: [.read],
                value: nil,
                permissions: [.readable]
            )
        }
        self.readCharacteristic = readCharacteristic

        let writeService = CBMutableService(type: profile.writeServiceUUID, primary: true)
        writeService.characteristics = [writeCharacteristic] + [notifyCharacteristic].compactMap { $0 }
        var services = [writeService]
        if let readServiceUUID = profile.readServiceUUID, let readCharacteristic {
            let readService = CBMutableService(type: readServiceUUID, primary: true)
            readService.characteristics = [readCharacteristic]
            services.append(readService)
        }
        self.services = services
        super.init()
        manager = CBPeripheralManager(delegate: self, queue: nil)
    }

    func peripheralManagerDidUpdateState(_ peripheral: CBPeripheralManager) {
        guard peripheral.state == .poweredOn else {
            print("Bluetooth state: \(peripheral.state.rawValue)")
            return
        }

        for service in services {
            peripheral.add(service)
        }
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

        addedServiceCount += 1
        guard addedServiceCount == services.count else {
            return
        }

        peripheral.startAdvertising([
            CBAdvertisementDataLocalNameKey: profile.localName,
            CBAdvertisementDataServiceUUIDsKey: profile.advertisedServiceUUIDs,
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

    func peripheralManager(_ peripheral: CBPeripheralManager, didReceiveRead request: CBATTRequest) {
        guard
            let readCharacteristic,
            request.characteristic.uuid == readCharacteristic.uuid,
            let value = profile.readValue
        else {
            peripheral.respond(to: request, withResult: .attributeNotFound)
            return
        }
        guard request.offset <= value.count else {
            peripheral.respond(to: request, withResult: .invalidOffset)
            return
        }

        request.value = Data(value.dropFirst(request.offset))
        peripheral.respond(to: request, withResult: .success)
        print("Returned \(request.characteristic.uuid) read: \(Array(request.value ?? Data()))")
        fflush(stdout)
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
    print("Unknown profile \(requestedProfile). Choose mizzzee-v2, vibcrafter, or hismith.")
    exit(2)
}

let peripheral = UpstreamProfilePeripheral(profile: profile)
withExtendedLifetime(peripheral) {
    RunLoop.main.run()
}
