cmake_minimum_required(VERSION 3.20)
project(ps3recomp_runtime
    VERSION 0.3.0
    LANGUAGES C CXX
    DESCRIPTION "ps3recomp - Static recompilation runtime for PS3 games"
)

# The Metal RSX backend is Objective-C: Metal ships no C API.
if(APPLE)
    enable_language(OBJC)
endif()

# ---------------------------------------------------------------------------
# Standards
# ---------------------------------------------------------------------------
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(PS3RECOMP_BUILD_TESTS "Build unit tests" OFF)

# ---------------------------------------------------------------------------
# CTest (task F0-5)
#
# Tudo aqui é condicional a PS3RECOMP_BUILD_TESTS: com a opção OFF (o
# default) nem enable_testing() nem a procura de Python3 correm — o build
# por defeito fica exactamente como estava antes desta task.
#
# O mínimo de Python do projecto é 3.11 (pyproject.toml, task F0-2); o
# python3 de sistema em muitas máquinas (ex.: macOS com só as Command Line
# Tools) é mais antigo e nem consegue fazer parse de alguns scripts de
# tools/ (ver docs/BASELINE_FASE0.md). find_package(Python3) é a forma
# nativa do CMake de descobrir um interpretador que cumpra o mínimo — sem
# depender de um .venv existir nem de hardcodar "python3".
# ---------------------------------------------------------------------------
if(PS3RECOMP_BUILD_TESTS)
    enable_testing()
    find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
    message(STATUS "ps3recomp: testes Python vão usar ${Python3_EXECUTABLE} (${Python3_VERSION})")
endif()

# ---------------------------------------------------------------------------
# SDL2 (non-Windows input/audio/window backend)
#
# On Windows the pad/audio modules use XInput and WASAPI; everywhere else they
# default to the SDL2 backends, so SDL2 is a hard dependency there. Prefer the
# upstream CMake config package and fall back to pkg-config, which is what
# Homebrew's sdl2/sdl2-compat ships on macOS.
# ---------------------------------------------------------------------------
if(NOT WIN32)
    find_package(SDL2 QUIET)

    if(NOT SDL2_FOUND)
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(SDL2_PC QUIET sdl2)
        endif()
    endif()

    if(SDL2_FOUND OR SDL2_PC_FOUND)
        if(SDL2_PC_FOUND AND NOT SDL2_FOUND)
            set(PS3RECOMP_SDL2_INCLUDE_DIRS ${SDL2_PC_INCLUDE_DIRS})
            set(PS3RECOMP_SDL2_LIBRARIES    ${SDL2_PC_LINK_LIBRARIES})
            set(PS3RECOMP_SDL2_VERSION      ${SDL2_PC_VERSION})
        else()
            set(PS3RECOMP_SDL2_INCLUDE_DIRS ${SDL2_INCLUDE_DIRS})
            set(PS3RECOMP_SDL2_LIBRARIES    ${SDL2_LIBRARIES})
            set(PS3RECOMP_SDL2_VERSION      ${SDL2_VERSION})
        endif()
        # The sources include <SDL2/SDL.h>, but many sdl2.pc files only export
        # <prefix>/include/SDL2. Add the parent of any such directory so both
        # the <SDL2/SDL.h> and <SDL.h> conventions resolve.
        foreach(_dir IN LISTS PS3RECOMP_SDL2_INCLUDE_DIRS)
            if(_dir MATCHES "/SDL2$")
                get_filename_component(_parent "${_dir}" DIRECTORY)
                list(APPEND PS3RECOMP_SDL2_INCLUDE_DIRS "${_parent}")
            endif()
        endforeach()
        list(REMOVE_DUPLICATES PS3RECOMP_SDL2_INCLUDE_DIRS)

        message(STATUS "ps3recomp: SDL2 ${PS3RECOMP_SDL2_VERSION} found")
    else()
        message(FATAL_ERROR
            "SDL2 not found. The pad/audio HLE modules require it on non-Windows "
            "hosts.\n  macOS:  brew install sdl2 pkg-config\n"
            "  Debian: apt install libsdl2-dev pkg-config")
    endif()
endif()

# ---------------------------------------------------------------------------
# Collect source files
# ---------------------------------------------------------------------------

# HLE library sources
file(GLOB_RECURSE HLE_SOURCES
    "libs/system/*.c"
    "libs/filesystem/*.c"
    "libs/input/*.c"
    "libs/audio/*.c"
    "libs/video/*.c"
    "libs/network/*.c"
    "libs/spurs/*.c"
    "libs/sync/*.c"
    "libs/codec/*.c"
    "libs/font/*.c"
    "libs/misc/*.c"
    "libs/hardware/*.c"
)
# sceNpCommerce.c and sceNpCommerce2.c are two independent implementations of
# the same sceNpCommerce2* API -- different context types (SceNpCommerce2Ctx vs
# SceNpCommerce2Context), six symbols defined by both. ar does not object, so
# the library builds, but any link that pulls in both objects fails with
# duplicate symbols; the generated HLE NID table does exactly that.
#
# Drop the misnamed sceNpCommerce.c: everything it defines is a sceNpCommerce2*
# symbol, so the file name promises an API it does not implement. This is not
# lossless -- these seven have no counterpart in sceNpCommerce2.c and stop being
# available:
#
#   sceNpCommerce2DoCheckoutStartAsync         sceNpCommerce2GetCategoryInfo
#   sceNpCommerce2DoEntitlementListStartAsync  sceNpCommerce2GetProductInfo
#   sceNpCommerce2DoEntitlementListFinishAsync sceNpCommerce2GetResult
#   sceNpCommerce2InitStoreRequest
#
# A title importing one of those now fails to link, loudly, instead of binding
# to whichever definition the linker happened to see first. Merging the two into
# one module is the real fix and needs a decision on the context type.
list(FILTER HLE_SOURCES EXCLUDE REGEX "/sceNpCommerce\\.c$")

