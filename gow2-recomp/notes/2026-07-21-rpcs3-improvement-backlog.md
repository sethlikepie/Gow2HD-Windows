# Backlog de melhorias via oráculo RPCS3 — GoW2 recomp

**Método:** leitura estática read-only do clone RPCS3 completo (`../_ref_rpcs3`, HEAD) como
oráculo de semântica fiel, cruzada com o nosso código clean-room (`ps3recomp/libs/...`).
Três investigações paralelas (mesmo contexto, forked) cobriram as três áreas do pedido:
(1) formato de captura `.rrc`, (2) fidelidade do decode NV4097 (`rsx_dispatch.c` + os
consumidores do estado que ele decodifica), (3) semântica `cellVdec` FORCE/callback. Os
achados de maior risco — os 3 P1 da Área 2, a alegação de gzip puro e o formato VLE da
Área 1, o clamp de constante P3, e os dois enums `cellVdec` — foram reverificados por
leitura directa do código-fonte (não só aceites do relatório do sub-agente) antes de
escrever esta nota. Nenhum código RPCS3 foi copiado; RPCS3 é GPLv2, referência read-only —
o que aparece citado abaixo é semântica descrita por nós, no máximo com trechos muito
curtos claramente marcados como "referência RPCS3, não copiar".

Nenhum ficheiro de código foi tocado (nem nosso nem do RPCS3) — isto é só análise + esta nota.

## Resumo executivo — topo do backlog

| # | Prioridade | Área | Achado | Por que importa agora |
|---|---|---|---|---|
| 1 | **P1** | 2 (render) | `fetch_attr()` em `rsx_live_draw.c` só decodifica correctamente os tipos FLOAT e UNORM8; os outros 5 tipos de vértice (SNORM16/HALF/SINT16/CMP32/UINT8) caem num `else` que reinterpreta os bytes errados como float | Qualquer malha com atributo não-float (normais/UVs comprimidos — muito comum em conteúdo real) rende geometria lixo/NaN |
| 2 | **P1** | 2 (render) | O triangle strip em `sink_end()` (`rsx_live_draw.c`) não alterna o *winding* dos triângulos ímpares | Com backface culling ligado (default em conteúdo 3D real), ~metade dos triângulos de toda malha em strip fica invisível ou de dentro para fora |
| 3 | **P1** | 2 (render) | Os helpers correctos de expansão quad/fan→triângulo (`rsx_primitives.h`) existem, estão correctos, mas têm **zero chamadores** (verificado por grep); `rsx_d3d12_backend.c` nunca os invoca — e o guard que devia "saltar" esses primitivos nunca dispara para eles | Qualquer draw QUADS/TRIANGLE_FAN (UI, partículas, fans simples) sai garbled no backend D3D12 |
| 4 | **P1 (desbloqueador)** | 1 (`.rrc`) | Spec do `.rrc`→`.rxs` está completa só a partir do código-fonte — envelope é gzip padrão (zlib `windowBits=31`), wire format inteiro batido campo a campo | Desbloqueia a escrita do `rrc_export.py` já, sem precisar de uma captura `.rrc` real em mãos para validar a maior parte do formato |
| 5 | P2 | 2 (render) | `rsx_dsp_get_surface()` não expõe `antialias` nem `log2(width/height)` de `SURFACE_FORMAT`, apesar do próprio comentário do ficheiro já documentar os bits | Só importa em render targets AA ou swizzled — não confirmado ainda se o caminho actual do GoW2 usa isso |
| 6 | P2 | 3 (vdec) | O watchdog FORCE não tem análogo fiel em hardware real (`SEQDONE` só existe via `cellVdecEndSeq()` chamado pelo guest) — não vale a pena tentar "torná-lo mais fiel"; o fix real fica a montante (FSM do movie), fora de `cellVdec.c` | Evita investir tempo a melhorar um timer quando não há nada fiel do RPCS3 para copiar |
| 7 | P2 | 3 (vdec) | O nosso `PICOUT` é rigidamente 1:1 com AU consumida; RPCS3 permite 0..N por AU (reordenação B-frame) | Fragilidade latente — não é bug provado no clipe actual (que já passa GREEN), mas é o primeiro sítio a olhar se um filme com GOP diferente empancar |
| 8 | P3 | 2 (render) | Falta clamp do slot de constante de transform (nós aceitamos 0-511; hardware real limita a 0-467) | Baixo risco — só dispara se um jogo (por bug ou de propósito) escrever nos slots 468-511 |

