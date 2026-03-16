#ifndef _ADAFRUIT_SPIDEVICE_STUB_H
#define _ADAFRUIT_SPIDEVICE_STUB_H

#include <stdint.h>
#include <stddef.h>

#ifndef MSBFIRST
#define MSBFIRST 1
#endif
#ifndef LSBFIRST
#define LSBFIRST 0
#endif
#ifndef SPI_MODE0
#define SPI_MODE0 0
#endif
#ifndef SPI_BITORDER_MSBFIRST
#define SPI_BITORDER_MSBFIRST MSBFIRST
#endif
#ifndef SPI_BITORDER_LSBFIRST
#define SPI_BITORDER_LSBFIRST LSBFIRST
#endif

typedef uint8_t BusIOBitOrder;

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

class Adafruit_SPIDevice {
  public:
    Adafruit_SPIDevice(int8_t cspin, uint32_t freq = 1000000,
                       uint8_t dataOrder = MSBFIRST,
                       uint8_t dataMode = SPI_MODE0,
                       void *theSPI = nullptr) {}
    Adafruit_SPIDevice(int8_t cspin, int8_t sckpin, int8_t misopin,
                       int8_t mosipin, uint32_t freq = 1000000,
                       uint8_t dataOrder = MSBFIRST,
                       uint8_t dataMode = SPI_MODE0) {}
    bool begin(void) { return true; }
    bool write(const uint8_t *buffer, size_t len,
               const uint8_t *prefix_buffer = nullptr,
               size_t prefix_len = 0) {
        return true;
    }
    bool read(uint8_t *buffer, size_t len, uint8_t sendvalue = 0xFF) {
        return true;
    }
    bool write_then_read(const uint8_t *write_buffer, size_t write_len,
                         uint8_t *read_buffer, size_t read_len,
                         uint8_t sendvalue = 0xFF) {
        return true;
    }
};

#endif
