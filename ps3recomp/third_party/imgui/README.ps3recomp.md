# Dear ImGui source pin

This directory vendors the Dear ImGui core required by the runtime overlay.

- Upstream release: `v1.92.5` (published 2025-11-20)
- Immutable upstream revision: `6d910d5487d11ca567b61c7824b0c78c569d62f0`
- Upstream repository: <https://github.com/ocornut/imgui>
- License: MIT; the upstream license text is preserved in `LICENSE.txt`.

The `ps3recomp_imgui` CMake target compiles only the four upstream core
translation units: `imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`, and
`imgui_widgets.cpp`. Their public/internal headers, default `imconfig.h`, and
required `imstb_*` headers are included alongside them.

No upstream example or renderer backend source is vendored or compiled. Native
D3D12 and Metal renderer sources remain project-owned and are selected only on
Windows and Apple platforms, respectively, once later overlay tasks add them.
