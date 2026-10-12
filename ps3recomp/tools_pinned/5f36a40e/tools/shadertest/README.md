# NV40 → HLSL translator validation

Two harnesses that validate `libs/video/rsx_fp_decompiler.c` and
`libs/video/rsx_vp_decompiler.c` objectively.

## harness.c — compile validation (fxc)
Synthesizes NV40 ucode per opcode + modifier combinations + realistic programs,
runs the decompiler, and compiles the generated HLSL with `fxc.exe`. Also golden
checks the generated math (LRP order, DST layout, RCP broadcast, TXP divide...).

    gcc -O0 -I ../../include harness.c ../../libs/video/rsx_fp_decompiler.c \
        ../../libs/video/rsx_vp_decompiler.c -o harness.exe
    FXC=$(cygpath -w "/c/Program Files (x86)/Windows Kits/10/bin/<ver>/x64/fxc.exe")
    ./harness.exe "$FXC"     # expect: RESULT: N pass, 0 fail

Git Bash note: fxc is native — pass its path via `cygpath -w` (avoids `/T`->`T:/`
MSYS path mangling).

## execharness.c — numerical validation (D3D11 WARP)
Runs each translated fragment shader on a real GPU path with known inputs and
compares the pixel output to the value computed independently from the NV40
semantics. Proves the arithmetic, not just that it compiles.

    gcc -O0 -I ../../include execharness.c ../../libs/video/rsx_fp_decompiler.c \
        -o execharness.exe -ld3d11 -ld3dcompiler -ldxgi
    ./execharness.exe        # expect: NUMERIC RESULT: N pass, 0 fail
