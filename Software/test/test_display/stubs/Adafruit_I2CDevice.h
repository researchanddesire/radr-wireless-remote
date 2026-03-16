#ifndef _ADAFRUIT_I2CDEVICE_STUB_H
#define _ADAFRUIT_I2CDEVICE_STUB_H

#include <stdint.h>
#include <stddef.h>

class Adafruit_I2CDevice {
  public:
    Adafruit_I2CDevice(uint8_t addr = 0, void *theWire = nullptr) {}
    bool begin(bool addr_detect = true) { return true; }
    bool detected(void) { return false; }
    bool write(const uint8_t *buffer, size_t len, bool stop = true,
               const uint8_t *prefix_buffer = nullptr, size_t prefix_len = 0) {
        return true;
    }
    bool read(uint8_t *buffer, size_t len, bool stop = true) { return true; }
};

#endif