Também há uma boa notícia estrutural: o dimensionamento do nosso register file
(`RSX_DSP_NUM_REGS = 0x10000/4` e `RSX_DSP_VP_INSTR = 544` em `rsx_dispatch.h:32-33`)
bate **exactamente** com `rsx::rsx_state::registers` e `max_vertex_program_instructions`
do RPCS3 (`rsx_methods.h:81`, `Program/program_util.h:10`) — confirmado de forma
independente pelas investigações da Área 1 e da Área 2. O esqueleto do dispatcher está certo;
os bugs reais ficam nos consumidores do estado que ele decodifica, não no decode em si.

---

## Área 1 — formato `.rrc` → spec para `rrc_export.py`

### A. Layout de bytes do `.rrc`/`.rrc.gz`

**Envelope externo — gzip padrão, não um formato próprio.** O handler realmente ligado a
`.rrc.gz` (`compressed_serialization_file_handler`, instanciado em
`Emu/RSX/RSXThread.cpp:3240`) chama `deflateInit2(..., Z_DEFLATED, 16+15, 9, ...)` para
escrever e `inflateInit2(..., 16+15)` para ler (`util/serialization_ext.cpp` — confirmado
por leitura directa; a linha muda ligeiramente entre write/read mas ambas usam
`windowBits = 16+15 = 31`, a flag documentada do zlib para escrever/ler **envelope gzip
RFC1952** em vez do wrapper zlib normal). Existe um segundo handler,
`compressed_zstd_serialization_file_handler` (`serialization_ext.hpp:121-198`), mas é para
`.zst` — não é o que `.rrc.gz` usa. Resultado prático: `gzip.decompress(...)` do stdlib do
Python remove o envelope inteiro sem qualquer engenharia reversa adicional.

**Stream interno** (pós-descompressão), ordem estrita da esquerda para a direita — o
`operator()` variádico de `struct serial` (`util/serialization.hpp`) serializa os
argumentos na ordem em que aparecem na chamada, e essa ordem = ordem em disco:

| Campo | Tipo / tamanho | Codificação | Citação (struct / lógica de serialize) |
|---|---|---|---|
| `magic` | u32 = `"RRC"` | 4B crus | `Capture/rsx_replay.h:13,91` / `RSXThread.cpp:73` |
| `version` | u32 = `0x6` | 4B crus | `rsx_replay.h:14,92` / `RSXThread.cpp:73` |
| `LE_format` | u32 (bool) | 4B crus | `rsx_replay.h:93` / `RSXThread.cpp:73` |
| `tile_map` | `unordered_map<tile_state(432B, bitwise), u64>` | VLE(count) + count×(432B chave + 8B valor) | `rsx_replay.h:65-71,129` |
| `memory_map` | `unordered_map<memory_block(16B, bitwise), u64>` | VLE(count) + count×(16B chave + 8B valor) | `rsx_replay.h:25-32,132` |
| `memory_data_map` | `unordered_map<memory_block_data{vector<u8>}, u64>` | VLE(count) + count×(VLE(len)+len B chave + 8B valor) | `rsx_replay.h:19-22,135` / `RSXThread.cpp:84-87` |
| `display_buffers_map` | `unordered_map<display_buffers_state(132B, bitwise), u64>` | VLE(count) + count×(132B chave + 8B valor) | `rsx_replay.h:73-89,138` |
| `replay_commands` | `vector<replay_command>` | VLE(count) + count elementos (ver abaixo) | `rsx_replay.h:34-40,141` / `RSXThread.cpp:90-93` |
| `reg_state` | `rsx::rsx_state` | tamanho fixo, **sem** prefixo de tamanho (ver abaixo) | `rsx_methods.h:78` / `RSXThread.cpp:53-65` |

Cada elemento de `replay_commands` (não é bitwise — sem `ENABLE_BITWISE_SERIALIZATION`,
logo cada campo serializa por si): `rsx_command:pair<u32,u32>` (8B crus, **pair/tuple nunca
leva prefixo VLE** — tamanho é estático) + `memory_state:unordered_set<u64>` (VLE(n)+8n) +
`tile_state:u64` (8B) + `display_buffer_state:u64` (8B).

