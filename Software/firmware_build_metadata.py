Import("env")

import os

env.Append(
    CPPDEFINES=[
        (
            "FIRMWARE_BUILD_SHA",
            env.StringifyMacro(
                os.getenv("RELEASE_BUILD_SHA", os.getenv("GITHUB_SHA", "unknown"))
            ),
        )
    ]
)

# Staging hardware observation uses the ordinary application and normal updates.
profile = env.subst("$PIOENV")
if profile == "staging" or profile.startswith("staging-"):
    variant = profile.split("-", 1)[1] if "-" in profile else "r8"
    env.Append(CPPDEFINES=[
        "RAD_HIL_TLS_PSRAM",
        ("RAD_HIL_PRODUCT", env.StringifyMacro("radr")),
        ("RAD_HIL_VARIANT", env.StringifyMacro(variant)),
    ])

# Development screen rows and SDK logs share the serial writer.
if env.subst("$PIOENV").startswith("development") and "espidf" in env.get("PIOFRAMEWORK", []):
    env.Append(LINKFLAGS=["-Wl,--wrap=log_printf"])

# Model names describe this image's target, separate from measured physical RAM.
import sys
sys.path.insert(0, env.subst("$PROJECT_DIR"))
from serial_identity_build import install as install_serial_identity
install_serial_identity(env, "RADR")
