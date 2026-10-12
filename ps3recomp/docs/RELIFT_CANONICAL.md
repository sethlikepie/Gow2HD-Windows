# RELIFT_CANONICAL.md — a sequência canónica de re-lift (GoW2 / macOS)

> **Fonte única** da ordem em que um lift regenerado é produzido, verificado e
> aceite, depois do tooling do marco v1.3 (Fases 17–20: mid-asm, switch tables,
> weak overrides, oráculo).
> Origem da sequência: `docs/superpowers/plans/2026-08-02-milestone-xenon-tooling-e-re-ia.md` §9 (M5.1).

**Porque existe:** até à Fase 16, um re-lift limpo perdia correcções que só
viviam como texto injectado nos `ppu_recomp_*.cpp` (gitignored). Desde as Fases
17–19 há três mecanismos que **sobrevivem ao re-lift** — `[[midasm_hook]]`,
`[[switch_table]]` e `[[functions_override]]` + weak wrappers — e essas
correcções **não devem ser reaplicadas à mão**. Esta é a ordem que garante isso
sem inventar passos.

---

## 0. Antes de começar — onde se corre

São **dois checkouts**, e a distinção importa:

| Checkout | Papel | Contém |
|---|---|---|
| `ps3recomp/` (monorepo) | **motor**: `tools/`, `runtime/`, `libs/`, e a cópia versionada de `games/gow2/*` | `tools/ppu_lifter.py`, `games/gow2/verify_lift.sh`, `games/gow2/config/*.toml` |
| `../gow2-recomp/` (checkout de build) | **dados + build**: é aqui que se corre tudo | `EBOOT.ELF`, `functions.json`, `recomp_macos_v2/` (produção), `build_macos.sh`, `boot_gow2` |

**Corre-se a sequência a partir de `../gow2-recomp/`.** Motivo medido: o
`accept_relift.sh` resolve `PS3_ENGINE_ROOT="${PS3_ENGINE_ROOT:-$REPO/../ps3recomp}"`
(`games/gow2/accept_relift.sh:72`), e a partir do checkout de build esse default
aponta para o monorepo e encontra o `verify_lift.sh` do motor. A partir de
`ps3recomp/games/gow2/` o mesmo default resolve para
`games/gow2/../ps3recomp/games/gow2/verify_lift.sh`, que **não existe** — o script
sai `rc=2` com `sem a perna 2 nao ha aceite possivel`. Nesse caso é obrigatório
exportar `PS3_ENGINE_ROOT` à mão. *(Medido nesta fase, offline.)*

```bash
export PS3_ENGINE_ROOT=/caminho/para/ps3recomp   # só necessário fora do checkout de build
cd /caminho/para/gow2-recomp
```

**Ressalva medida:** `../gow2-recomp/verify_lift.sh` é uma cópia **anterior à
Fase 20** (sem o passo de oráculo). O caminho de aceite nunca a usa — o
`accept_relift.sh` chama sempre a cópia do motor via `PS3_ENGINE_ROOT`. Quem
correr `./verify_lift.sh` **directamente dentro do checkout de build** apanha a
versão antiga (3 passos, sem `VERIFY_ORACLE`). Prefira sempre
`"$PS3_ENGINE_ROOT/games/gow2/verify_lift.sh"`.

---

## 1. As oito etapas

| # | Etapa | Comando | Bloqueia? |
|---|---|---|---|
| 1 | `find_functions` / `functions.json` | só **se as fronteiras mudaram** | — |
| 2 | `ppu_lifter --config` → `LIFT_NEW` | `RELIFT=1 ./build_macos.sh LIFT_NEW` | sim |
| 3 | **(não)** reaplicar patches de correctness já migrados | — nada a correr | sim |
| 4 | `apply_all_patches.sh LIFT_NEW` | só PROBE / legado residual | sim |
| 5 | `verify_lift.sh LIFT_NEW` | delta vs `lift_baseline/*` | sim |
| 6 | `build_macos.sh LIFT_NEW` | (fundido no passo 2 sob `RELIFT=1`) | sim |
| 7 | smoke da intro 25–40 s | kill por **PID** | sim |
| 8 | `accept_relift.sh` / promoção | **só se 5–7 verdes** | sim |