`reg_state` = `transform_program:array<u32,544*4>` (8704B crus) **+ opcionalmente**
`transform_constants:array<u32[4],512>` (8192B crus) **+** `registers:array<u32,0x10000/4>`
(65536B crus) — `RSXThread.cpp:53-64`. A inclusão de `transform_constants` é condicionada
por `GET_SERIALIZATION_VERSION(global_version) || is_savestate_capture` (`RSXThread.cpp:57`)
— ver o veredicto na secção D para como resolver isto sem precisar de uma amostra real.

**Regras do wire format** (lidas por completo em `serialization.hpp`): containers
dinâmicos (`vector`, `string`, `unordered_map`/`set`) levam um **prefixo de tamanho VLE
(estilo LEB128, 7 bits por byte, bit alto = "continua")** — confirmado directamente no
código (`serialize_vle`/`deserialize_vle`): grava `value & 0x7F` por byte com o bit 0x80
ligado enquanto sobrar valor, standard LEB128 unsigned. Containers de tamanho fixo
(`std::array`, arrays C, `pair`/`tuple`) **não levam prefixo** — o tamanho é estático e já
conhecido de ambos os lados. Escalares/enums/estruturas `ENABLE_BITWISE_SERIALIZATION`
são copiados como bytes crus host-native (`memcpy`). **Não existe nenhuma lógica de
byteswap neste ficheiro** — `LE_format` é só um guarda, verificado uma vez no load
(`Emu/System.cpp`, por volta da linha 900-910: recusa carregar se `LE_format` não bater
com o host), nunca usado para converter. Os mapas não-ordenados serializam como uma
**lista plana de pares (chave,valor) na ordem de iteração no momento da escrita** — o
`bitwise_hasher` customizado só afecta a deduplicação em memória durante a captura; quem
lê o ficheiro nunca precisa reproduzir o hash do RPCS3, só ler `count` pares sequenciais.

`memory_indexer` (`rsx_replay.h:147`) **não é serializado** — é só um contador usado em
tempo de captura; os valores dos mapas são chaves u64 opacas, basta construir um dicionário
`{valor: chave}` por mapa e resolver as referências de `replay_command` contra ele.

### B. Layout de bytes do nosso `.rxs` (RXS1 v2)

Derivado de `ps3recomp/libs/video/tests/replay_main.c:77-153` (leitor) e
`ps3recomp/libs/video/tests/gen_synthetic_rxs.py:104-178` (escritor sintético — via
`struct.pack`, byte-exacto contra o leitor):

| Campo | Tipo / tamanho | Citação (.c leitor / .py escritor) |
|---|---|---|
| magic | `"RXS1"` (4B) | `replay_main.c:102` / `gen_synthetic_rxs.py:155` |
| version | u32 = 2 | `replay_main.c:102` / `:156` |
| n_blocks, n_records, reg_words, vp_words, disp_w, disp_h | 6×u32 | `replay_main.c:101-112` / `:157-162` |
| disp_count | u32 | `replay_main.c:113` / `:165` |
| disp[8] | 8×`{w,h,pitch,offset}` (16B cada = 128B, sempre 8, resto a zero) | `replay_main.c:81-83,114` / `:166-168` |
| regs[reg_words] | u32[] | `replay_main.c:120,123` / `:170` |
| vp[vp_words] | u32[] | `replay_main.c:121,124` / `:170` |
| blocks[n_blocks] | `{location,offset,size,data_off}` (16B cada) | `replay_main.c:77-79,125` / `:172-173` |
| data[] | bytes crus, tamanho = max(block.data_off+size) | `replay_main.c:130-137` / `:175` — **ordem de bytes do guest preservada tal e qual** (o escritor usa `struct.pack('>f', ...)`, big-endian, `gen_synthetic_rxs.py:84-85,105`) |
| records[n_records] | n_records×(u32,u32) | `replay_main.c:136-138` / `:177-178` |

Semântica do consumidor (`replay_main.c` por volta de 2240-2251): faz
`rsx_dispatch_seed_registers(regs)` + `seed_transform_program(vp)` uma vez, depois percorre
`records` sequencialmente — se `a & 0x80000000` (`APPLY_BLOCK_FLAG`), copia o bloco `b`
para a arena local/main (`guest_ptr`/`g_arena`); senão chama `rsx_dispatch_method(a, b)`.
Isto é funcionalmente o mesmo modelo do `.rrc`: "aplica os blocos de memória referenciados,
depois corre o comando".

