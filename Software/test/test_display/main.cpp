#include <unity.h>

#include "test_helpers.h"

GFXcanvas16 *canvasPtr = nullptr;
const char *OUTPUT_DIR = "test/test_display/_output/ppm";
const char *PNG_DIR = "test/test_display/_output/png";

void setUp(void) {
    if (canvasPtr) delete canvasPtr;
    canvasPtr = new GFXcanvas16(320, 240);
    initTestCanvas(*canvasPtr);
}

void tearDown(void) {}

extern void register_textpage_tests();
extern void register_menu_tests();
extern void register_component_tests();
extern void register_qr_tests();
extern void register_headerbar_tests();
extern void register_ossm_button_tests();
extern void register_ossm_menu_tests();
extern void register_ossm_control_tests();
extern void register_generic_device_tests();

static void convertPpmToPng() {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "which magick >/dev/null 2>&1 && "
             "find %s -name '*.ppm' | while read f; do "
             "rel=\"${f#%s/}\"; "
             "dir=\"%s/$(dirname \"$rel\")\"; "
             "name=$(basename \"$f\" .ppm); "
             "mkdir -p \"$dir\"; "
             "magick \"$f\" -scale 200%%%% \"$dir/${name}.png\"; "
             "done",
             OUTPUT_DIR, OUTPUT_DIR, PNG_DIR);
    int rc = system(cmd);
    if (rc == 0) {
        printf("\n  -> PNGs saved to %s/\n", PNG_DIR);
    }
}

static void cleanOutputDir() {
    system("rm -rf test/test_display/_output");
}

int runUnityTests() {
    cleanOutputDir();
    ensureDirRecursive(OUTPUT_DIR);

    UNITY_BEGIN();
    register_headerbar_tests();
    register_textpage_tests();
    register_menu_tests();
    register_component_tests();
    register_qr_tests();
    register_ossm_button_tests();
    register_ossm_menu_tests();
    register_ossm_control_tests();
    register_generic_device_tests();
    int result = UNITY_END();

    convertPpmToPng();

    if (canvasPtr) delete canvasPtr;
    canvasPtr = nullptr;

    return result;
}

int main(void) { return runUnityTests(); }
