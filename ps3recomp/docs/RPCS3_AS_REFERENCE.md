# RPCS3 como referência de implementação (ps3recomp)

> Como usar o [RPCS3](https://github.com/RPCS3/rpcs3) para **acelerar** o
> recompilador estático — sem embutir o emulador no runtime e sem confundir
> “roda no RPCS3” com “está recompilado”.

**Repos (2026-07-20):** o GoW2 no ps3recomp já passa o wall M2 (lift `rA=0`) e
entra em GCM/intro; o muro activo no Mac é **media I/O / FIOS / open de wav**.
O RPCS3 é o melhor oráculo para esse tipo de contrato guest↔LV2/HLE.

Repositório de referência (leitura; não é submodule):

```text
https://github.com/RPCS3/rpcs3
```

Clone local opcional (sparse, só o que interessa à doc):

```bash
# fora do monorepo de jogo — não commitar
git clone --depth 1 --filter=blob:none --sparse \
  https://github.com/RPCS3/rpcs3.git ../_ref_rpcs3
cd ../_ref_rpcs3
git sparse-checkout set rpcs3/Emu/Cell/lv2 rpcs3/Emu/Cell/Modules rpcs3/Emu/RSX
```

---

## 1. O que o RPCS3 é (e não é) para nós

| | RPCS3 | ps3recomp |
|--|--------|-----------|
| Modelo | Emulação **dinâmica** (PPU/SPU interpretados ou recompilados **em runtime**) | **Recompilação estática** (PPC/SPU → C à priori) + HLE |
| Compatibilidade | Biblioteca larga | Um título (ou poucos) em profundidade |
| SPU | JIT/recomp maduro | Lift + kernel HLE + jobs (em evolução) |
| RSX | Vulkan + decode NV maduro | FIFO + backends; shaders/texturas do jogo ainda murados |
| Uso correcto | **Spec viva + oráculo de traces** | Produto nativo |

**Sim:** usar RPCS3 para números de syscall, NIDs, ordem de HLE, traces de FS/SPURS/RSX.  
**Não:** linkar RPCS3 dentro do `boot_gow2`, copiar código GPL sem política clara, ou “forjar” estado guest porque o emulador avança.

Licença RPCS3: **GPLv2**. Ler o código para entender comportamento é normal; **copiar código** para o ps3recomp exige decisão legal explícita da equipa (preferimos reimplementação clean-room guiada pela observação).

---

## 2. Mapa de pastas úteis no RPCS3

Caminhos relativos à raiz do repo `RPCS3/rpcs3`:

| Caminho | O que é | Quando abrir no ps3recomp |
|---------|---------|---------------------------|
| `rpcs3/Emu/Cell/lv2/lv2.cpp` | Tabela `BIND_SYSC(...)` — **números oficiais de sc** | Novo stub `lv2_syscall N`; validar 141=usleep, 128–133 events, etc. |
| `rpcs3/Emu/Cell/lv2/sys_*.cpp` | Implementação de cada syscall | Portar semântica (não timing 1:1) |
| `rpcs3/Emu/Cell/lv2/sys_fs.cpp` | Filesystem kernel | VFS, async-ish opens, erros CELL |
| `rpcs3/Emu/Cell/lv2/sys_event.cpp` | Event queues / ports | SPURS printf queue, receive/push |
| `rpcs3/Emu/Cell/lv2/sys_spu.cpp` | SPU thread groups | group create/start/join |
| `rpcs3/Emu/Cell/lv2/sys_rsx.cpp` | RSX ao nível lv2 | map, context, flip low-level |
| `rpcs3/Emu/Cell/Modules/cellSpurs*.cpp` | HLE SPURS (default RPCS3) | Kernel HLE, workloads, taskset |
| `rpcs3/Emu/Cell/Modules/cellFs.cpp` | HLE cellFs | Paths, open/read vs o vosso `ppu_fs` / movie_io |
| `rpcs3/Emu/Cell/Modules/cellGcmSys.cpp` | GCM | Comparar com `libs/video/cellGcmSys.c` |
| `rpcs3/Emu/Cell/Modules/cellVdec.cpp` | Video decode | Intro movies, SEQDONE, Open/StartSeq |
| `rpcs3/Emu/Cell/Modules/cellAudio*.cpp` | Áudio | Ports, mix, open stream |
| `rpcs3/Emu/Cell/Modules/cellPad.cpp` | Pad | Mapeamento botões |
| `rpcs3/Emu/Cell/Modules/cellSysmodule.cpp` | LoadModule IDs | Módulos UNKNOWN (ex. `0x3E`) |
| `rpcs3/Emu/Cell/SPUThread.*` / `SPUOpcodes.h` | ISA SPU | Lifter / helpers |
| `rpcs3/Emu/Cell/PPUOpcodes.h` / `PPUInterpreter.*` | ISA PPU | Bugs de lift (ex. rA=0 em indexadas) |
| `rpcs3/Emu/RSX/` | RSX/NV47, GCM enums, Program | FIFO, shaders, textures |
| `rpcs3/Emu/VFS.*` | Virtual FS do emulador | Ideias de path mapping (não copiar) |

Já usamos isto na prática:

- Números lv2 vs `docs/SYSCALLS.md` / `lv2_syscall_table.h` (evento-flag vs timer).
- NIDs em `docs/NID_SYSTEM.md` e `tools/nid_database.py`.
- Disasm SPU vs tabela em `docs/SPU_LIFTER.md`.
- SPURS HLE-first alinhado ao default do RPCS3 (`docs/SPURS_BRINGUP.md`).

---

## 3. Workflow padrão: oráculo RPCS3 ↔ `boot_gow2`

Use o **mesmo EBOOT** (mesmo dump) nos dois lados.

### 3.1 Configuração RPCS3 (sessão de RE)

1. Instalar build recente: https://rpcs3.net/ ou builds do GitHub Actions.
2. Settings → Log: elevar nível do que importa:
   - `sys_fs` / `cellFs` / `cell_fs`
   - `cellSpurs` / `sys_spu`
   - `cellGcmSys` / `RSX`
   - `cellVdec` / `cellAudio` / `cellPad`
   - `sys_ppu_thread` / `sys_event`
3. Opcional: File → Save log; ou recolher `RPCS3.log` da pasta de dados do emulador.
4. Correr só até o sintoma (ex.: logo da intro), não a campanha inteira.

### 3.2 Configuração ps3recomp (mesmo momento do guest)

```bash
cd gow2-recomp   # ou games/gow2
. ./env_gow2.sh
export PS3_NO_RSX=0          # ou 1 se só I/O
export PS3_TRACE_SPURS=1
export PS3_TRACE_SC=1        # se precisar de sc
# probes gated do port (quando existirem):
# PS3_TRACE_FS / PS3_TRACE_INTROSEQ / PS3_TRACE_AREAD / PS3_TRACE_HOSTINFL
./boot_gow2 EBOOT.ELF 2>&1 | tee /tmp/recomp_boot.log
```

### 3.3 Tabela de diff (preencher sempre)

| # | Evento guest (ordem) | RPCS3 faz | recomp faz | Gap |
|---|----------------------|-----------|------------|-----|
| 1 | … | … | … | missing open / wrong NID / never called |
| 2 | … | … | … | … |

**Regra:** um gap por vez. Não “consertar” três HLE porque o RPCS3 avançou o FSM.

### 3.4 Armadilha M2 (lição permanente)

O watchpoint “ninguém escreve `0x86E118`” era **verdadeiro** e a conclusão “falta SPURS” era **falsa**: o lift antigo escrevia noutro EA (`rA=0` mal modelado).

Antes de meses de HLE:

1. Confirmar no RPCS3 (ou no interpretador PPU) o **EA real** do store no mesmo PC.
2. Confirmar no C liftado o que `ppu_lifter` emitiu.
3. Só depois inventar SPURS/kernel.

Ver `gow2-recomp/notes/2026-07-20-wall-m2-vencido.md`.

---

## 4. Onde o RPCS3 ajuda por wall (cadeia actual)

| Wall / fase | Ajuda RPCS3? | Como |
|-------------|--------------|------|
| **W0 host** | Baixa | Só se precisar de ABI/docs de tipos |
| **W1 M2 / init** | Alta *se* for lift | Diff de stores no PC; senão HLE engana |
| **W6–W9 SPURS/SPU** | **Alta** | `cellSpurs*.cpp`, `sys_spu`, traces de task/workload |
| **W13–W15 intro/FIOS/media (actual Mac)** | **Muito alta** | Ordem de open, paths reais, cellFs vs FIOS, audio stream |
| **W17–W23 RSX** | **Alta** | `Emu/RSX`, `cellGcmSys`, dumps de programa NV |
| **Perf nativa** | Baixa | Objectivo oposto |

### Checklist mínimo — wall intro (open wav / st620)

Usar com o plano `docs/superpowers/plans/2026-07-20-macos-intro-audio-open-wall.md`.

| # | Pergunta | RPCS3 | recomp | Acção se gap |
|---|----------|-------|--------|--------------|
| 1 | Quantos opens até o logo? | contar log FS | `PS3_TRACE_FS` / cellFs | Instrumentar se cego |
| 2 | Path exacto de `SmLogo` / `smlogo`? | string no log | guest print / open | Normalizar case (já há movie_io CI) |
| 3 | Open passa por cellFs, FIOS, ou outro? | módulo no log | qual HLE é chamado | Ligar o path que falta |
| 4 | Handle grava-se em obj+0x630/634? | memória / debugger | `[MOVIEOBJ]` | Completar open contract |
| 5 | cellVdec Open/StartSeq acontece? | contagem | log cellVdec | Não forjar SEQDONE sem open |
| 6 | cellAudio / snd_stream open? | log | print `couldn't open file` | Resolver path → cache |
| 7 | Quando `st620` deixa de ser 1? | sampler / mem | sampler st620 | Só avançar com produtor real |
| 8 | Há job SPU entre open e play? | sys_spu / spurs | TRACE_SPURS | Wiring SPU se MISS |
| 9 | Erros CELL devolvidos | código de erro | gpr[3] no HLE | Alinhar errno |
| 10 | Ordem temporal | timestamps log | timestamps log | Reordenar HLE se race |

**Proibido** como “fix” só porque o RPCS3 avançou: escrever `st620` / `obj+0x744` / EOS a partir do host sem o jogo ter o done producer.

---

## 5. Como extrair números de syscall (lv2)

No RPCS3, a tabela em `lv2.cpp` regista por exemplo:

```text
BIND_SYSC(sys_event_queue_create),   //128
BIND_SYSC(sys_event_queue_receive),  //130
BIND_SYSC(sys_timer_usleep),         //141
```

No ps3recomp:

1. Procurar o mesmo nome em `runtime/syscalls/lv2_syscall_table.h`.
2. Se o guest loga `lv2_syscall N (stub)`, **não** confiar só em docs antigos (`SYSCALLS.md` já esteve desactualizado em ranges de event-flag).
3. Medir args com `PS3_TRACE_SC` (assinatura) + confrontar com `sys_*.cpp` no RPCS3.
4. Implementar o mínimo que desbloqueia; commit por syscall.

Exemplo já feito: sc **144** = `sys_time_get_timezone` (não event_flag_clear).

---

## 6. Como extrair NIDs e HLE de módulo

```bash
# No tree RPCS3 (Modules):
grep -R "REG_FUNC\|REG_FNID\|0x" rpcs3/Emu/Cell/Modules/cellFs.cpp | head
```

No ps3recomp:

1. `tools/nid_database.py` / gen HLE.
2. Se `unresolved NID 0x........` no boot: grep o hex no RPCS3 Modules.
3. Preferir **comportamento observado no jogo** + RPCS3; stub `CELL_OK` só se o jogo tolerar (e documentar).

Ver `docs/NID_SYSTEM.md`.

---

## 7. SPU / SPURS

| Objectivo | No RPCS3 | No ps3recomp |
|---------|----------|--------------|
| ISA / opcodes | `SPUOpcodes.h`, disasm | `tools/spu_disasm.py`, `SPU_LIFTER.md` |
| Diff de execução | log SPU + ELF standalone | traces do lifter §3 |
| SPURS HLE | `cellSpurs.cpp`, `cellSpursSpu.cpp` | `libs/spurs/*`, `spurs_kernel.*` |
| Task ABI | como o emulador enche `gpr[3]` (u128) | `spu_run_lifted_job_ex` |

Diff de traces SPU: ver secção “Tracing” em `docs/SPU_LIFTER.md` (já descreve normalizar `SPU.log` do RPCS3).

---

## 8. RSX

Quando o título emitir draws reais (`tex_binds` / `vp_load` > 0):

1. RPCS3: capturar ou logar programas de shader / state.
2. ps3recomp: `PS3_TRACE_RSX_SHADERS`, FIFO dump, frame dump.
3. Comparar decode NV40 com `Emu/RSX/Program` e enums em `gcm_enums`.

Não usar o renderer do RPCS3 como backend do recomp.

---

## 9. O que **não** fazer

| Anti-padrão | Porquê |
|-------------|--------|
| Forjar FSM (st620/EOS) porque o RPCS3 avança | Esconde o open/FIOS real; quebra fidelidade |
| Assumir HLE RPCS3 = patch pontual no recomp | M2: medição ≠ causa |
| Copiar ficheiros GPL para o runtime | Licença / manutenção |
| Usar só “joga no RPCS3” como aceite de port | Critério errado para estático |
| Igualar timing/thread schedule | Giant lock e threads host ≠ scheduler RPCS3 |

---

## 10. Ligação aos planos de implementação

| Plano | Uso de RPCS3 |
|-------|----------------|
| [wall-chain INDEX](superpowers/plans/2026-07-20-00-wall-chain-INDEX.md) | Método geral oráculo |
| [01 host rebaseline](superpowers/plans/2026-07-20-01-host-rebaseline-mac.md) | Pouco |
| [02 M2 / SPURS boot](superpowers/plans/2026-07-20-02-wall-m2-spurs-boot.md) | Validar EA/stores; SPURS só se AddWorkload real |
| [03 SPU jobs](superpowers/plans/2026-07-20-03-spu-jobs-protocol.md) | Diff SPU/SPURS/LFQueue |
| [04 content/WAD](superpowers/plans/2026-07-20-04-content-intro-wad.md) | FS/psarc/FIOS order |
| [05 RSX pixels](superpowers/plans/2026-07-20-05-rsx-shaders-pixels.md) | Shader/FIFO ground truth |
| [intro audio open](superpowers/plans/2026-07-20-macos-intro-audio-open-wall.md) | **Checklist §4** prioritário |
| [movie EOS FSM](superpowers/plans/2026-07-20-macos-movie-eos-fsm.md) | Quando st620 avança no emulador |
| [FIOS async read](superpowers/plans/2026-07-20-fios-async-read-movie-io-hle.md) | sys_fs / cellFs async vs HLE |

---

## 11. Ferramentas e comandos

### 11.1 Sessão oráculo automatizada (recomendado)

```bash
# No port GoW2 — corre o boot, gera a tabela de 10 linhas
cd /path/to/gow2-recomp
./oracle_intro_checklist.sh 30
# com log RPCS3 já gravado:
./oracle_intro_checklist.sh 30 /path/to/RPCS3.log

# Saída:
#   oracle_out/recomp_<ts>.log
#   oracle_out/checklist_<ts>.md   ← preencher coluna RPCS3
```

Procedimento completo: [`tools/oracle_diff_boot.md`](../tools/oracle_diff_boot.md).

Parser isolado (qualquer log do recomp):

```bash
cd /path/to/ps3recomp
python3 tools/oracle_parse_recomp_log.py /tmp/recomp.log -m
python3 tools/oracle_parse_recomp_log.py /tmp/recomp.log -o /tmp/checklist.md
```

### 11.2 Manual

```bash
# --- RPCS3 (humano) ---
# Instalar, Log Level alto nos módulos relevantes, mesmo EBOOT, gravar log

# --- ps3recomp / GoW2 ---
cd /path/to/gow2-recomp
. ./env_gow2.sh
export PS3_TRACE_SPURS=1
./boot_gow2 EBOOT.ELF 2>&1 | tee /tmp/recomp.log

# Syscall stub hunt
grep 'lv2_syscall\|\[SC\]' /tmp/recomp.log | sort | uniq -c | sort -rn | head

# NID hunt
grep 'unresolved NID' /tmp/recomp.log | sort | uniq -c

# No tree RPCS3 local
grep -n 'sys_timer_usleep\|BIND_SYSC' ../_ref_rpcs3/rpcs3/Emu/Cell/lv2/lv2.cpp | head
grep -R 'SmLogo\|smlogo\|snd_stream' ../_ref_rpcs3/rpcs3/Emu/Cell/Modules/ | head
```

---

## 12. Aceite de “usei bem o RPCS3”

Uma sessão de RE conta como bem feita se produzir **as três**:

1. Um **log RPCS3** (ou extract) com timestamp/ordem dos eventos do sintoma.
2. Um **log recomp** no mesmo trecho do boot.
3. Uma **tabela de gaps** (§3.3) com **uma** acção implementável (HLE, lift, VFS, probe).

Sem (3), foi só “jogar no emulador”.

---

## Ver também

- [ARCHITECTURE.md](ARCHITECTURE.md) — tabela RPCS3 vs ps3recomp  
- [FAQ.md](FAQ.md) — diferenças de produto  
- [GAME_PORTING_GUIDE.md](GAME_PORTING_GUIDE.md) — pre-analysis com RPCS3  
- [NID_SYSTEM.md](NID_SYSTEM.md) — NID a partir de Modules  
- [SPU_LIFTER.md](SPU_LIFTER.md) — diff de traces SPU  
- [SPURS_BRINGUP.md](SPURS_BRINGUP.md) — HLE-first como no RPCS3  
- [FEASIBILITY.md](../FEASIBILITY.md) — veredito de esforço  