### C. Mapeamento de campos `.rrc` → `.rxs`

| `.rrc` | → `.rxs` | Transformação necessária |
|---|---|---|
| `magic`/`version`/`LE_format` | (descartado) | Só valida e descarta — o RXS tem o seu próprio magic/version sem relação |
| `reg_state.registers` (65536B) | `regs[]`, `reg_words=16384` | **Cópia directa.** É exactamente para isto que `regs[]` + `rsx_dispatch_seed_registers` existem (seed do snapshot inicial) |
| `reg_state.transform_program` (8704B) | `vp[]`, `vp_words=2176` | Cópia directa — valores já são u32 host-native (palavras de FIFO decodificadas, não memória guest crua) |
| `reg_state.transform_constants` (8192B, se presente) | **sem campo actual — GAP** | `.rxs` não tem `constants[]`. Fix recomendado: sintetizar `records` que reproduzam as escritas de `M_VP_UPLOAD_CONST_ID`+`M_VP_UPLOAD_CONST` (já decodificadas por `rsx_dispatch.c:180-189`) em vez de mudar o formato do ficheiro |
| `memory_map`+`memory_data_map`+`memory_state` por comando | `blocks[]`+`data[]`+ registos `APPLY_BLOCK_FLAG` intercalados | Decodificar+reformatar: deduplicar por chave (o `.rrc` já faz isto) → `rxs_block`; emitir o registo de "aplica bloco" no mesmo ponto do stream de comandos onde o `.rrc` o associa. **Sem conversão de byte order necessária** — os dois lados guardam bytes crus big-endian do guest tal e qual (confirmado nos dois sentidos, secções A e B) |
| `replay_commands[].tile_state`/`display_buffer_state` | só o header estático `disp[8]`; tile_state = **GAP** | `.rxs` só tem uma tabela de display estática no header, não a versão por-comando com hash do `.rrc`; tiling/zcull não tem campo nenhum no `.rxs` — o exportador devia pelo menos avisar em `tile_state` não-default em vez de descartar silenciosamente |
| `replay_commands[].rsx_command` (par do header FIFO) | `records[]` (method,arg) | **Precisa de um algoritmo, não uma cópia:** decodificar o header clássico do PFIFO (campo de contagem `(first>>18)&0x7ff`, `Capture/rsx_replay.cpp:23,85`) para expandir em pares (method,arg) planos — o próprio `alloc_write_fifo` do RPCS3 (`rsx_replay.cpp:59-102`) faz exactamente isto. Sobrepõe-se ao achado da Área 2 sobre o nosso próprio consumidor de FIFO precisar da mesma decodificação |

### D. Veredicto

**Sim — spec suficiente para implementar `rrc_export.py` de ponta a ponta só a partir do
código-fonte.** O envelope é gzip padrão (zero adivinhação, descodificável com stdlib);
cada regra de wire format de cada container (VLE nos dinâmicos vs. sem prefixo nos fixos,
bitcopy cru nos PODs, mapas não-ordenados como lista plana sem precisar reproduzir hash)
está confirmada directamente em `serialization.hpp`, sem lacunas. O passo
`replay_commands` → `records` é trabalho algorítmico real (~20 linhas) mas totalmente
especificado, não é adivinhação.

**Única incógnita real que sobra:** se `transform_constants` (8192B) está mesmo presente
numa captura actual — condicionado por `GET_SERIALIZATION_VERSION(global_version)`
(`RSXThread.cpp:57`). Isto **não precisa de uma captura real para se resolver**: como todo
campo antes de `reg_state` é auto-delimitado (prefixos VLE), o `rrc_export.py` pode fazer
parse de tudo até `reg_state` e depois checar os bytes que sobram até EOF contra as duas
hipóteses (74240B sem constants vs. 82432B com constants — aritmética: `8704+65536=74240`;
`+8192=82432`) — determinístico, sem precisar de amostra ao vivo. Todo o resto desta spec é
derivável do código-fonte; uma amostra `.rrc.gz` real só serviria para uma checagem de
sanidade, não para resolver nenhuma ambiguidade que sobra.

---

## Área 2 — fidelidade do decode NV4097 (`rsx_dispatch.c` + consumidores)