### Etapa 1 — `functions.json` (condicional)

Só se as fronteiras de função mudaram (nova análise, reparação de bounds,
alvos mid-function novos). O `functions.json` do GoW2 tem adições curadas à
mão — **não o regenerar por reflexo**.

```bash
python3 "$PS3_ENGINE_ROOT/tools/find_functions.py" EBOOT.ELF --json -o functions.json
```

Se não mudou nada: saltar. O passo 5 (`audit_boundaries`) mede as fronteiras por
delta contra `lift_baseline/BOUNDS_BASELINE.json` e apanha regressão aqui.

### Etapa 2 — lift para um directório NOVO

```bash
cd /caminho/para/gow2-recomp
RELIFT=1 ./build_macos.sh ./recomp_macos_v3
```

`RELIFT=1` regenera o lift **e** compila/linka a seguir (funde as etapas 2 e 6).
Garantias já dentro do script:

- **nunca escreve em `recomp_macos_v2`** (produção) — aborta se o alvo resolver
  para lá (`build_macos.sh:58-62`);
- passa `--config config/gow2_recomp.toml` automaticamente quando o ficheiro
  existe (`build_macos.sh:82-88`). **Sem `--config` o lift sai sem os mid-asm
  hooks** e o marco inteiro deixa de estar no binário, em silêncio.

Forma explícita equivalente (se preferir separar lift de build):

```bash
python3 "$PS3_ENGINE_ROOT/tools/ppu_lifter.py" EBOOT.ELF \
    --functions functions.json \
    --config "$PS3_ENGINE_ROOT/games/gow2/config/gow2_recomp.toml" \
    -o ./recomp_macos_v3 -j 4
```

### Etapa 3 — o que **NÃO** se reaplica

As correcções já migradas para mecanismo vivem no TOML + código host
versionado. **Não as reinjectar à mão.** Estado medido do ledger
`games/gow2/lift_baseline/PATCH_MIGRATION.tsv` (140 linhas de dados):

| Patch | Mecanismo (`alvo`) | Estado |
|---|---|---|
| `patch_ce03c_introseq_block.py` | `midasm+weak` | `migrated` |
| `patch_ce03c_movie_done_reset.py` | `midasm` | `migrated` |
| `patch_b71_2f3f0_guard.py` | `weak` | `migrated` |
| `patch_fios_play_already_active.py` | `weak` | `migrated` |
| `patch_jumptable_2a209c.py` | `switch_table` | `redundant` (nunca foi preciso) |
| `patch_jumptable_2b11b8.py` | `switch_table` | `redundant` (nunca foi preciso) |

Isto **não** obriga a editar o `apply_all_patches.sh`: a Fase 19 mediu a escada
A/B/C sobre re-lifts completos e o degrau C (patch por cima de um lift que já
tem o mecanismo) dá `WEAK`/skip com o **`sha256` dos chunks idêntico** — sem
dupla injecção (`19-03-SUMMARY.md` §3).

**Honestidade sobre o passo 4 abaixo:** a formulação "só PROBE / LEGACY" é o
**estado-alvo**, não o de hoje. Medido agora: `134 todo`, `4 migrated`,
`2 redundant`; **72 patches de classe `CORRECTNESS` continuam activos** e
continuam a ser reaplicados. O que o marco v1.3 garante é que estes seis já não
dependem do `apply_all_patches.sh` para sobreviver.

### Etapa 4 — `apply_all_patches.sh` (PROBE / legado residual)

```bash
./apply_all_patches.sh ./recomp_macos_v3 --status /tmp/relift_status.tsv
```

**Não use o `rc` bruto como gate.** Esse `rc` conta também `UNVERIFIED` (dívida
conhecida: patches FUNCIONAIS sem contrato declarado). O critério é o TSV:
**zero `NO-MATCH`** e **zero `FAILED`/`FAILED-PARTIAL` fora da classe `PROBE`**
— exactamente o que o `accept_relift.sh` faz na perna 3
(`accept_relift.sh:142-152`).

