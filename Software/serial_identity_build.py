"""Generate identity from the actual selected board/profile, never the host fixture."""
from pathlib import Path
import re

def profile_identity(product, flash_size, board):
    size = re.fullmatch(r"(4|8|16|32)MB", str(flash_size).upper())
    if not size or product not in ("LKBX", "DTT", "OSSM", "RADR"):
        raise ValueError("Unsupported serial identity product/flash profile")
    flash_mb = int(size[1])
    psram_mb = 0
    if product in ("LKBX", "RADR"):
        ram = re.search(r"n16r(2|8)v?\b", str(board).lower())
        if not ram or flash_mb != 16:
            raise ValueError("TFT serial identity requires an explicit N16R2/R8 board")
        psram_mb = int(ram[1])
    model = f"{product}-N{flash_mb}" + (f"R{psram_mb}" if psram_mb else "")
    return model, flash_mb * 1024 * 1024, psram_mb * 1024 * 1024

def install(env, product):
    if "arduino" not in env.get("PIOFRAMEWORK", []):
        return
    board = env.BoardConfig()
    model, flash, psram = profile_identity(product, board.get("upload.flash_size"), env.subst("$BOARD"))
    directory = Path(env.subst("$BUILD_DIR"))
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / "serialIdentityBuild.h"
    text = (f'#pragma once\n#define RAD_ID_PRODUCT "{product}"\n#define RAD_ID_MODEL "{model}"\n'
            f'#define RAD_ID_FLASH_BYTES {flash}u\n#define RAD_ID_PSRAM_BYTES {psram}u\n')
    if not path.exists() or path.read_text() != text:
        path.write_text(text, encoding="utf-8")
    env.Append(CPPPATH=[str(directory)])