**Nota de escopo:** o decode de bitfields/offsets em `rsx_dispatch.c` em si está sólido —
todo registo e bitfield verificado bate exactamente com o RPCS3. Os bugs reais de maior
impacto ficam nos **consumidores** do estado que o dispatcher decodifica
(`rsx_live_draw.c`, `rsx_d3d12_backend.c`), que o pedido original já incluía no escopo
("correctness of the model-demo render path"). Os 3 achados P1 abaixo foram reverificados
por leitura directa do código nosso antes desta nota — não são só o relatório do sub-agente.

| Prioridade | Nosso file:line | RPCS3 file:line | Divergência | Fix clean-room | Impacto |
|---|---|---|---|---|---|
| **P1** | `rsx_live_draw.c:817-839` (`fetch_attr`) — confirmado por leitura directa: só os ramos `RSX_VTX_TYPE_FLOAT` e `RSX_VTX_TYPE_UNORM8` decodificam correctamente; SNORM16/HALF/SINT16/CMP32/UINT8 caem no `else` (linhas 833-836) que reinterpreta 4 bytes crus como float BE, incluindo avançar `q = p + c*4` (errado para tipos de 1-2 bytes/componente) | `gcm_enums.h:1220-1229` (`vertex_base_type`); `RSXThread.cpp:457-499` (`get_vertex_type_size_on_host`) | Só 2 dos 7 tipos de vértice são decodificados correctamente; os outros 5 lêem o offset errado e reinterpretam como float | Reescrever `fetch_attr()` com switch em `a.type`, usando `rsx_vertex_type_size()` (já existe e está correcto em `rsx_vertex_formats.h:28-40`) para o tamanho por componente, e decodificar o layout real de cada tipo (u16 BE sign/unorm para S1/S32K, unpack de half-float BE para SF, dword packed 11-11-10 BE para CMP, byte cru sem swap para UB/UB256). O fallback de stride (`a.stride ? a.stride : a.size*4`, linha 822) também assume 4 bytes/componente sempre — errado para tipos não-F quando stride não é explícito | Qualquer malha com atributo não-float (normais/UVs comprimidos, muito comum) rende geometria lixo/NaN na via `rsx_live_draw.c` |
| **P1** | `rsx_live_draw.c:900-916` (`sink_end`, `case PRIM_TRIANGLE_STRIP`) — confirmado: `tri[i*3+0..2] = dc.verts[i], [i+1], [i+2]` para **todo** i, sem alternância | `Common/BufferUtils.cpp:575-593` (`is_primitive_native`: `triangle_strip` → `true`, ou seja, o RPCS3 submete strip nativa à GPU, que alterna o winding por especificação) | Toda malha em triangle strip sai com o winding do triângulo ímpar invertido | Alternar para `(v[i+1], v[i], v[i+2])` nos i ímpares, ou (como o RPCS3) não expandir de todo — submeter `D3D_TOPOLOGY_TRIANGLESTRIP` nativamente, que `rsx_primitives.h:35` (`rsx_to_d3d12_topology`) já devolve para este primitivo mas que `rsx_live_draw.c` nunca usa | Com backface culling ligado (default em conteúdo 3D real), ~metade dos triângulos de qualquer malha em strip é cortada ou aparece de dentro para fora — strips são o primitivo default da maioria dos meshes de personagem/ambiente |
| **P1** | `rsx_d3d12_backend.c` (`d3d12_draw_arrays`/`d3d12_draw_indexed`, por volta de 1774-1826) + `rsx_primitives.h:28-43` — confirmado por leitura directa dos dois ficheiros | `Common/BufferUtils.cpp:575-593` (`is_primitive_native`: quads/triangle_fan/polygon/line_loop = `false`), `:612-682` (geração de índices: quads → 2 triângulos por quad; fan → leque a partir do vértice 0) | `rsx_to_d3d12_topology()` mapeia QUADS/QUAD_STRIP/TRIANGLE_FAN para `D3D_TOPOLOGY_TRIANGLELIST` (**não** UNDEFINED) — confirmado a olhar a função: só o `default:` devolve UNDEFINED. Logo o guard `if (topo == D3D_TOPOLOGY_UNDEFINED) { skip; }` no backend **nunca dispara** para estes primitivos, apesar do comentário ao lado dizer "skipping ... quads, line loops, triangle fans". O intervalo de vértices crus é enviado como se já fossem triângulos consecutivos, sem gerar os índices certos. Os helpers correctos (`rsx_convert_quads`/`rsx_convert_triangle_fan`, `rsx_primitives.h:58-94`) — verificados: produzem o mesmo conjunto/winding de triângulos que o RPCS3 e que a expansão inline (também correcta) já usada em `rsx_live_draw.c:917-933` — têm **zero chamadores** em todo o `libs/` (confirmado por grep) | Chamar `rsx_prim_needs_conversion()` + `rsx_convert_quads()`/`rsx_convert_triangle_fan()` a partir de `d3d12_draw_arrays`/`d3d12_draw_indexed` para construir um index buffer real antes do upload, em vez de confiar no comentário (enganoso, hoje morto) sobre "saltar" esses primitivos | Qualquer draw do GoW2 com QUADS ou TRIANGLE_FAN (UI, partículas, fans simples) sai garbled no backend D3D12 |
| P2 | `rsx_dispatch.c:244-274` (`rsx_dsp_get_surface`) + `rsx_dispatch.h:137-149` | `rsx_decode.h:3323-3368` (`registers_decoder<NV4097_SET_SURFACE_FORMAT>`: `antialias` nos bits [12:16), `log2width`/`log2height` em [16:24)/[24:32)) | O próprio comentário do nosso ficheiro (`rsx_dispatch.c:13`) já documenta `aa[12:15]`, mas `rsx_dsp_get_surface()` só extrai os bits [0:11] (color/depth/raster type) — `antialias`, `log2_width`, `log2_height` não existem em `rsx_dsp_surface` | Adicionar `antialias`, `log2_width`, `log2_height` a `rsx_dsp_surface` e decodificar os bits [12:16)/[16:24)/[24:32) no getter | Condicional — só importa em render targets AA ou swizzled (`raster_type=2`); não foi traçado ao vivo se o caminho actual do GoW2 usa isto |
| P3 | `rsx_dispatch.c:186` (`if (slot < RSX_DSP_NUM_CONSTANTS)`, ou seja permite 0-511) — confirmado | `NV47/HW/nv4097.cpp:74` (`limit = 468 * 4`) — confirmado por leitura directa | RPCS3 limita os slots de constante de transform usáveis a [0,467] com aviso + skip de FIFO em overflow; nós aceitamos o array `transform_constants` inteiro [0,511] sem clamp/aviso | Adicionar clamp a 468 + log/skip em overflow, replicando o intervalo real utilizável do hardware | Baixo — só dispara se um jogo (por bug ou de propósito) escrever nos slots 468-511 |