Um `FAILED` **não se força**: pode significar que o patch ficou obsoleto para o
novo shape do lift. Investigar.

### Etapa 5 — `verify_lift.sh`

```bash
"$PS3_ENGINE_ROOT/games/gow2/verify_lift.sh" ./recomp_macos_v3 ; echo rc=$?
```

Três passos, todos por **delta contra baseline congelado** (nunca limiar
absoluto — limiar absoluto pune melhorias, D-4.5):

1. `lift_parity.py` vs `lift_baseline/PARITY_BASELINE.json`
2. `verify_lift_baseline.sh` + `manifest_delta_gate.py` vs `MANIFEST_DEBT_BASELINE.json`
3. `audit_boundaries.py` (I1/I3) vs `BOUNDS_BASELINE.json`

O `LIFT_DIR` tem de estar **ao lado do `EBOOT.ELF` e do `functions.json`** — o
script deriva `GOW2_ROOT="$(dirname "$LIFT")"` (`verify_lift.sh:70-72`).
Overrides: `PS3_ELF`, `PS3_FUNCTIONS`.

**Passo 4 opcional — o oráculo Ghidra:**

```bash
VERIFY_ORACLE=1 "$PS3_ENGINE_ROOT/games/gow2/verify_lift.sh" ./recomp_macos_v3
```

Opt-in por design (Fase 20). **Sem a variável o script corre exactamente como
antes** — os mesmos 3 passos, o mesmo `rc`, sem exigir Ghidra instalado. Isto
não é preferência: o script está no caminho de aceitação e mudar o default
alteraria o que o projecto aceita hoje. Um `ghidra_out/MANIFEST.json` ausente ou
de outro ELF falha **alto** (`absent` / `stale`), nunca passa em silêncio.

### Etapa 6 — build

Já feita pelo `RELIFT=1` da etapa 2. Se separou lift de build:

```bash
OUT=./boot_gow2_v3 FORCE_REBUILD_LIFT=1 ./build_macos.sh ./recomp_macos_v3
```

### Etapa 7 — smoke da intro (25–40 s)

```bash
cd /caminho/para/gow2-recomp
set -a; . ./env_gow2.sh; set +a
unset $(env | awk -F= '/^PS3_TRACE_/ {print $1}') 2>/dev/null || true
export PS3_NO_RSX=1 PS3_RSX_BACKEND=trace PS3_PERF_FSM=1 PS3_MOVIE_EOS=0
unset PS3_MOVIE_DONE_MS PS3_VDEC_FORCE_SEQDONE_MS

./boot_gow2_v3 EBOOT.ELF > /tmp/relift_smoke.log 2>&1 &
BPID=$!
sleep 25
kill -TERM "$BPID" 2>/dev/null; sleep 1; kill -9 "$BPID" 2>/dev/null

grep -E '\[MOVIEFSM\] st620|FATAL|OOB|0xFFFF' /tmp/relift_smoke.log | head -40
```

**Critérios:** `st620` máximo **≥ 3** no path baseline (baseline medido na Fase
16: `st620_max=11`, sequência `0→1→3→3→11→11`) e **zero** `OOB` / `0xFFFF*`
logo depois do TLS. O sentinela `4294967295` (`0xFFFFFFFF`) fica **fora** do
máximo.

> **Protocolo G6, sem excepções:** kill **por PID** (TERM, depois `-9`). **Nunca
> `pkill -f boot_gow2`** (já pôs a load a 75). Logs em `/tmp`, **fora dos dois
> repositórios**. Verificar órfãos no fim (`pgrep -f <tag>` deve dar 0).

**O smoke sozinho é necessário mas NUNCA suficiente.** A lição que criou o
`accept_relift.sh`: em 2026-07-26 um lift passou o smoke com `st620=11` em 6/6 —
melhor do que o critério exige — e foi promovido. Faltavam-lhe 36 conversões
OPD, 35 guardas `0x4F000000u` e 9 famílias de marcador. O smoke mede a intro até
`st620=11`; a perda estava no WADLD e no caminho de shaders, **depois** disso.