# Exclude test harnesses (e.g. libs/video/tests/*): they define their own main().
list(FILTER HLE_SOURCES EXCLUDE REGEX "/tests/")
# Overlay renderers are selected below by their native host platform. Keep them
# out of the recursive video glob so a future backend cannot leak into the
# other platform's build.
list(FILTER HLE_SOURCES EXCLUDE REGEX "/rsx_overlay_(d3d12|metal)\\.(c|m)$")
# GLOB is cached; keep the overlay module listed even if configure is stale.
list(APPEND HLE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/libs/filesystem/ps3_mods.c")
# The overlay core is C++ (Dear ImGui); the *.c globs above do not see it.
list(APPEND HLE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/libs/video/rsx_overlay.cpp")
list(APPEND HLE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/libs/video/rsx_overlay_shell.cpp")
list(APPEND HLE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/libs/video/rsx_overlay_shell_draw.cpp")
list(APPEND HLE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/libs/video/rsx_overlay_assets.c")
list(APPEND HLE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/libs/video/rsx_overlay_ui_sound.c")
list(REMOVE_DUPLICATES HLE_SOURCES)

# Runtime core (syscalls, PPU, SPU, memory)
file(GLOB_RECURSE RUNTIME_SOURCES
    "runtime/*.c"
    "runtime/*.cpp"
)
# Exclude:
#  - test harnesses (runtime/*/tests/*): they have their own main() and #include
#    per-game generated headers (spu_recomp.h) that a plain library build lacks.
#  - the PPU boot scaffold (runtime/ppu/, runtime/host/): these are compiled
#    per-game against the lifter-generated ppu_recomp.h (and the game's RSX/D3D12
#    backend), not as part of the game-agnostic runtime library. See
#    docs/PPU_RECOMP.md.
list(FILTER RUNTIME_SOURCES EXCLUDE REGEX "/tests/|/runtime/ppu/|/runtime/host/")

# ---------------------------------------------------------------------------
# Static library
# ---------------------------------------------------------------------------
set(PS3RECOMP_OVERLAY_RENDERER_SOURCES)
if(APPLE)
    set(PS3RECOMP_OVERLAY_METAL_SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video/rsx_overlay_metal.m")
    if(EXISTS "${PS3RECOMP_OVERLAY_METAL_SOURCE}")
        list(APPEND PS3RECOMP_OVERLAY_RENDERER_SOURCES
            "${PS3RECOMP_OVERLAY_METAL_SOURCE}")
    endif()
endif()

if(WIN32)
    set(PS3RECOMP_OVERLAY_D3D12_SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video/rsx_overlay_d3d12.c")
    if(EXISTS "${PS3RECOMP_OVERLAY_D3D12_SOURCE}")
        list(APPEND PS3RECOMP_OVERLAY_RENDERER_SOURCES
            "${PS3RECOMP_OVERLAY_D3D12_SOURCE}")
    endif()
endif()

list(APPEND HLE_SOURCES ${PS3RECOMP_OVERLAY_RENDERER_SOURCES})

if(APPLE)
    list(APPEND HLE_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video/rsx_metal_backend.m"
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video/movie_vt_metal.m"
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/codec/cellVdec_vt.m"
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/input/cellpad_gamecontroller.m"
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video/rsx_overlay_assets_apple.m"
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video/rsx_overlay_feedback_apple.m")
endif()

# Vulkan is optional: native on Linux, MoltenVK on macOS. Without it the file
# is simply left out of the build rather than compiling to an empty stub.
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    message(STATUS "ps3recomp: Vulkan ${Vulkan_VERSION} found")
else()
    list(REMOVE_ITEM HLE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/libs/video/rsx_vulkan_backend.c")
    message(STATUS "ps3recomp: Vulkan not found, RSX Vulkan backend disabled")
endif()

# shaderc: GLSL -> SPIR-V for the Vulkan RSX backend (system package:
# brew install shaderc / apt install libshaderc-dev). Optional: without it the
# GLSL tests are skipped and the Vulkan backend stays clear-only.
find_package(PkgConfig QUIET)
set(PS3RECOMP_HAVE_SHADERC OFF)
if(PkgConfig_FOUND)
    pkg_check_modules(SHADERC QUIET IMPORTED_TARGET shaderc)
    if(SHADERC_FOUND)
        set(PS3RECOMP_HAVE_SHADERC ON)
        message(STATUS "ps3recomp: shaderc ${SHADERC_VERSION} found")
    endif()
endif()
if(NOT PS3RECOMP_HAVE_SHADERC)
    message(STATUS "ps3recomp: shaderc not found, GLSL->SPIR-V tests disabled")
endif()

# OBJECT library: linking it into ps3recomp_runtime folds the ImGui objects
# INTO libps3recomp_runtime.a, so hand-written links that use only that archive
# (games/gow2/build_macos.sh, the Windows scripts) resolve every ImGui symbol
# rsx_overlay.o needs. Tests that compile the overlay core directly (without
# the runtime archive) link this target themselves; nothing links both.
add_library(ps3recomp_imgui OBJECT
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui_draw.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui_tables.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui_widgets.cpp"
)
target_compile_features(ps3recomp_imgui PUBLIC cxx_std_20)
target_include_directories(ps3recomp_imgui PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui"
)

# Inter (third_party/inter, OFL) embedded as C arrays: the console shell's
# fallback typeface. Object library so the runtime archive and the tests share
# one generated copy.
set(PS3RECOMP_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(PS3RECOMP_INTER_SOURCES)
foreach(pair "Inter-Regular.ttf|rsx_overlay_inter_regular" "Inter-SemiBold.ttf|rsx_overlay_inter_semibold")
    string(REPLACE "|" ";" parts "${pair}")
    list(GET parts 0 font_file)
    list(GET parts 1 font_symbol)
    add_custom_command(
        OUTPUT "${PS3RECOMP_GEN_DIR}/${font_symbol}.c"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PS3RECOMP_GEN_DIR}"
        COMMAND ${CMAKE_COMMAND}
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/third_party/inter/${font_file}
            -DOUTPUT=${PS3RECOMP_GEN_DIR}/${font_symbol}.c
            -DSYMBOL=${font_symbol}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/inter/${font_file}"
                "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake"
        VERBATIM)
    list(APPEND PS3RECOMP_INTER_SOURCES "${PS3RECOMP_GEN_DIR}/${font_symbol}.c")
endforeach()
add_library(ps3recomp_overlay_fonts OBJECT ${PS3RECOMP_INTER_SOURCES})

add_library(ps3recomp_runtime STATIC
    ${HLE_SOURCES}
    ${RUNTIME_SOURCES}
)
target_link_libraries(ps3recomp_runtime PRIVATE ps3recomp_imgui ps3recomp_overlay_fonts)

if(APPLE)
    set(PS3RECOMP_OBJC_ARC_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video/rsx_metal_backend.m"
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video/movie_vt_metal.m"
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/codec/cellVdec_vt.m"
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/input/cellpad_gamecontroller.m"
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video/rsx_overlay_assets_apple.m"
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video/rsx_overlay_feedback_apple.m"
    )
    if(PS3RECOMP_OVERLAY_RENDERER_SOURCES)
        list(APPEND PS3RECOMP_OBJC_ARC_SOURCES
            "${PS3RECOMP_OVERLAY_METAL_SOURCE}")
    endif()
    set_source_files_properties(
        ${PS3RECOMP_OBJC_ARC_SOURCES}
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")

    target_link_libraries(ps3recomp_runtime PUBLIC
        "-framework Metal"
        "-framework MetalFX"
        "-framework MetalPerformanceShaders"
        "-framework QuartzCore"
        "-framework CoreGraphics"
        "-framework Foundation"
        "-framework AppKit"
        "-framework AVFoundation"
        "-framework CoreMedia"
        "-framework CoreVideo"
        "-framework VideoToolbox"
        "-framework AudioToolbox"
        "-framework CoreAudio"
        "-framework GameController"
        "-framework CoreHaptics"
        "-framework CoreText"
    )
endif()

if(Vulkan_FOUND)
    target_link_libraries(ps3recomp_runtime PUBLIC Vulkan::Vulkan)
endif()

target_include_directories(ps3recomp_runtime PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# HLE modules include their own headers
target_include_directories(ps3recomp_runtime PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/system"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/filesystem"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/input"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/audio"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/network"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/spurs"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/sync"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/codec"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/font"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/misc"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/spu"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# Platform-specific link libraries
if(WIN32)
    target_link_libraries(ps3recomp_runtime PRIVATE
        ws2_32      # Winsock for network modules
        xinput      # XInput for gamepad
        ole32       # COM for WASAPI audio
        bcrypt      # BCryptGenRandom for SSL RNG
        d3d12       # D3D12 backend + overlay renderer (MSVC also gets these
        dxgi        # from #pragma comment; MinGW needs them spelled out)
        d3dcompiler
    )
else()
    target_include_directories(ps3recomp_runtime PUBLIC ${PS3RECOMP_SDL2_INCLUDE_DIRS})
    target_link_libraries(ps3recomp_runtime PUBLIC ${PS3RECOMP_SDL2_LIBRARIES})
    find_package(Threads REQUIRED)
    target_link_libraries(ps3recomp_runtime PUBLIC Threads::Threads)
endif()

# ---------------------------------------------------------------------------
# rsx_replay (macOS): offline replay of a PS3_RSX_CAPTURE file through the
# PRODUCTION Metal backend. Not part of the runtime library: it links only the
# RSX/video sources the Metal backend needs, plus tools/rsx_replay/replay_shim.c
# in place of the PPU runtime and cellGcmSys.c. Tested by
# libs/video/tests/test_rsx_replay_synthetic.sh (a script, not an add_test).
# ---------------------------------------------------------------------------
if(APPLE)
    add_executable(rsx_replay
        tools/rsx_replay/rsx_replay_main.c
        tools/rsx_replay/replay_shim.c
        tools/rsx_replay/replay_overlay_stubs.c
        libs/video/rsx_metal_backend.m
        libs/video/rsx_commands.c
        libs/video/rsx_capture.c
        libs/video/rsx_capture_ranges.c
        libs/video/rsx_core_decode.c
        libs/video/rsx_core_pipe.c
        libs/video/rsx_core_rules.c
        libs/video/rsx_fp_decompiler.c
        libs/video/rsx_vp_decompiler.c
        libs/video/rsx_glsl_dialect.c
        libs/video/rsx_ucode_dump.c
        libs/video/rsx_depth_debug.c
        libs/video/rsx_rt_feedback.c
        libs/video/rsx_frame_dump.c)
    target_include_directories(rsx_replay PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/input"
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/spu"
        ${PS3RECOMP_SDL2_INCLUDE_DIRS})
    target_compile_options(rsx_replay PRIVATE -fno-strict-aliasing)
    target_link_libraries(rsx_replay PRIVATE
        ${PS3RECOMP_SDL2_LIBRARIES}
        "-framework Metal"
        "-framework MetalFX"
        "-framework QuartzCore"
        "-framework CoreGraphics"
        "-framework CoreVideo"
        "-framework Foundation"
        "-framework AppKit")
endif()

# ---------------------------------------------------------------------------
# Compiler flags
# ---------------------------------------------------------------------------
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(ps3recomp_runtime PRIVATE
        -Wall -Wextra -Wno-unused-parameter
        -fno-strict-aliasing
    )
elseif(MSVC)
    target_compile_options(ps3recomp_runtime PRIVATE
        /W3 /wd4100 /wd4244 /wd4267 /wd4996
        /Zc:__cplusplus
        /experimental:c11atomics   # Enable C11 <stdatomic.h> support
    )
    target_compile_definitions(ps3recomp_runtime PRIVATE
        _CRT_SECURE_NO_WARNINGS
    )
endif()

# ---------------------------------------------------------------------------
# Tests (task F0-5)
#
# Só regista o que já existe e compila hoje neste checkout. Candidatos que
# não compilam ou precisam de dados/toolchain que não temos aqui (ELF do
# jogo, SDK PS3, um checkout externo, um servidor de janelas) ficam de fora
# e documentados no relatório da task (.superpowers/sdd/task-F0-5-report.md)
# em vez de um add_test() forjado que falha sempre.
#
# Dois candidatos verificados por amostragem NÃO entram por compilarem limpo
# mas FALHAREM de facto (bugs reais de produto, não desta task):
# libs/video/tests/test_fp_decompiler.c (7 passed, 2 failed --
# mov_dest/mad_const) e libs/video/tests/test_rsx_fifo.c (0/4 asserts
# passam). Registá-los "verde" seria forjar resultado -- ficam de fora até
# uma task própria os corrigir (ver task-F0-5-report.md §3).
# ---------------------------------------------------------------------------
if(PS3RECOMP_BUILD_TESTS)

    # Convenção de SKIP (task F0-3): um self-test que não pode correr aqui
    # (falta um compilador/SDK) imprime "SKIP: <motivo>" em stdout e sai com
    # código 0 -- não é falha do produto. Sem tratamento, exit 0 faz o CTest
    # reportar "Passed", escondendo que nada correu de facto.
    # SKIP_REGULAR_EXPRESSION (CMake >= 3.16; este projecto já exige >= 3.20)
    # faz o CTest procurar o padrão na saída e reportar "Not Run" em vez de
    # "Passed", seja qual for o código de saída. Aplicado a todo o self-test
    # Python: é inofensivo nos que nunca imprimem SKIP (o regex simplesmente
    # não bate) e cobre também os que só ganham um caminho de SKIP no futuro.
    #
    # SEM "^": o SKIP_REGULAR_EXPRESSION do CMake ancora "^" ao início de TODA
    # a saída capturada do teste, não ao início de cada linha (confirmado por
    # reprodução isolada -- ver task-F0-5-report.md/task-F0-6-report.md
    # secção "Correcções pós-review"). Com "^SKIP:", qualquer preâmbulo antes
    # da linha "SKIP: ..." (ex.: tools/test_ppu_lift.py imprime ~7 linhas de
    # checks fpscr/vrsave antes de decidir que falta o compilador C++ host)
    # faz o CTest classificar o teste como "Passed" em vez de "Skipped" --
    # exactamente o mascaramento que esta convenção existe para evitar. Sem o
    # "^", o regex bate em qualquer posição da saída; confirmado por grep nos
    # 8 scripts embrulhados aqui que nenhum imprime a substring literal
    # "SKIP:" fora de um caminho de skip genuíno, por isso o regex
    # desancorado não pode capturar um FAIL real por engano.
    set(PS3RECOMP_SKIP_REGEX "SKIP:")

    # --- Self-tests Python de tools/ (game-agnostic, sem SDK PS3 nem ELF) --
    # test_ppu_lift.py e runtime/ppu/tests/test_ppu_lifter_emit.py compilam e
    # correm um harness C++ gerado (precisam de um compilador C++ host, já
    # exigido para o próprio projecto); lift_selftest.py e spu_lift_selftest.py
    # SKIPam de facto aqui (sem --ppu-gcc/--spu-gcc do SDK PS3, verificado).
    foreach(_ps3_pytest IN ITEMS
        test_audit_boundaries
        test_audit_fallthrough
        test_check_block_record
        test_find_functions_prologue_corroboration
        test_ghidra_lookup
        test_lift_parity
        test_objdump_audit
        test_ppu_bounds_repair
        test_ppu_disasm_regs
        test_ppu_jumptables
        test_ppu_lift
        test_recomp_config
        test_spu_lift_ops
        test_patch_spu_add_entries
        test_spu_indirect_entries
        test_upstream_delta
        test_pad_gesture_off_poll
        test_boot_macos_overlay_shutdown
        test_upstream_jt_contrib
        test_wall4_subtag_fix
        test_wall1_subtag_fix
        lift_selftest
        spu_lift_selftest
    )
        set(_ps3_pytest_path "${CMAKE_CURRENT_SOURCE_DIR}/tools/${_ps3_pytest}.py")
        if(NOT EXISTS "${_ps3_pytest_path}")
            set(_ps3_pytest_path "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/${_ps3_pytest}.py")
        endif()
        if(NOT EXISTS "${_ps3_pytest_path}")
            message(FATAL_ERROR "ps3recomp: teste Python ausente: ${_ps3_pytest}")
        endif()
        add_test(NAME py.${_ps3_pytest}
            COMMAND ${Python3_EXECUTABLE} "${_ps3_pytest_path}")
        set_tests_properties(py.${_ps3_pytest} PROPERTIES
            SKIP_REGULAR_EXPRESSION "${PS3RECOMP_SKIP_REGEX}")
    endforeach()

    add_test(NAME py.ppu_lifter_emit
        COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_CURRENT_SOURCE_DIR}/runtime/ppu/tests/test_ppu_lifter_emit.py")
    set_tests_properties(py.ppu_lifter_emit PROPERTIES
        SKIP_REGULAR_EXPRESSION "${PS3RECOMP_SKIP_REGEX}")

    # --- Unidades C/C++ auto-contidas (sem lifter, sem ELF do jogo) --------
    # Cada uma inclui só cabeçalhos (relativos ou -I explícito abaixo) e
    # define os seus próprios stubs -- provado por compilação directa antes
    # de entrar aqui (ver o relatório da task).

    # ppu_ops.h/ppu_context.h são header-only (static inline): basta compilar
    # o .cpp. -ffp-contract=off é exigido pelo próprio cabeçalho do teste (o
    # pragma STDC FP_CONTRACT OFF sozinho não é honrado por todos os
    # toolchains) para impedir o compilador de fundir a*c+b num FMA e mascarar
    # a divergência de double-rounding que o teste existe para provar.
    add_executable(test_ppu_fpu_lmw runtime/ppu/tests/test_ppu_fpu_lmw.cpp)
    target_compile_options(test_ppu_fpu_lmw PRIVATE -ffp-contract=off)
    if(NOT WIN32)
        target_link_libraries(test_ppu_fpu_lmw PRIVATE m)
    endif()
    add_test(NAME test_ppu_fpu_lmw COMMAND test_ppu_fpu_lmw)

    add_executable(test_ppu_blrl_call runtime/ppu/tests/test_ppu_blrl_call.cpp)
    add_test(NAME test_ppu_blrl_call COMMAND test_ppu_blrl_call)

    add_executable(test_ppu_icall_ascii
        runtime/ppu/tests/test_ppu_icall_ascii.c
        runtime/ppu/ppu_icall_ascii.c)
    add_test(NAME test_ppu_icall_ascii COMMAND test_ppu_icall_ascii)

    add_executable(test_ppu_icall_hot
        runtime/ppu/tests/test_ppu_icall_hot.c)
    add_test(NAME test_ppu_icall_hot COMMAND test_ppu_icall_hot)

    add_executable(test_ppu_vm_fast_policy
        runtime/ppu/tests/test_ppu_vm_fast_policy.c
        runtime/ppu/ppu_vm_fast_policy.c)
    add_test(NAME test_ppu_vm_fast_policy COMMAND test_ppu_vm_fast_policy)

    add_executable(test_ppu_p10_ctr
        runtime/ppu/tests/test_ppu_p10_ctr.c
        runtime/ppu/ppu_p10_ctr.c)
    add_test(NAME test_ppu_p10_ctr COMMAND test_ppu_p10_ctr)

    add_executable(test_ppu_vm_read32_hot
        runtime/ppu/tests/test_ppu_vm_read32_hot.c)
    add_test(NAME test_ppu_vm_read32_hot COMMAND test_ppu_vm_read32_hot)

    add_executable(test_ppu_vm_gameplay
        runtime/ppu/tests/test_ppu_vm_gameplay.c)
    add_test(NAME test_ppu_vm_gameplay COMMAND test_ppu_vm_gameplay)

    add_executable(test_ppu_loader_vm_undef
        runtime/ppu/tests/test_ppu_loader_vm_undef.c)
    add_test(NAME test_ppu_loader_vm_undef COMMAND test_ppu_loader_vm_undef)

    add_executable(test_ppu_vm_image
        runtime/ppu/tests/test_ppu_vm_image.c)
    add_test(NAME test_ppu_vm_image COMMAND test_ppu_vm_image)

    add_executable(test_ppu_store32_be
        runtime/ppu/tests/test_ppu_store32_be.c)
    add_test(NAME test_ppu_store32_be COMMAND test_ppu_store32_be)

    # Sem includes do projecto: só mirroriza as constantes públicas de
    # cellSaveData.h (ver comentário no próprio ficheiro).
    #
    # Este ficheiro verifica TUDO só via assert() (sem caminho de falha
    # alternativo) -- e um configure "-DCMAKE_BUILD_TYPE=Release" (a receita
    # "padrão" do motor, ver CLAUDE.md/build-macos) injecta -DNDEBUG via
    # CMAKE_C_FLAGS_RELEASE, o que desliga assert() da libc: o teste passaria
    # a reportar "Passed" sempre, mesmo com um bug real (achado F0-6, provado
    # por injecção). -UNDEBUG depois de -DNDEBUG na mesma linha de comando
    # undefine a macro outra vez (gcc/clang processam -D/-U em ordem; o CMake
    # coloca as compile_options do alvo DEPOIS de CMAKE_<LANG>_FLAGS_<CONFIG>
    # na FLAGS gerada -- confirmado com `ninja -t commands test_autoload_map`)
    # -- por isso assert() continua activo neste alvo independentemente do
    # build type escolhido por quem configura. Fix na wiring (CMake), não no
    # ficheiro de teste (fora do âmbito). Provado por injecção nova: ver
    # task-F0-5-report.md secção "Correcções pós-review".
    # (MSVC usa "/U" em vez de "-U" para remover uma macro predefinida; não
    # verificado num host Windows nesta task -- só a sintaxe está coberta.)
    # Memory card automatico: AutoSave2/AutoLoad2 guiados por callbacks do guest,
    # com memoria de guest e um "jogo" falsos. assert() mantido activo (-UNDEBUG),
    # pelo mesmo motivo do test_autoload_map abaixo.
    add_executable(test_savedata_auto
        libs/system/tests/test_savedata_auto.c
        libs/system/cellSaveData.c)
    target_include_directories(test_savedata_auto PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/system
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    if(MSVC)
        target_compile_options(test_savedata_auto PRIVATE /UNDEBUG)
    else()
        target_compile_options(test_savedata_auto PRIVATE -UNDEBUG)
    endif()
    add_test(NAME test_savedata_auto COMMAND test_savedata_auto)

    add_executable(test_autoload_map libs/system/tests/test_autoload_map.c)
    if(MSVC)
        target_compile_options(test_autoload_map PRIVATE /UNDEBUG)
    else()
        target_compile_options(test_autoload_map PRIVATE -UNDEBUG)
    endif()
    add_test(NAME test_autoload_map COMMAND test_autoload_map)

    # coreaudio_ring.h é header-only; "../coreaudio_ring.h" resolve-se pela
    # busca relativa ao próprio ficheiro-fonte, sem -I extra.
    add_executable(test_coreaudio_ring libs/audio/tests/test_coreaudio_ring.c)
    add_test(NAME test_coreaudio_ring COMMAND test_coreaudio_ring)

    add_executable(test_audio_port_mix libs/audio/tests/test_audio_port_mix.c)
    target_include_directories(test_audio_port_mix PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME test_audio_port_mix COMMAND test_audio_port_mix)

    add_executable(test_pm_arg runtime/spu/tests/test_pm_arg.c)
    target_include_directories(test_pm_arg PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/spu")
    add_test(NAME test_pm_arg COMMAND test_pm_arg)

    # SPU interpreter: links the REAL channel/MFC glue (spu_channels.c +
    # spu_dma.h); the test stubs the eight runtime symbols those import.
    add_executable(test_spu_interp
        runtime/spu/tests/test_spu_interp.c
        runtime/spu/spu_interp.c
        runtime/spu/spu_channels.c)
    target_include_directories(test_spu_interp PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/spu")
    target_link_libraries(test_spu_interp PRIVATE m)
    add_test(NAME test_spu_interp COMMAND test_spu_interp)
    # spu_interp_gen.h is generated from spu_lifter.py/spu_disasm.py: stale = fail.
    add_test(NAME py.gen_spu_interp_check
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_spu_interp.py" --check)

    # PS3_MUTE: o teste chama ps3_audio_muted() do cellAudio, que so' precisa
    # do getenv -- linka a runtime para ser a funcao REAL a ser testada.
    add_executable(test_audio_muted
        libs/audio/tests/test_audio_muted.c
        libs/audio/audio_mute.c)
    target_include_directories(test_audio_muted PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/libs/audio")
    add_test(NAME test_audio_muted COMMAND test_audio_muted)

    add_executable(test_ps3_mods
        libs/filesystem/tests/test_ps3_mods.c
        libs/filesystem/ps3_mods.c)
    target_include_directories(test_ps3_mods PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME test_ps3_mods COMMAND test_ps3_mods)

    add_executable(test_cellvdec_ingame
        libs/codec/tests/test_cellvdec_ingame.c
        libs/codec/cellVdec.c
        libs/codec/cellDmux.c
        libs/codec/cellPamf.c
        runtime/trace/ps3_trace.c
        libs/video/rsx_overlay_log.c
        games/gow2/movie_eos_arm.c)
    target_include_directories(test_cellvdec_ingame PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/codec"
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/trace/tests")
    if(APPLE)
        target_sources(test_cellvdec_ingame PRIVATE libs/codec/cellVdec_vt.m)
        target_link_libraries(test_cellvdec_ingame PRIVATE
            "-framework Foundation" "-framework VideoToolbox" "-framework CoreMedia" "-framework CoreVideo")
    endif()
    add_test(NAME test_cellvdec_ingame COMMAND test_cellvdec_ingame)

    add_executable(test_cellvdec_force_gate
        libs/codec/tests/test_cellvdec_force_gate.c
        libs/codec/cellVdec.c
        runtime/trace/ps3_trace.c)
    target_include_directories(test_cellvdec_force_gate PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/codec"
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/trace/tests")
    if(APPLE)
        target_sources(test_cellvdec_force_gate PRIVATE libs/codec/cellVdec_vt.m)
        target_link_libraries(test_cellvdec_force_gate PRIVATE
            "-framework Foundation" "-framework VideoToolbox" "-framework CoreMedia" "-framework CoreVideo")
    endif()
    add_test(NAME test_cellvdec_force_gate COMMAND test_cellvdec_force_gate)


    # rsx_texture_deswizzle.h/rsx_texture_formats.h vivem em libs/video (um
    # nível acima da pasta tests/), por isso precisam do -I explícito.
    add_executable(test_texture_deswizzle libs/video/tests/test_texture_deswizzle.c)
    target_include_directories(test_texture_deswizzle PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME test_texture_deswizzle COMMAND test_texture_deswizzle)

    add_executable(test_rsx_overlay_settings
        libs/video/tests/test_rsx_overlay_settings.c
        libs/video/rsx_overlay_settings.c)
    target_include_directories(test_rsx_overlay_settings PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME test_rsx_overlay_settings COMMAND test_rsx_overlay_settings)

    # The overlay core as the four core-linking tests build it: shell logic,
    # shell drawing, embedded glyph atlas and the runtime mute it drives.
    set(PS3RECOMP_OVERLAY_CORE_TEST_SOURCES
        libs/video/rsx_overlay.cpp
        libs/video/rsx_overlay_log.c
        libs/video/rsx_overlay_settings.c
        libs/video/rsx_overlay_shell.cpp
        libs/video/rsx_overlay_shell_draw.cpp
        libs/video/rsx_overlay_assets.c
        libs/audio/audio_mute.c)

    add_executable(test_rsx_overlay
        libs/video/tests/test_rsx_overlay.c
        ${PS3RECOMP_OVERLAY_CORE_TEST_SOURCES})
    target_include_directories(test_rsx_overlay PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/input"
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/audio"
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
    target_compile_definitions(test_rsx_overlay PRIVATE RSX_OVERLAY_TESTING=1)
    target_link_libraries(test_rsx_overlay PRIVATE ps3recomp_imgui ps3recomp_overlay_fonts Threads::Threads)
    add_test(NAME test_rsx_overlay COMMAND test_rsx_overlay)

    add_executable(test_rsx_overlay_shell
        libs/video/tests/test_rsx_overlay_shell.c
        libs/video/rsx_overlay_shell.cpp)
    target_include_directories(test_rsx_overlay_shell PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/input"
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    set_target_properties(test_rsx_overlay_shell PROPERTIES LINKER_LANGUAGE CXX)
    add_test(NAME test_rsx_overlay_shell COMMAND test_rsx_overlay_shell)

    add_executable(test_rsx_overlay_assets
        libs/video/tests/test_rsx_overlay_assets.c
        libs/video/rsx_overlay_assets.c)
    target_include_directories(test_rsx_overlay_assets PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
    target_link_libraries(test_rsx_overlay_assets PRIVATE ps3recomp_overlay_fonts m)
    add_test(NAME test_rsx_overlay_assets COMMAND test_rsx_overlay_assets)

    if(APPLE)
        add_executable(test_rsx_overlay_assets_apple
            libs/video/tests/test_rsx_overlay_assets_apple.m
            libs/video/rsx_overlay_assets_apple.m
            libs/video/rsx_overlay_assets.c
            libs/video/rsx_overlay_log.c)
        set_source_files_properties(
            libs/video/tests/test_rsx_overlay_assets_apple.m libs/video/rsx_overlay_assets_apple.m
            PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        target_include_directories(test_rsx_overlay_assets_apple PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
        target_compile_definitions(test_rsx_overlay_assets_apple PRIVATE RSX_OVERLAY_TESTING=1)
        target_link_libraries(test_rsx_overlay_assets_apple PRIVATE ps3recomp_overlay_fonts
            Threads::Threads "-framework AppKit" "-framework CoreText" "-framework Foundation")
        add_test(NAME test_rsx_overlay_assets_apple COMMAND test_rsx_overlay_assets_apple)
    endif()

    add_executable(test_rsx_overlay_ui_sound
        libs/video/tests/test_rsx_overlay_ui_sound.c
        libs/video/rsx_overlay_ui_sound.c)
    target_include_directories(test_rsx_overlay_ui_sound PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/libs/video")
    target_link_libraries(test_rsx_overlay_ui_sound PRIVATE m)
    add_test(NAME test_rsx_overlay_ui_sound COMMAND test_rsx_overlay_ui_sound)
    if(APPLE)
        add_executable(test_rsx_overlay_feedback_apple
            libs/video/tests/test_rsx_overlay_feedback_apple.m
            libs/video/rsx_overlay_feedback_apple.m
            libs/video/rsx_overlay_ui_sound.c
            libs/video/rsx_overlay_log.c)
        set_source_files_properties(
            libs/video/tests/test_rsx_overlay_feedback_apple.m libs/video/rsx_overlay_feedback_apple.m
            PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        target_include_directories(test_rsx_overlay_feedback_apple PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/libs/video")
        target_compile_definitions(test_rsx_overlay_feedback_apple PRIVATE RSX_OVERLAY_TESTING=1)
        target_link_libraries(test_rsx_overlay_feedback_apple PRIVATE Threads::Threads
            "-framework AVFoundation" "-framework Foundation")
        add_test(NAME test_rsx_overlay_feedback_apple COMMAND test_rsx_overlay_feedback_apple)
    endif()

    # GoW2 diagnostics provider (games/gow2) against the real overlay core.
    add_executable(test_gow2_overlay_provider
        games/gow2/tests/test_gow2_overlay_provider.c
        games/gow2/gow2_overlay_provider.c
        ${PS3RECOMP_OVERLAY_CORE_TEST_SOURCES})
    target_include_directories(test_gow2_overlay_provider PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/input"
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/audio"
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
    target_link_libraries(test_gow2_overlay_provider PRIVATE ps3recomp_imgui ps3recomp_overlay_fonts Threads::Threads)
    add_test(NAME test_gow2_overlay_provider COMMAND test_gow2_overlay_provider)

    add_executable(test_rsx_overlay_d3d12
        libs/video/tests/test_rsx_overlay_d3d12.c)
    target_include_directories(test_rsx_overlay_d3d12 PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME test_rsx_overlay_d3d12 COMMAND test_rsx_overlay_d3d12)

    if(APPLE)
        # Headless Metal overlay renderer: known packets rendered offscreen and
        # read back, plus one frame from the real overlay core.
        add_executable(test_rsx_overlay_metal
            libs/video/tests/test_rsx_overlay_metal.m
            libs/video/rsx_overlay_metal.m
            libs/video/rsx_overlay_assets_apple.m
            ${PS3RECOMP_OVERLAY_CORE_TEST_SOURCES})
        set_source_files_properties(
            libs/video/tests/test_rsx_overlay_metal.m libs/video/rsx_overlay_assets_apple.m
            PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        target_include_directories(test_rsx_overlay_metal PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
            "${CMAKE_CURRENT_SOURCE_DIR}/libs/input"
            "${CMAKE_CURRENT_SOURCE_DIR}/libs/audio"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui"
            ${PS3RECOMP_SDL2_INCLUDE_DIRS})
        target_compile_definitions(test_rsx_overlay_metal PRIVATE RSX_OVERLAY_TESTING=1)
        target_link_libraries(test_rsx_overlay_metal PRIVATE
            ps3recomp_imgui ps3recomp_overlay_fonts Threads::Threads
            "-framework Metal" "-framework MetalPerformanceShaders" "-framework Foundation"
            "-framework AppKit" "-framework CoreText" "-framework QuartzCore")
        add_test(NAME test_rsx_overlay_metal COMMAND test_rsx_overlay_metal)
        set_tests_properties(test_rsx_overlay_metal PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1")

        # Link-only smoke of libps3recomp_runtime.a as the GoW2 host consumes
        # it: the archive FILE (not the CMake target, whose usage requirements
        # could mask a missing object) plus build_macos.sh's frameworks.
        add_executable(test_runtime_overlay_link
            libs/video/tests/test_runtime_overlay_link.c)
        target_include_directories(test_runtime_overlay_link PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/libs/video")
        add_dependencies(test_runtime_overlay_link ps3recomp_runtime)
        set_target_properties(test_runtime_overlay_link PROPERTIES LINKER_LANGUAGE CXX)
        target_link_libraries(test_runtime_overlay_link PRIVATE
            "$<TARGET_FILE:ps3recomp_runtime>"
            "-framework Metal" "-framework MetalFX" "-framework MetalPerformanceShaders"
            "-framework QuartzCore"
            "-framework Foundation" "-framework Cocoa" "-framework AVFoundation"
            "-framework CoreMedia" "-framework CoreVideo" "-framework AudioToolbox"
            "-framework CoreAudio" "-framework GameController" "-framework CoreHaptics"
            "-framework CoreText"
            ${PS3RECOMP_SDL2_LIBRARIES} m)
        add_test(NAME test_runtime_overlay_link COMMAND test_runtime_overlay_link)
    endif()

    add_executable(test_rsx_overlay_log
        libs/video/tests/test_rsx_overlay_log.c
        libs/video/rsx_overlay_log.c)
    target_include_directories(test_rsx_overlay_log PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video")
    find_package(Threads REQUIRED)
    target_link_libraries(test_rsx_overlay_log PRIVATE Threads::Threads)
    add_test(NAME test_rsx_overlay_log COMMAND test_rsx_overlay_log)

    # NV40 FP → MSL. Same rsx_fp_decompile_msl Metal set_shader uses. Do NOT
    # register test_fp_decompiler.c (known product failures mov_dest/mad_const).
    add_executable(test_msl_decompiler
        libs/video/tests/test_msl_decompiler.c
        libs/video/rsx_fp_decompiler.c
        libs/video/rsx_glsl_dialect.c)
    target_include_directories(test_msl_decompiler PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    if(APPLE)
        set_source_files_properties(
            "${CMAKE_CURRENT_SOURCE_DIR}/libs/video/tests/test_msl_decompiler.c"
            PROPERTIES LANGUAGE OBJC)
        target_compile_definitions(test_msl_decompiler PRIVATE TEST_MSL_METAL_COMPILE=1)
        target_link_libraries(test_msl_decompiler PRIVATE
            "-framework Metal" "-framework Foundation")
    endif()
    add_test(NAME test_msl_decompiler COMMAND test_msl_decompiler)

    add_executable(test_glsl_dialect
        libs/video/tests/test_glsl_dialect.c
        libs/video/rsx_glsl_dialect.c)
    target_include_directories(test_glsl_dialect PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME test_glsl_dialect COMMAND test_glsl_dialect)

    add_executable(test_rsx_core_decode
        libs/video/tests/test_rsx_core_decode.c
        libs/video/rsx_core_decode.c)
    target_include_directories(test_rsx_core_decode PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video" "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME test_rsx_core_decode COMMAND test_rsx_core_decode)

    add_executable(test_rsx_core_rules
        libs/video/tests/test_rsx_core_rules.c
        libs/video/rsx_core_rules.c
        libs/video/rsx_core_decode.c)
    target_include_directories(test_rsx_core_rules PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video" "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME test_rsx_core_rules COMMAND test_rsx_core_rules)

    add_executable(test_rsx_core_pipe libs/video/tests/test_rsx_core_pipe.c libs/video/rsx_core_pipe.c)
    target_include_directories(test_rsx_core_pipe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/libs/video" "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME test_rsx_core_pipe COMMAND test_rsx_core_pipe)
    if(APPLE)
        add_executable(test_rsx_core_pipe_metal libs/video/tests/test_rsx_core_pipe_metal.m libs/video/rsx_core_pipe.c)
        set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/libs/video/tests/test_rsx_core_pipe_metal.m" PROPERTIES LANGUAGE OBJC)
        target_include_directories(test_rsx_core_pipe_metal PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/libs/video" "${CMAKE_CURRENT_SOURCE_DIR}/include")
        target_link_libraries(test_rsx_core_pipe_metal PRIVATE "-framework Metal" "-framework Foundation")
        add_test(NAME test_rsx_core_pipe_metal COMMAND test_rsx_core_pipe_metal)
    endif()

    if(PS3RECOMP_HAVE_SHADERC)
        add_executable(test_shaderc_smoke libs/video/tests/test_shaderc_smoke.c)
        target_link_libraries(test_shaderc_smoke PRIVATE PkgConfig::SHADERC)
        add_test(NAME test_shaderc_smoke COMMAND test_shaderc_smoke)

        add_executable(test_glsl_decompiler
            libs/video/tests/test_glsl_decompiler.c
            libs/video/rsx_fp_decompiler.c
            libs/video/rsx_vp_decompiler.c
            libs/video/rsx_glsl_dialect.c)
        target_include_directories(test_glsl_decompiler PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
            "${CMAKE_CURRENT_SOURCE_DIR}/include")
        target_link_libraries(test_glsl_decompiler PRIVATE PkgConfig::SHADERC)
        add_test(NAME test_glsl_decompiler COMMAND test_glsl_decompiler)

        # Corpus validator (plan 2026-09-23-vulkan-v0, task 6): CLI tool, not a
        # ctest (it needs real RPCS3/PS3_RSX_DUMP_UCODE game-data dirs on disk).
        add_executable(validate_glsl_corpus
            libs/video/tests/validate_glsl_corpus.c
            libs/video/rsx_fp_decompiler.c
            libs/video/rsx_vp_decompiler.c
            libs/video/rsx_glsl_dialect.c)
        target_include_directories(validate_glsl_corpus PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
            "${CMAKE_CURRENT_SOURCE_DIR}/include")
        target_link_libraries(validate_glsl_corpus PRIVATE PkgConfig::SHADERC)
    endif()

    # PS3_RSX_DUMP_UCODE=<dir>: raw FP/VP microcode dump for the Vulkan GLSL
    # emitter corpus (plan 2026-09-23-vulkan-v0). No-op unless the env var is set.
    add_executable(test_ucode_dump
        libs/video/tests/test_ucode_dump.c
        libs/video/rsx_ucode_dump.c)
    target_include_directories(test_ucode_dump PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME test_ucode_dump COMMAND test_ucode_dump)

    # RSX frame-capture file format (writer/reader), portable C, no shaderc
    # dependency (plan 2026-09-23-vulkan-v2-0-rsx-capture-replay, task 1).
    add_executable(test_rsx_capture
        libs/video/tests/test_rsx_capture.c
        libs/video/rsx_capture.c
        libs/video/rsx_capture_ranges.c
        libs/video/rsx_core_rules.c
        libs/video/rsx_core_decode.c
        libs/video/rsx_fp_decompiler.c
        libs/video/rsx_glsl_dialect.c)
    target_include_directories(test_rsx_capture PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME test_rsx_capture COMMAND test_rsx_capture)

    # Portable calculators of the guest-memory ranges each RSX backend
    # callback reads (mirrors the Metal backend's read rules; plan
    # 2026-09-23-vulkan-v2-0b-draw-granular-capture, task 2).
    add_executable(test_rsx_capture_ranges
        libs/video/tests/test_rsx_capture_ranges.c
        libs/video/rsx_capture_ranges.c
        libs/video/rsx_core_rules.c
        libs/video/rsx_core_decode.c
        libs/video/rsx_fp_decompiler.c
        libs/video/rsx_glsl_dialect.c)
    target_include_directories(test_rsx_capture_ranges PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME test_rsx_capture_ranges COMMAND test_rsx_capture_ranges)

    # PS3_PAD_AUTOSTART sequence: the same pad_autostart_buttons() cellPadGetData
    # calls. Does not pull in the full pad HLE (vm_write / GameController).
    add_executable(test_pad_autostart
        libs/input/tests/test_pad_autostart.c
        libs/input/pad_autostart.c)
    target_include_directories(test_pad_autostart PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/input"
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME test_pad_autostart COMMAND test_pad_autostart)

    # Pad map/deadzone/identity flag: one publication per read (seqlock),
    # publications interleaved deterministically through a test hook.
    add_executable(test_pad_mapping_seqlock libs/input/tests/test_pad_mapping_seqlock.c)
    target_include_directories(test_pad_mapping_seqlock PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/libs/input")
    add_test(NAME test_pad_mapping_seqlock COMMAND test_pad_mapping_seqlock)

    # Keyboard vs controller routing into the runtime overlay (one physical
    # key, one overlay action). Links the real overlay core.
    add_executable(test_pad_overlay_route
        libs/input/tests/test_pad_overlay_route.c
        ${PS3RECOMP_OVERLAY_CORE_TEST_SOURCES})
    target_include_directories(test_pad_overlay_route PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/input"
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/audio"
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
    target_compile_definitions(test_pad_overlay_route PRIVATE RSX_OVERLAY_TESTING=1)
    target_link_libraries(test_pad_overlay_route PRIVATE ps3recomp_imgui ps3recomp_overlay_fonts Threads::Threads)
    add_test(NAME test_pad_overlay_route COMMAND test_pad_overlay_route)

    # Launcher -> runtime handoff: one settings file, no forced env overrides
    # (Swift launcher units + an end-to-end run through the generated launch
    # script into rsx_overlay_resolve_flag's own precedence). macOS only, and
    # only when a Swift toolchain is present.
    # jogar_g2.sh: fullscreen/VSync from the overlay settings file unless the
    # caller exports them; a file without `fullscreen` is seeded to 1.
    if(NOT WIN32)
        add_test(NAME sh.jogar_g2_display
            COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/games/gow2/tests/test_jogar_g2_display.sh"
                         "${CMAKE_CURRENT_BINARY_DIR}/jogar_g2_display")
    endif()
    find_program(PS3RECOMP_SWIFTC swiftc)
    if(APPLE AND PS3RECOMP_SWIFTC)
        add_test(NAME swift.launcher_launch_check
            COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/games/gow2/launcher/macos/tests/launch_check/run.sh"
                         "${CMAKE_CURRENT_BINARY_DIR}/launch_check")
    endif()

    add_executable(test_host_prompt_mesh
        libs/input/tests/test_host_prompt_mesh.c
        libs/input/host_prompt_mesh.c)
    target_include_directories(test_host_prompt_mesh PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/input)
    add_test(NAME test_host_prompt_mesh COMMAND test_host_prompt_mesh)

    # Host button prompts: the gate, the device -> icon-set table and "auto".
    add_executable(test_host_prompts
        libs/input/tests/test_host_prompts.c
        libs/input/host_prompts.c
        libs/input/host_prompt_overlay.c)
    target_include_directories(test_host_prompts PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/input"
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME test_host_prompts COMMAND test_host_prompts)

    add_executable(test_rsx_index libs/video/tests/test_rsx_index.c)
    target_include_directories(test_rsx_index PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME test_rsx_index COMMAND test_rsx_index)

    add_executable(test_rsx_arrays
        libs/video/tests/test_rsx_arrays.c
        libs/video/rsx_commands.c
        libs/video/rsx_depth_debug.c
        runtime/trace/ps3_trace.c
        runtime/trace/ps3_checkpoint.c)
    target_include_directories(test_rsx_arrays PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/trace/tests")
    add_test(NAME test_rsx_arrays COMMAND test_rsx_arrays)

    add_executable(test_rsx_depth_debug
        libs/video/tests/test_rsx_depth_debug.c
        libs/video/rsx_depth_debug.c)
    target_include_directories(test_rsx_depth_debug PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME test_rsx_depth_debug COMMAND test_rsx_depth_debug)

    add_executable(test_rt_feedback
        libs/video/tests/test_rt_feedback.c
        libs/video/rsx_rt_feedback.c)
    target_include_directories(test_rt_feedback PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME test_rt_feedback COMMAND test_rt_feedback)

    add_executable(test_boot_logo_queue
        libs/video/tests/test_boot_logo_queue.c
        libs/video/rsx_host_content.c
        libs/video/movie_hle.c
        libs/video/rsx_overlay_log.c
        libs/filesystem/ps3_mods.c
        runtime/trace/ps3_trace.c
        runtime/trace/ps3_checkpoint.c)
    target_include_directories(test_boot_logo_queue PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/trace/tests")
    add_test(NAME test_boot_logo_queue COMMAND test_boot_logo_queue)

    add_executable(test_ps3_patch
        runtime/patch/tests/test_ps3_patch.c
        runtime/patch/ps3_patch.c)
    target_include_directories(test_ps3_patch PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME test_ps3_patch COMMAND test_ps3_patch)

    add_executable(test_gcm_wait_ns
        libs/video/tests/test_gcm_wait_ns.c
        libs/video/gcm_wait_ns.c)
    target_include_directories(test_gcm_wait_ns PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME test_gcm_wait_ns COMMAND test_gcm_wait_ns)

    add_executable(test_gcm_eager_put
        libs/video/tests/test_gcm_eager_put.c)
    target_include_directories(test_gcm_eager_put PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME test_gcm_eager_put COMMAND test_gcm_eager_put)

    add_executable(test_gcm_ovf_unlock
        libs/video/tests/test_gcm_ovf_unlock.c)
    target_include_directories(test_gcm_ovf_unlock PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME test_gcm_ovf_unlock COMMAND test_gcm_ovf_unlock)

    add_executable(test_gcm_fifo_nolock
        libs/video/tests/test_gcm_fifo_nolock.c)
    target_include_directories(test_gcm_fifo_nolock PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME test_gcm_fifo_nolock COMMAND test_gcm_fifo_nolock)

    add_executable(test_gcm_grow_cmd
        libs/video/tests/test_gcm_grow_cmd.c)
    target_include_directories(test_gcm_grow_cmd PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME test_gcm_grow_cmd COMMAND test_gcm_grow_cmd)

    add_executable(test_gcm_fifo_fast
        libs/video/tests/test_gcm_fifo_fast.c)
    target_include_directories(test_gcm_fifo_fast PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME test_gcm_fifo_fast COMMAND test_gcm_fifo_fast)

    add_executable(test_ps3_timebase_scale
        runtime/syscalls/tests/test_ps3_timebase_scale.c
        runtime/syscalls/ps3_timebase_scale.c)
    add_test(NAME test_ps3_timebase_scale COMMAND test_ps3_timebase_scale)

    # --- LV2S-01: sys_spu_image_import escreve `src` como u32 em +0x10 ------
    # Duas decisoes de wiring que NAO sao acidente e por isso ficam escritas:
    #
    # (1) O ficheiro de teste faz #include da unidade de traducao
    #     runtime/syscalls/lv2_register.c (por uma macro de indireccao,
    #     LV2_REGISTER_C). Razao: sys_spu_image_import_handler e' `static`, e a
    #     unica forma de correr o CAMINHO REAL -- em vez de verificar uma copia
    #     da logica, que nao provaria nada sobre a producao -- e' incluir a TU.
    #     A indireccao existe para run_spu_image_import_mutation.sh poder
    #     apontar o MESMO teste a uma copia mutada fora da arvore.
    #
    # (2) Deliberadamente SEM target_link_libraries(... ps3recomp_runtime):
    #     como o teste ja' inclui o lv2_register.c, linkar a biblioteca daria
    #     simbolos duplicados. Os simbolos externos que faltam (os dez
    #     sys_*_init, sys_fs_translate_path, spu_lookup_ppu_fallback e
    #     sys_event_queue_push_by_id) estao stubados dentro do proprio teste.
    #
    # -UNDEBUG pelo mesmo motivo documentado no test_autoload_map acima (um
    # configure Release injecta -DNDEBUG); aqui e' redundancia defensiva, ja'
    # que este teste nao depende de assert() -- cada verificacao tem caminho de
    # falha explicito e o main() devolve 1 pelo contador de falhas.
    add_executable(test_spu_image_import runtime/syscalls/tests/test_spu_image_import.c)
    target_include_directories(test_spu_image_import PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime")
    if(MSVC)
        target_compile_options(test_spu_image_import PRIVATE /UNDEBUG)
    else()
        target_compile_options(test_spu_image_import PRIVATE -UNDEBUG)
    endif()
    add_test(NAME test_spu_image_import COMMAND test_spu_image_import)

    # Prova por MUTACAO do teste acima: repor o defeito antigo numa COPIA fora
    # da arvore tem de deixar o MESMO teste vermelho (e a mesma copia sem a
    # mutacao tem de ficar verde -- controlo positivo). Sem isto, "o teste
    # passa" nao diz que o teste APANHARIA o defeito. O script compila a copia
    # a mao, por isso recebe o mesmo compilador e o mesmo Python que o CMake
    # escolheu (mesmo padrao da suite.runtime_spu). Requer bash no PATH.
    add_test(NAME test_spu_image_import_mutacao
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/runtime/syscalls/tests/run_spu_image_import_mutation.sh")
    set_tests_properties(test_spu_image_import_mutacao PROPERTIES
        ENVIRONMENT "CC=${CMAKE_C_COMPILER};PYTHON=${Python3_EXECUTABLE}")

    # --- Suites com o próprio build/gerador (script já mantido e provado) --
    # runtime/spu/tests/run_tests.sh gera um ELF SPU por caso com
    # gen_test_*.py, lifta-o com tools/spu_lifter.py e compila+corre o
    # resultado -- 21 casos (11 gerados + libs/spurs/tests/* + as fases
    # duplas de spu_fma_precision + o probe TSan), incluindo os candidatos
    # libs/spurs/tests/test_spurs_kernel_main.c, test_spurs_kick_async_main.c
    # e test_lfq_inflate_main.c (chamados de dentro do script via
    # run_extra) -- por isso não têm add_test próprio, já correm aqui.
    # Reimplementar essa geração em CMake duplicaria a lógica só para ganhar
    # granularidade por-caso; embrulhar o script mantido evita essa
    # duplicação (e o risco de as duas cópias divergirem) mantendo um teste
    # real, que corre e reporta passed/failed no CTest.
    # Efeito colateral conhecido (pré-existente ao script, não desta task):
    # cada corrida de run_tests.sh REESCREVE em árvore, dentro do próprio
    # checkout, os pares runtime/spu/tests/out_*/spu_recomp.{c,h} (saída do
    # lifter, já versionada) -- deixa o working tree "sujo" (git status " M
    # ...") depois de cada ctest. scripts/test_runtime.sh (task F0-6) já
    # repõe isto sozinho quando a sujidade bate exactamente com este padrão.
    # Requer bash no PATH (script POSIX; não verificado em Windows aqui).
    add_test(NAME suite.runtime_spu
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/runtime/spu/tests/run_tests.sh")
    set_tests_properties(suite.runtime_spu PROPERTIES
        ENVIRONMENT "PYTHON=${Python3_EXECUTABLE}")

    # runtime/trace/tests/run_tests.sh corre 4 cenários (formato + 3
    # combinações de gating), cada um no seu próprio processo porque
    # ps3_trace_on() cacheia getenv() para sempre -- ver o comentário do
    # próprio script. Mesma lógica de embrulho que a suite SPU acima.
    add_test(NAME suite.runtime_trace
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/runtime/trace/tests/run_tests.sh")

    # --- 23-03: auto-teste do PORTAO DE ADOPCAO (scripts/upstream_block_gate.sh)
    # Corre em segundos: nenhum build, nenhum boot, nenhum worktree.
    #  - Grupos 1 a 5 correm em QUALQUER maquina (incluindo a CI, que nao tem o
    #    checkout do jogo): auditam a disciplina G6 no proprio ficheiro do
    #    portao e exercitam derive_rc_class/perna_a/perna_b/perna_c com
    #    vectores sinteticos, via UPSTREAM_GATE_LIB_ONLY=1.
    #  - Grupo 6 (pre-requisitos do portao) precisa de ../gow2-recomp com
    #    EBOOT.ELF/env_gow2.sh/build_macos.sh/lib_boot_chain_metrics.sh, do dir
    #    de lift e de cmake+ninja; sem isso o script imprime a linha de salto da
    #    convencao F0-3 e sai 0 -- dai o SKIP_REGULAR_EXPRESSION, igual aos py.*
    #    (sem ele o CTest classificava o salto como "Passed").
    #    O proprio script so' imprime essa linha quando nao houve nenhum FAIL,
    #    porque o regex do CTest e' desancorado e mascararia um vermelho.
    # Requer bash no PATH (script POSIX; nao verificado em Windows aqui).
    add_test(NAME test_upstream_block_gate
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_upstream_block_gate.sh")
    set_tests_properties(test_upstream_block_gate PROPERTIES
        SKIP_REGULAR_EXPRESSION "${PS3RECOMP_SKIP_REGEX}")

endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS ps3recomp_runtime
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(DIRECTORY include/ps3emu
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "ps3recomp runtime v${PROJECT_VERSION}")
message(STATUS "  C compiler:   ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "  C++ compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Build type:   ${CMAKE_BUILD_TYPE}")
message(STATUS "")