**Também verificado, sem divergência** (checked, matches — citações resumidas):
- `rsx_dispatch.c:180-189` (janela `M_VP_UPLOAD_CONST`, load pointer "não avança") vs.
  `NV47/HW/nv4097.cpp:23-30,570-573` + `rsx_methods.h:1243-1245`: confirmado em profundidade
  que `transform_constant_load()` nunca é auto-incrementado — a assimetria com o ponteiro
  de instrução VP (que avança) é comportamento real de hardware, não bug.
- `rsx_dispatch.c:299-302` (VTXFMT: type&7, size 4b, stride 8b, freq 16b) vs.
  `rsx_decode.h:4413-4446` (`bf_decoder<0,3>`/`<4,4>`/`<8,8>`/`<16,16>`): larguras/posições
  batem exactamente, incluindo confirmar que o campo `type` é mesmo só 3 bits.
- `rsx_dispatch.c:379-384` (formato/localização de índice) vs. `rsx_decode.h:3416-3438` +
  `gcm_enums.h:112-113` (`TYPE_32=0`, `TYPE_16=1`): posição/largura dos bits batem; o nosso
  "qualquer byte não-zero = u16" é mais permissivo que o enum real de 2 valores, mas
  inofensivo — o próprio RPCS3 não valida além de um `static_cast` directo.
- `rsx_dispatch.h:64-69` (bits de `CLEAR_BUFFERS`) vs. `gcm_enums.h:1126-1132`
  (`CELL_GCM_CLEAR_Z=1<<0, S=1<<1, R=1<<4, G=1<<5, B=1<<6, A=1<<7`): match exacto.
