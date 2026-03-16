#ifndef TEST_DISPLAY_HELPERS_H
#define TEST_DISPLAY_HELPERS_H

#include <ui.h>
#include <unity.h>

#include "ppm.h"

extern GFXcanvas16 *canvasPtr;
extern const char *OUTPUT_DIR;
extern const char *PNG_DIR;

#define canvas (*canvasPtr)

static inline bool savePPMGrouped(GFXcanvas16 &c, const char *group,
                                  const char *name) {
    char dir[256], path[256];
    snprintf(dir, sizeof(dir), "%s/%s", OUTPUT_DIR, group);
    ensureDirRecursive(dir);
    snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    bool ok = savePPM(c, path);
    if (ok) printf("  -> Saved %s\n", path);
    return ok;
}

#endif  // TEST_DISPLAY_HELPERS_H
