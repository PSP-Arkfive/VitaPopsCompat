# VitaPopsCompat
Compat Layer that allows ARK to run on stock Vita ps1emu.

Vita POPS (PSX exploits on PS Vita) compatibility layer for ARK.
Rescued from early TN-X source code by TheFl0w.

PSX games on PS Vita run on a limited PSP environment.
- The emulator itself is POPS, same exact as PSP 6.60, but audio is handled by Vita OS.
- No Wifi, no PSP sound, file size limited to size of EBOOT (on 3.30+).
- Can make use of PSP GPU, but Vita ignores PSP framebuffer and directly reads from PSX VRAM.
- Very minimal PSP homebrews can run due to limited performance.

ARK's SystemControl and Popcorn can run under this environment, along this module for especific patches.