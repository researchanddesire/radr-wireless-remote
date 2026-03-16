"""Pre-build script for the [env:test] native test environment.

Replaces ArduinoFake's PrintFake.cpp with an empty file so our own real
Print.cpp (in test/test_display/stubs/) is linked instead. ArduinoFake's
version routes every Print method through the FakeIt mock system, which
crashes when called from Adafruit_GFX subclasses like GFXcanvas16 that
aren't registered as mock instances.
"""
import os
Import("env")

def patch_printfake(source, target, env):
    libdeps = env.subst("$PROJECT_LIBDEPS_DIR/$PIOENV")
    fakecpp = os.path.join(libdeps, "ArduinoFake", "src", "PrintFake.cpp")
    if os.path.isfile(fakecpp):
        with open(fakecpp, "w") as f:
            f.write("// neutered by test_pre_build.py – real Print.cpp in stubs/\n")
        print(f"  [patch] Replaced {fakecpp}")

env.AddPreAction("buildprog", patch_printfake)
env.AddPreAction("$BUILD_DIR/src", patch_printfake)
