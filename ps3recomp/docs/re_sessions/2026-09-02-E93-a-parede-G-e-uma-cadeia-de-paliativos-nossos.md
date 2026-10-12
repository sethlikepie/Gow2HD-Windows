# E93–E97 — a parede [G] é uma cadeia de paliativos nossos (AUTO_LOAD, save-data, icallA do B71)

**Data:** 2026-09-02 · **Antecessor:** [E90](2026-09-01-E90-o-oraculo-destravado-e-o-fix-de-raiz-da-parede-F.md)
· Nível: **in-boot** (2–3 corridas por passo, controlo com o legado gated) + **oráculo RPCS3** (breakpoints/amostragem).

## 0. O que «preso em 0x0052F3F0» quer dizer

`app+0x460C` (o OPD do handler por frame) só é escrito **uma vez**, no arranque (`FUN_0024a7cc` ← `FUN_002b2e74`);
o console fica em `0x0052F3F0` para sempre (amostrado até 150 s de guest). O sinal de progresso não é esse ponteiro:
é o que o tick faz por baixo — e o console, depois do B71, faz **`thr_auto_load` → shaders (`GOWShader.ps3fx`, Resolve, Fog, …) →
R_Hero01 → R_ShellA → R_S_Olympus0 → R_SavePt → R_MCIcon → Rhod10 → …** (`oracle_wads.py`, TTY do RPCS3).

## 1. E93 — a thread AUTO_LOAD era stubada por nós, e o save-data mentia (✔ 2 corridas + controlo)

- `sys_ppu_thread.c` **stubava** a thread `AUTO_LOAD` por default («post-B71 stability»). A máquina de boot `FUN_000bb0b0`
  espera `+8 == 2` dessa thread → sem ela, o jogo nunca sai do boot. **Fix:** corre por default; `PS3_AUTO_LOAD_STUB=1` só para A/B.
- A thread chama `cellSaveDataAutoLoad2`. No console/RPCS3 o `funcStat` devolve `ERR_NODATA` (−4) e a API devolve
  **`CELL_SAVEDATA_ERROR_CBRESULT` (0x8002b401)** — o jogo trata isso como «sem save, segue». O nosso HLE mapeava
  `NODATA+isNew → CELL_OK`: o jogo acreditava ter carregado um save (`+0xc=0` → `FUN_000bae7c` aplica nada) e tomava o ramo errado.
  **Fix:** qualquer resultado negativo do `funcStat` → `CBRESULT` (= SDK/RPCS3, verificado no `cellSaveData.cpp`);
  legado só com `PS3_SAVEDATA_NODATA_OK=1`.
- Medido: `thr_auto_load() start → cellSaveDataAutoLoad2() : 0x8002b401 → thr_auto_load() end` igual ao console; o jogo
  passa a pedir **`/wad/r_hero01.wad_ps3`** (que nunca tinha pedido).

## 2. E94 — os WADs a seguir não existiam na nossa cache

O console lê tudo do `gow2.psarc`; a nossa `movie_cache/` só tinha R_LglScA/R_PermA (extraídos à mão em Julho).
Extraídos com `tools/psarc_extract.py` (ordem do console): r_hero01, r_shella, r_s_olympus0, r_savept, r_mcicon, rhod10,
r_rhsold00, r_colsus00, r_m_earth0, r_m_medusa0, r_m_wind0, r_m_lghtn2 (dados de jogo, fora de qualquer repo).

## 3. E95/E96 — o hero carregava, mas com um filho a menos: o icallA do B71 (✔ 2 corridas + controlo)

Com o hero presente o loop parava num ciclo infinito em `func_0039D51C` (lookup `vt[0x50]` de `TABLE[4]` a devolver `0x47AB4400`).
Cadeia medida (sondas E95 `[REG]`/`[DEFLIST]`/`[DEFLOOP]` + oráculo `oracle_hero_reg.py`/`oracle_defloop.py`):