- `rsx_dispatch.c:210-220` (contagem `(arg>>24)+1` de DRAW_ARRAYS/DRAW_INDEX_ARRAY) vs.
  `rsx_decode.h:4325-4377`: fórmula de start/count bate exactamente.
- `rsx_dispatch.h:33` (`RSX_DSP_VP_INSTR = 544`) + varrimento amplo de offsets
  (RT_FORMAT, VIEWPORT_HORIZ/VERT/TRANSLATE/SCALE, VTXBUF_OFFSET, VTXFMT,
  VERTEX_BEGIN_END, CLEAR_DEPTH/COLOR/BUFFERS, VP_UPLOAD_FROM_ID, IDXBUF_OFFSET/FORMAT,
  VB_INDEX_BATCH) vs. `Program/program_util.h:10` (`max_vertex_program_instructions=544`)
  + os enums `NV4097_*` correspondentes em `gcm_enums.h`: 544 é o valor real de hardware
  (não uma folga conservadora nossa), e todo offset do varrimento bate. Só diferem nomes
  cosméticos (ex.: RPCS3 chama "VIEWPORT_OFFSET" ao que nós chamamos "VIEWPORT_TRANSLATE").
- `cellVdec.h` (CELL_VDEC_MSG_TYPE_AUDONE=0, PICOUT=1, SEQDONE=2, ERROR=3): confirmado
  byte-idêntico ao enum do RPCS3 (ver Área 3).

**Questão em aberto, não confirmada como bug:** a expansão do campo "count" do header FIFO
(um pacote NV4097 pode carregar N argumentos consecutivos para o mesmo método, cada um
devendo virar uma chamada separada a `rsx_dispatch_method`) não foi localizada no nosso
código em tempo disponível — `cellGcmSys.c:539` só mostra parsing de jump-opcode, sem
expansão de count/bit NI visível. Os únicos chamadores actuais de `rsx_dispatch_method` são
o harness de replay offline (comandos `.rxs` já achatados) e uma `rsx_live_draw_method`
hoje não invocada — portanto isto **não é um bug provado**, é uma pergunta de design em
aberto para quando um consumidor de FIFO ao vivo for ligado.

---

## Área 3 — semântica `cellVdec` FORCE/callback (mais leve, 3 achados)

