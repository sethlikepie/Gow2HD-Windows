# Mapa de referência RPCS3 — navegação rápida para o port GoW2

Clone completo em `../_ref_rpcs3` (1943 ficheiros, HEAD, sem submodules 3rdparty).
Índices gerados (para saltar/pesquisar depressa — o "graph"):

| Índice | Ficheiro | Uso |
|--------|----------|-----|
| **Símbolos** (universal-ctags, 152864) | `../_ref_rpcs3/tags` | vim `:ts <símbolo>`, editor jump-to-def, ou `rg '^<símbolo>\t' ../_ref_rpcs3/tags` |
| **Call/xref graph** (cscope, 19M) | `../_ref_rpcs3/cscope.out` | `cd ../_ref_rpcs3 && cscope -dq -f cscope.out` → find callers/callees/refs |
| Helper | `tools/rpcs3_grep.sh` | `rpcs3_grep.sh def <sym>` / `callers <sym>` / `<regex>` |

Regenerar após `git pull` no clone: `tools/rpcs3_index.sh` (recria tags + cscope.out).

## Mapa: PROBLEMA nosso → CÓDIGO de referência RPCS3

### 1. Formato de captura `.rrc` → escrever `rrc_export.py` (demo de modelo real)
O elo em falta para o demo "modelo real do jogo via a nossa lógica":
RPCS3 corre GoW2 → captura um frame → `.rrc` → `rrc_export.py` → nosso `.rxs` → `replay_mac` (Metal).
- **Formato `.rrc`**: `rpcs3/Emu/RSX/Capture/rsx_replay.h` — magic `"RRC"` (`c_fc_magic`, :13), `struct frame_capture_data` (:17), `memory_block_data`/`memory_block` (:19/:25), `replay_command` (:34). É isto que o `rrc_export.py` tem de desserializar (RPCS3 serializa com `<util/serialization.hpp>`).
- **Como RPCS3 escreve/lê**: `rpcs3/Emu/RSX/Capture/rsx_capture.cpp` (409 linhas) — o que cada `replay_command` contém (method+arg, memória, tile state, image).
- Nosso lado: `ps3recomp/libs/video/tests/replay_main.c:74-153` (formato `.rxs` RXS1 v2) + `replay_mac.m` (a nascer, agente afd6d2f).

### 2. Decompiladores FP/VP → M2 (FP/VP → MSL) e o stage 4 do replay
- **FP**: `rpcs3/Emu/RSX/Program/FragmentProgramDecompiler.h` (class :25, `Decompile()` :207) + `.cpp` (1528 linhas). Emite GLSL; nós queremos MSL — a ESTRUTURA (decode do microcode NV40 → IR → emit) é o que se reusa; o emit diverge.
- **VP**: `rpcs3/Emu/RSX/Program/VertexProgramDecompiler.h` (`Decompile()` :145) + `.cpp`.
- **Cg binary (o que o jogo submete)**: `CgBinaryFragmentProgram.cpp`, `CgBinaryVertexProgram.cpp`.
- Common GLSL (para ver o que traduzir p/ MSL): `GLSLCommon.cpp`, `GLSLTypes.h`.
- Nosso lado: `ps3recomp/libs/video/rsx_fp_decompiler.c` / `rsx_vp_decompiler.c` (sessão concorrente M2).

### 3. Semântica dos métodos NV4097 → validar `rsx_dispatch.c`
- **Tabela de métodos**: `rpcs3/Emu/RSX/rsx_methods.h` — `rsx_method_t` (:15), `methods[0x10000/4]` (:1337), decode de BEGIN_END/DRAW (:966). O que cada método FAZ.
- **Enums GCM**: `gcm_enums.h`, `GCM.h` (formatos de superfície/textura, primitivas).
- Nosso lado: `ps3recomp/libs/video/rsx_dispatch.c` + `rsx_commands.c` (o nosso decode).

### 4. Vertex arrays → interpretar a geometria do jogo (replay stage 2)
- `rpcs3/Emu/RSX/rsx_vertex_data.h` — `push_buffer_vertex_info` (:57), `register_vertex_data_info` (:78).
- `rpcs3/Emu/RSX/Common/BufferUtils.cpp/.h` — `write_vertex_array_data`, tamanhos por tipo, BE→LE.

### 5. HLE Cell → o nosso recomp (vdec/spurs/gcm/fs)
- `rpcs3/Emu/Cell/Modules/` (241 ficheiros): `cellVdec.cpp`, `cellGcmSys.cpp`, `cellResc.cpp`, `cellSpurs*.cpp`, `sys_fs.cpp`, `cellPamf.cpp`, `sceNp*`, ...
- `rpcs3/Emu/Cell/lv2/` (85): syscalls (`sys_*`).
- `rpcs3/Emu/Cell/PPUThread.cpp` (5753) / `SPUThread.cpp` — semântica de CPU (oráculo p/ o lift).
- Uso típico: quando um HLE nosso diverge, comparar com a versão RPCS3 (é intérprete fiel do CELL).

### 6. Parede actual (registry de shaders / CGOWShader) — oráculo
RPCS3 corre o jogo até aos draws reais (o registry popula). Para perceber o que DEVIA acontecer
no caminho typemap/ICGLdrShader, ver como RPCS3 trata o SET_SHADER_PROGRAM / program cache:
- `rpcs3/Emu/RSX/Program/` (program cache, hashing), `rpcs3/Emu/RSX/rsx_cache.h`.
- NB: a parede A1 é guest-side (a nossa FSM não invoca o walk); RPCS3 não tem esse problema (executa o guest inteiro), mas mostra o RESULTADO esperado (shaders reais no cache).

## Regras
- `_ref_rpcs3` é REFERÊNCIA read-only. RPCS3 é GPLv2 — **não copiar código** para o nosso tree;
  ler para perceber semântica e reimplementar clean-room (como `rsx_dispatch.h` já declara).
- `tags` / `cscope.out` / `cscope.files` são artefactos locais (não versionar no nosso repo).