1. a raiz do R_Hero01 recebe 12 filhos default no console (`3 4 20 15 17 9 23 21 16 32 25 18`); em nós 11 — falta a classe **4** (SCRX);
2. a lista de descritores tinha 12 nós nos dois lados, mas o 2.º nó apontava para **`0x47AB4400`** em nós;
3. `0x47AB4400` é um **shell host**: `[FACTORY] freelist REPLENISH fo=0x400D6808 … shell=0x47AB4400`, plantado no **B71 #058**
   pelo bloco de Julho (`patch_b71_cb56c_reuse_block.py`: `ps3_factory_repair_vt` + `ps3_factory_freelist_replenish(T4)` +
   «reuse product» + skip do attach). O registo SCRX do hero sacou esse shell da freelist (OPD em `0x90090130` → lixo).
4. No console a freelist vazia cresce naturalmente (`func_002550C8`, fixado no megaprojecto) e o construct do #058 devolve
   um produto real.

**Fix (E96, `patch_e96_b71_natural_icall.py`):** os três hacks ficam sob `PS3_B71_LEGACY_ICALL=1` (OFF). Medido: sem REPLENISH,
`icallA product r3=0x407C4E30 reused=0`, hero com 12 filhos (`DEFLOOP` nó 2 = `0x414FEF10 cls=4`), sem ciclo em `func_0039D51C`.

## 4. E97–E99 — o «HLE-lite» do 393E0 deixava `*(X+0x1FD8)` a zero (✔ 2 corridas + controlo)

Depois do hero completo a thread principal (sample do host: `main → func_00242C94 → tick → func_000B951C → func_000B8368 →
func_0003209C → func_002A466C`) girava em `func_002A466C(*(X+0x1FD8))` com o argumento **0** — a lista `obj+0x70` percorrida
a partir do endereço `0x70`. No console `*(X+0x1FD8)` = `0x30669020` (lista vazia e circular). Os únicos escritores de `+0x1FD8`
são `func_00034A60` e **`func_000393E0`** — e o bloco de Julho substituía o `func_000393E0` do B71 #055 por um «HLE-lite»
(carimba a vtable e zera três campos). A lista global `*(0x53EA54)` (E97) está bem formada nos dois lados — não era ela.

**Fix (E99, `patch_e99_b71_full_393e0.py`):** o construtor completo corre por default (`PS3_B71_LITE_393E0=1` repõe o lite).
Medido: o B71 passa pelo 393E0 completo; `[LIST70] obj=0x40668E70` (lista vazia/circular, como no console); controlo (lite)
→ `obj=0`. O próximo ponto de paragem já é dentro do caminho do console (`func_002B25AC → func_000B765C → func_00042294`,
a mesma cadeia que no console abre o R_ShellA): um search de lista `func_002DACCC` sob `func_002DAF04` (E100, em curso).

## 5. E100–E103 — depois do 393E0 completo: um filho de classe 15 que não existe (em curso)

Com o E99 a thread principal entra no caminho do console (`func_002B25AC → func_000B765C → func_00042294`, o mesmo que abre o
R_ShellA) e pára num search de lista `func_002DACCC(head=0x1030/0x2410…, key=0x50)` — ponteiros minúsculos, i.e. um objecto nulo
mais deslocamentos. Cadeia (sample do host + sondas E101/E102): `func_0009EC0C(obj=0x41573620)` lê `parent=obj[1]=0x407C7670`
(w0=`0x40030001`, subtag 3) e vai buscar o filho de classe 15 em `parent+0xB8+idx*4` → **0** → `FUN_00283328(lixo)` →
`func_002DAF04` percorre listas a partir de 0x1030. A poda E6 (`PAREDE1-E6-SUBTAG-SKIP-FIX`, incondicional, de outra sessão) saltava o
dispatch desse mesmo objecto; desligá-la (E103, `PS3_E6_PRUNE=1` repõe) **não muda** o resultado — o parent continua sem filhos.
Pergunta em aberto (oráculo, `oracle_bp.py` em `0x283328`/`0x912A0`): que objecto o console passa aqui e quem é o seu parent.

