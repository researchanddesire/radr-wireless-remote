import CoreBluetooth
import Foundation

final class UpstreamProfilePeripheral: NSObject, CBPeripheralManagerDelegate {
    private let serviceUUID = CBUUID(string: "EEA0")
    private let characteristicUUID = CBUUID(string: "EE01")
    private lazy var characteristic = CBMutableCharacteristic(
        type: characteristicUUID,
        properties: [.write, .writeWithoutResponse],
        value: nil,
        permissions: [.writeable]
    )
    private var manager: CBPeripheralManager!

    override init() {
        super.init()
        manager = CBPeripheralManager(delegate: self, queue: nil)
    }

    func peripheralManagerDidUpdateState(_ peripheral: CBPeripheralManager) {
        guard peripheral.state == .poweredOn else {
            print("Bluetooth state: \(peripheral.state.rawValue)")
            return
        }

        let service = CBMutableService(type: serviceUUID, primary: true)
        service.characteristics = [characteristic]
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
            CBAdvertisementDataLocalNameKey: "XHT",
            CBAdvertisementDataServiceUUIDsKey: [serviceUUID],
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
        print("Advertising upstream mizzzee-v2 test profile as XHT")
        fflush(stdout)
    }

    func peripheralManager(
        _ peripheral: CBPeripheralManager,
        didReceiveWrite requests: [CBATTRequest]
    ) {
        for request in requests {
            let bytes = Array(request.value ?? Data())
            print("Received EE01 write: \(bytes)")
            fflush(stdout)
            if request.characteristic.properties.contains(.write) {
                peripheral.respond(to: request, withResult: .success)
            }
        }
    }
}

let peripheral = UpstreamProfilePeripheral()
withExtendedLifetime(peripheral) {
    RunLoop.main.run()
}