### Etapa 8 — aceite e (só depois) promoção

```bash
./accept_relift.sh ./recomp_macos_v3 6 /tmp
```

Quatro pernas + contadores, todas correm sempre (não aborta cedo), `rc=0` só se
todas passarem:

| Perna | O quê | Critério |
|---|---|---|
| 1 | `smoke_relift_equiv.sh` | `st620≥3` em ≥4 de 6 execuções reais |
| 2 | **`verify_lift.sh`** | `rc=0` nos 3 passos, por delta |
| 3 | `apply_all_patches.sh --status` | 0 `NO-MATCH`, 0 `FAILED` fora de `PROBE` |
| 4 | `smoke_chain_gate.sh` | cadeia de 5 elos (intro / 2.º movie / re-Play / AUTO_LOAD / WAD) |
| — | contadores (D-5.2) | `imp_modules` / `imp_imports` / `orfaos` bloqueantes, por delta |

**A promoção é um gate humano à parte.** `games/gow2/promote_lift.sh` não é
invocado por esta sequência nem pelo `accept_relift.sh`. Um `rc=0` de aceite
**autoriza** a promoção; não a executa.

---

## 2. A perna 2 já existia — evidência, não entrega nova

O elo "`verify_lift` no caminho de aceitação" **já estava de pé antes do marco
v1.3**. Registado aqui como facto verificado, não como coisa que a Fase 21
construiu:

| Onde | Linha | O quê |
|---|---|---|
| `games/gow2/accept_relift.sh` | `:72` | `PS3_ENGINE_ROOT="${PS3_ENGINE_ROOT:-$REPO/../ps3recomp}"` |
| `games/gow2/accept_relift.sh` | `:73` | `VERIFY_LIFT="$PS3_ENGINE_ROOT/games/gow2/verify_lift.sh"` |
| `games/gow2/accept_relift.sh` | `:74-77` | ausente/não-executável ⇒ `exit 2` (*"sem a perna 2 nao ha aceite possivel"*) |
| `games/gow2/accept_relift.sh` | `:129-130` | invocação real + captura de `leg2_rc` |
| `games/gow2/accept_relift.sh` | `:262` | `leg2_rc` entra no **AND** final que decide `rc=0` |

Reproduzir a evidência:

```bash
rg -n 'verify_lift\.sh|PERNA 2|leg2_rc' games/gow2/accept_relift.sh
bash -n games/gow2/accept_relift.sh          # rc=0
```

---

## 3. Realidades de produção que um re-lift tem de conhecer

**1. `emit_weak_wrappers = true` já está no TOML de produção.**
`games/gow2/config/gow2_recomp.toml:24`. Ligada no plano 19-02, no mesmo commit
que criou o override host. Consequência prática: **um `RELIFT=1` sobre a config
de produção já emite o par `__imp_` (forte) + wrapper fraco** — só para as EA
declaradas em `[[functions_override]]`, nunca global. Sem esta chave o lifter
emite símbolo forte e o override host dá `duplicate symbol` no link.

```bash
rg -n 'emit_weak_wrappers' games/gow2/config/gow2_recomp.toml
```

**2. O `switch_table_file` e o `gow2_recomp.toml` têm de chegar JUNTOS.**
`switch_table_file = "gow2_switch_tables.toml"` (`:23`) é resolvido **relativo
ao directório do `.toml`**. Com a chave activa a apontar para um ficheiro que
não existe, o `ppu_lifter.py` **aborta logo no arranque**:

```
Error: .../config/gow2_switch_tables.toml: switch table file not found
```

E não morre só a parte das switch tables — **morre o `--config` inteiro**,
levando os `[[midasm_hook]]` junto, em silêncio. Medido no 17-03 e confirmado na
Fase 18. Ao copiar a config para outro checkout ou para uma worktree, copiar
**os dois ficheiros**.

**3. `VERIFY_ORACLE=1` é opcional e continua opcional.** Ver etapa 5. O default
sem a variável dá `rc=0` contra o lift de produção (medido na Fase 16 e
reconfirmado na Fase 20).

