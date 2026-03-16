#ifndef TEST_DISPLAY_PPM_H
#define TEST_DISPLAY_PPM_H

#include <Adafruit_GFX.h>

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/**
 * Save a GFXcanvas16 buffer as a PPM P6 file (24-bit RGB).
 * RGB565 -> RGB888 conversion per pixel.
 */
static bool savePPM(GFXcanvas16 &canvas, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;

    int w = canvas.width();
    int h = canvas.height();
    fprintf(f, "P6\n%d %d\n255\n", w, h);

    uint16_t *buf = canvas.getBuffer();
    for (int i = 0; i < w * h; i++) {
        uint16_t c = buf[i];
        uint8_t r = ((c >> 11) & 0x1F) * 255 / 31;
        uint8_t g = ((c >> 5) & 0x3F) * 255 / 63;
        uint8_t b = (c & 0x1F) * 255 / 31;
        fputc(r, f);
        fputc(g, f);
        fputc(b, f);
    }

    fclose(f);
    return true;
}

static bool bufferHasContent(GFXcanvas16 &canvas) {
    uint16_t *buf = canvas.getBuffer();
    int size = canvas.width() * canvas.height();
    for (int i = 0; i < size; i++) {
        if (buf[i] != 0) return true;
    }
    return false;
}

static void ensureDirRecursive(const char *dir) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s", dir);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

static void initTestCanvas(GFXcanvas16 &canvas) {
    canvas.fillScreen(0x0000);
}

#endif  // TEST_DISPLAY_PPM_H