| Prioridade | Nosso file:line | RPCS3 file:line | Divergência | Fix clean-room | Impacto |
|---|---|---|---|---|---|
| P2 | `cellVdec.c:447-463` (watchdog `vdec_force_seqdone`) + `:480-497` (check inline em `vdec_driver`) | `cellVdec.cpp` por volta de 1557-1586 (`cellVdecEndSeq`: valida estado, empilha `end_sequence` na fila da thread de decode, devolve `CELL_OK` já) + por volta de 370-389 (`case vdec_cmd_type::end_sequence`: só aqui é que `CELL_VDEC_MSG_TYPE_SEQDONE` é disparado, de forma assíncrona) — **confirmado por leitura directa das duas zonas** | Em hardware real / RPCS3, `SEQDONE` é **estritamente** o resultado assíncrono do guest chamar `cellVdecEndSeq()` — não há nenhum caminho onde o decoder o dispare sozinho (nem por EOF, nem por timer). O nosso watchdog `PS3_VDEC_FORCE_SEQDONE_MS` sintetiza-o a partir de tempo de relógio decorrido, sem chamada do guest — não há nenhum "timer mais fiel" do RPCS3 para copiar, porque o RPCS3 nunca precisa de um. Também dispara `SEQDONE` de forma **síncrona dentro do próprio `cellVdecEndSeq`** (`cellVdec.c:783`), diferente do modelo assíncrono do RPCS3 (thread de decode separada) | Como o RPCS3 não oferece um timer mais fiel para copiar, não vale a pena tentar melhorar o watchdog em si — isso é um beco sem saída. Manter `PS3_VDEC_FORCE_SEQDONE_MS` claramente rotulado como escape-hatch de bring-up (já é, pelos próprios comentários) e priorizar o fix real a montante: seja lá o que devia estar a fazer o player do filme chamar `cellVdecEndSeq()` sozinho. Isso fica fora de `cellVdec.c` — é a FSM/loop de alimentação de AU do movie, não este ficheiro | Não é bug activo hoje (a intro já toca pelo caminho natural de EOF, ver linha seguinte) — é uma nota de fidelidade para não se tentar "consertar" o watchdog no sítio errado |
| P3 | `cellVdec.c:537-551` (caminho natural de EOF do pipe ffmpeg, também sintetiza `SEQDONE` via o mesmo token `seqDoneSent`) vs. o watchdog por timer acima | (mesmas citações RPCS3 acima — nenhum dos dois caminhos existe em hardware real) | As nossas duas fontes automáticas de `SEQDONE` (EOF e timer) são igualmente pouco fiéis ao oráculo, mas a de EOF pelo menos está ligada a progresso real de decode (esgotamento do stream cacheado), não a tempo arbitrário — o que bate com a observação do pedido original de que "o watchdog FORCE nunca dispara porque o filme fecha sozinho": o caminho de EOF já está a ganhar a corrida na prática | Tratar o `SEQDONE` disparado por EOF como o sinal primário de "fim sintético", e o watchdog de relógio como último recurso só para streams que nunca chegam a EOF (ex.: falha ao abrir o decoder) — já bate com o que `bd5e435` (`g_vdec_seqdone_fired`) fixou. Não precisa de mudança de código agora; só não vale a pena investir mais em "melhorar" o caminho do timer | Baixo — nota de limpeza/priorização, não é fix de correctness |
| P2 | `cellVdec.c:10-14` (comentário: AUDONE depois PICOUT "um por imagem de saída") + `:558-577` (`vdec_driver`: exactamente **um** PICOUT por `auPending` consumido, nunca zero, nunca mais de um) | `cellVdec.cpp:568-571` (`AUDONE` enviado incondicionalmente, uma vez por comando `au_decode`) depois por volta de 575-611 (`while (!decoded_frames.empty())` envia **0..N** PICOUT — um por frame que o codec realmente produziu para aquele AU, via `avcodec_receive_frame`) | O RPCS3 mostra que a cardinalidade de PICOUT **não** é 1:1 com AU: um decoder pode reter um AU sem saída imediata (latência de reordenação B-frame — `EAGAIN`, nada é emitido) ou, menos comum, libertar mais de um frame numa AU posterior. O nosso modelo fixa exactamente um PICOUT por AU alimentada. Estrutural também: o nosso AUDONE/PICOUT vêm de um loop de pacing próprio sobre ffmpeg a decodificar o **ficheiro de filme cacheado**, não os bytes de AU reais do guest — a suposição 1:1 é invenção nossa de pacing, não reflexo de tempo real de decode | Não é urgente mudar já, dado que o clipe da intro validado aparentemente não tem reordenação B-frame que dispare isto (empiricamente GREEN hoje). Se um filme futuro empancar depois de tocar parcialmente, este é o primeiro sítio a olhar: substituir o pareamento rígido 1:1 `auPending`→PICOUT por um modelo que tolera 0 ou >1 PICOUT por AU (ex.: uma fila de saída explícita, desacoplada do contador de AUs consumidas, espelhando o `out_queue` do RPCS3 que guarda `decoded_frames` à parte de `au_count`) | Fragilidade latente, não bug provado — bandeira para conteúdo de filme *novo* (codec/estrutura de GOP diferente), não para a intro já a funcionar |

**Também verificado, sem divergência:** os valores do enum `CellVdecMsgType` são
byte-idênticos (`AUDONE=0, PICOUT=1, SEQDONE=2, ERROR=3` — nosso `cellVdec.h:41-44`,
RPCS3 `cellVdec.h:33-36`) — confirmado por leitura directa dos dois lados.

---

## Fechamento

Ordem sugerida de ataque, por impacto nos objectivos activos (demo de modelo real,
render correcto, caminho do movie):

1. Os 3 P1 da Área 2 (`fetch_attr`, winding do triangle strip, expansão quad/fan morta) —
   bloqueiam render correcto de qualquer malha real assim que geometria começar a fluir,
   seja pelo caminho `rrc_export.py`→`.rxs`→replay, seja pelo caminho de draw ao vivo.
2. Implementar `rrc_export.py` usando a spec da Área 1 — está completa, não bloqueia em
   nenhuma incógnita que precise de captura real em mãos.
3. P2/P3 da Área 2 e Área 3 — condicionais ou de robustez, sem evidência de bug activo
   hoje; ficam no backlog para quando o conteúdo exercitado mudar (AA, GOP diferente).