**4. `promote_lift.sh` fica fora desta sequência.** Gate próprio, decisão humana.

---

## 4. O que **não** serve de critério

**`sticky_pub >= 1` não é verificável neste build.** O `CLAUDE.md` prescreve-o
como critério de não-regressão pós-merge, mas a Fase 16 mediu — e esta fase
reconfirmou — que `ps3_fios_sticky_publish` / `_peek` / `_consume`
(`runtime/ppu/ppu_loader.cpp:1370`, `:1383`, `:1393`) **não imprimem nada**:
zero `fprintf` com `sticky` em `runtime/` e `libs/`. Um `grep sticky` daria
sempre 0, e esse 0 **não** significa "sticky morto" — é uma métrica sem emissor,
não uma métrica a zero. Enquanto os `ps3_fios_sticky_*` não forem instrumentados
(item P1 do handoff), **não usar este grep como aceite**.

```bash
rg -n 'ps3_fios_sticky' runtime/ libs/     # 3 definições, 0 fprintf
```

---

## 5. Segurança e higiene

- **Nunca commitar dados de jogo:** `EBOOT.ELF`, `*.psarc`, `*.self`,
  `extracted/`, `movie_cache/`, `spu_images/`, `*.m2v`, `*.wad_ps3`.
- **Nunca commitar lifts** (`recomp*/*.cpp`, ~1.3 GB — regeneráveis) nem os
  exports do `ghidra_out/`.
- **Logs em `/tmp`**, fora dos dois repositórios (G6).
- **Nunca `git add -A` / `git add .`** — sempre paths explícitos.

---

## 6. Fora de âmbito desta sequência

Optimizações de ABI (`*_as_local`, `skip_lr`), SIMDe e recompilação de shaders
ficam **explicitamente fora** — cada uma com plano próprio. Não inventar etapas
para elas aqui. O handoff do marco v1.3 vive em
`games/gow2/notes/milestone-2026-08-xenon-re-results.md`.

---

## 7. Nível de evidência desta página

Regra 4 do `CLAUDE.md`: distinguir sempre o que foi exercitado do que foi
escrito.

| Item | Nível |
|---|---|
| Perna 2 (`accept_relift` → `verify_lift`) — file:line, `bash -n` | **medido offline** nesta fase |
| Resolução do `PS3_ENGINE_ROOT` nos dois checkouts | **medido offline** nesta fase |
| `emit_weak_wrappers = true` / `switch_table_file` no TOML de produção | **medido offline** nesta fase (`rg`) |
| Contagens do ledger (140 / 4 / 2 / 134 / 72 `CORRECTNESS`) | **medido offline** nesta fase |
| `sticky` sem emissor (0 `fprintf`) | **medido offline** nesta fase (Fase 16 + reconfirmação) |
| `verify_lift.sh` `rc=0` contra produção | **medido** nas Fases 16 e 20, **não** re-corrido aqui |
| Escada A/B/C (degrau C ⇒ `sha256` idêntico) | **medido** na Fase 19, **não** re-corrido aqui |
| `st620_max=11` no smoke da intro | **medido in-boot** na Fase 16, **não** re-corrido aqui |
| A sequência de 8 etapas **de ponta a ponta, numa só corrida** | **NÃO EXERCITADA** — cada etapa tem evidência própria das Fases 16–20, mas ninguém correu 1→8 seguido para produzir este documento |

---

## Referências

- `docs/superpowers/plans/2026-08-02-milestone-xenon-tooling-e-re-ia.md` §9 (M5.1) — origem da sequência
- `docs/RECOMP_CONFIG.md` — schema do TOML (`midasm_hook`, `switch_table`, `functions_override`)
- `games/gow2/accept_relift.sh` — as 4 pernas + contadores
- `games/gow2/verify_lift.sh` — os 3 passos + oráculo opt-in
- `games/gow2/lift_baseline/PATCH_MIGRATION.tsv` — ledger patch → mecanismo
- `CLAUDE.md` — regras absolutas (dados de jogo, G6, honestidade)
