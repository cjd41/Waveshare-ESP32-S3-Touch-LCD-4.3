Import("env")
import os

STUB = b"/* Stub: ARM-only assembly, not applicable on Xtensa targets */\n"

def stub_if_arm_asm(path):
    if not os.path.exists(path):
        return
    with open(path, "rb") as f:
        content = f.read()
    if content != STUB:
        with open(path, "wb") as f2:
            f2.write(STUB)
        print(f"Pre-build: stubbed {os.path.basename(path)} (ARM-only, not usable on Xtensa)")

base = os.path.join(".pio", "libdeps", env["PIOENV"], "lvgl", "src", "draw", "sw", "blend")
stub_if_arm_asm(os.path.join(base, "helium", "lv_blend_helium.S"))
stub_if_arm_asm(os.path.join(base, "neon",   "lv_blend_neon.S"))
