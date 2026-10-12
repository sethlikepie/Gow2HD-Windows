# E147–E173 — o relógio de áudio: uma jump table do lifter com 1 case em vez de 8, e o mixer do SCREAM cujo import nunca resolvia

> Continuação directa de [E124–E139 §12](2026-09-02-E124-E139-fallthrough-condicional-inflate-e-rwlock.md): o player
> do filme corrige o timebase pelo tempo do stream de som, que no console avança 1:1 e em nós fica em 0 — logo o
> player fica na 1.ª imagem sem `PS3_MOVIE_EOS`. Este ledger anda essa corrupção para montante até ao produtor.
> Oráculo: RPCS3 (interpretador, GDB stub, **mudo e sem janela**, `EBOOT_intro.BIN`). Medições "nossas" são in-boot.

## §1 O que se refutou por medição antes de chegar ao produtor (E149–E156)

| # | hipótese | instrumento | resultado |
|---|---|---|---|
| E149 | a posição da voz do Scream é escrita mas não lida | sonda `[VOICE]` em `FUN_00453a68` (`PS3_TRACE_REG`) | tabelas `t300=0x008AF900` / `t200=0x00897900` nunca escritas; nenhum DMA PUT do SPU aponta para `0x0089xxxx/0x008Axxxx` |
| E153 | o update do stream (`FUN_00461fe8`) corre pelo caminho produtivo | sonda `[STRUPD]` | **só o caminho de reset** (`lr=0x004633D0`, `stream=0x00000000`, `+0x154=0`); no console o mesmo sítio recebe `r3=0x33402C28` e `+0x154` sobe (25 088 → 25 856, tid 0x1000012) |
| E154 | starvation: a thread principal (prio 1001) monopoliza o giant lock e as threads do áudio (`syn_tick_timer_thread` 100, `snd_stream_service_thread` 401) nunca correm | `sys_timer_usleep` readquire com prioridade (`PS3_USLEEP_PRIO`, `sys_ppu_thread_priority_of`) | mais throughput, **samples continuam 0** |
| E156 | o mixer SPU só é despachado 1×/s (no push da LFQueue) e por isso nunca mistura | tick de re-kick das tasks SPURS (`PS3_SPU_TASK_TICK_MS`, `spurs_task_tick_thread`) | jobs "returned cleanly" 250 → 5 800, **samples continuam 0** |

Os dois últimos ficam como melhorias de escalonamento medidas, não como fix: o defeito não era de escalonamento.
Sobrevive de todos eles a medição; cai a inferência "é o mixer SPU / é o cellAudio".

## §2 A corrupção, andada para montante com o verificador de callee-saved (E157–E161)

`patch_e128_csave_check.py` generalizado (nomes de função por argumento) embrulha cada chamada e compara
`r1/r14..r31` antes/depois. Cadeia: `func_00461fe8 ← func_00463368 ← func_00462a70 → func_0043e810 → func_0043D9B0`.
Em `func_0043e810` (frame `stdu −0x130`), ao regressar da chamada em `0x43EA40`:

```
[CSAVE] callee=func_0043D9B0 tgt=0x0043EA44 r31: 0x00000008 -> 0xFFFFB1E0
        r30: 0x00995B38 -> 0x00573A14   r28: 0x00000960 -> 0x00995B38
        r1:  0xD003EC10 -> 0xD003EB20    (−0xF0 = o frame de FUN_0043d9b0, stdu −0xF0)
```

É a assinatura do E129 (a função regressa ao chamador **sem passar pelo epílogo**): o `r1` fica com o frame do
callee por desfazer e os callee-saved do chamador são depois restaurados do frame errado. `FUN_0043d9b0` é o
serviço do stream de som do Scream — é ele que, no caminho produtivo, chega ao `FUN_00461fe8` com o stream real.

## §3 O produtor: uma `switch` de 8 cases liftada com 1 (E162)

`FUN_0043d9b0` tem três dispatchers gcc (`lwz r11,d(r2); lwzx; add; mtctr; bctr`) em `0x43E130`, `0x43E324`,
`0x43E38C`, com tabelas de offsets em `0x43E134`, `0x43E328`, `0x43E390` — **8 entradas cada** (lidas do ELF,
TOC `0x541178`). O limite do primeiro vive 300 instruções antes, no salto que **entra** no dispatcher:

```
0043DC5C  addi   r0, r31, 20007
0043DC60  cmplwi cr7, r0, 0x7
0043DC64  ble    cr7, 0x43E118        ; -> lwz r11,-0x55C(r2) ... bctr @0x43E130
```

`discover_jump_tables` procurava o limite no **último `cmp[l]wi` das 30 instruções antes do `bctr`**, sem parar em
saltos incondicionais. Nessa janela estava o bloco irmão `0x43E0F4: cmpwi cr7,r0,0 … 0x43E114: b 0x43DECC` →
`count=0` → **1 case** (`0x43E468`). Os outros sete índices caem no `default: ps3_indirect_tail(ctx)` →
`ppu_lookup(0x43E480)` falha (não é início de fragmento) → `ps3_indirect_call` regista e **regressa** → a função C
regressa sem trampolim → o chamador continua com `r1 − 0xF0`. In-boot, já nos logs do E159/E160:

```
[ICALL-BAD] ctr=0x0043E480 lr=0x0043EA44 r2=0x00541178 r3=0x00995B30 r11=0x0043E134   (×133)
[ppu] unresolved indirect call -> 0x0043E480
```

`r11=0x0043E134` é a própria tabela; `0x43E480` é a entrada 7 (o comando `r31 = −20000` do stream).

### População (ELF inteiro, mesmo `discover_jump_tables` antes/depois)

| dispatchers gcc | truncados | todos atravessavam um `b` | cases no lift |
|---|---|---|---|
| 157 | **8** | sim | 1 750 → 1 788 (1 782 após 2 overrides do config) |

| bctr | antes → depois | bctr | antes → depois |
|---|---|---|---|
| `0x27E24` | 1 → 5 | `0x35FEFC` | 1 → 7 |
| `0x30F38` | 1 → 4 | `0x35FFE0` | 1 → 5 |
| `0x63250` | 1 → 5 | `0x3751E8` | 1 → 4 |
| `0xE6F6C` | 2 → 9 | `0x43E130` | 1 → 8 |

Nenhum dos outros 149 dispatchers mudou. `0x43E324` tinha `cmpwi …, −20007` na janela (negativo → 256 → decode até
entrada inválida) e por isso já saía certo por acaso.

### Fix (`tools/ppu_lifter.py`, `discover_jump_tables`)

O limite passa a vir de duas fontes, ficando a maior (ler a mais é limitado pela validação in-text de cada entrada;
ler a menos perde cases vivos): (1) o `cmp[l]wi` imediatamente antes de um salto condicional **para dentro** do bloco
do dispatcher; (2) o `cmp[l]wi` mais próximo no caminho de queda, **parando no primeiro salto incondicional**.
Testes: `tools/test_ppu_jumptables.py` (+2 casos: limite no salto que entra; limite de queda continua a valer).
`tools/test_switch_tables.py` falha antes e depois — anotação `str | None` em Python 3.9, pré-existente.

## §4 Prova in-boot (e162: re-lift + `apply_all_patches.sh` + sondas E149/E153, `PS3_MOVIE_EOS=0`, 120 s)

| métrica | e161 (lift v2) | e162 (lift com o fix) |
|---|---|---|
| `[ICALL-BAD]` (todos / `ctr` em `0x43E4xx`) | 133 / 133 (nos logs E159–E160) | **0 / 0** |
| `[STRUPD]` com `stream ≠ 0` | 1 de N (só o reset; depois `stream=0`) | **30 de 30** (`stream=0x433FF680`, `+0x1c4` 0xFFFFFFFF → **0x18** = voz 24 atribuída, `+0x1b8=1`) |
| `[VOICE]` (voz 24, `t300=0x008AF900`) | — | 10 leituras: `+0x14` (posição) **= 0**, `t200+0x1c=0x4341A400` (buffer), `+0x24=131072` |
| `[SNDTIME] samples(+0x154)` | 0 | **0** (ainda) |
| `GetPicture` / `[movieio] open` / `CreateTask` | 1 / 1 / 9 | 1 / 1 / 9 (sem regressão) |

O produtor do E162 está corrigido e o serviço do stream chega ao caminho produtivo com a voz atribuída. O que
**ainda** não avança é a posição da voz na tabela do Scream (`t300+0x14`), que na consola é escrita pelo mixer SPU
(DMA de volta para `0x008AF900+v×0x300`). Em nós os jobs do mixer "returned cleanly" (11 834 em 120 s) mas nenhum
DMA PUT aponta para `0x0089xxxx/0x008Axxxx` (E149). **Próxima parede natural: o mixer SPU não escreve a posição das
vozes** — a investigar do lado SPU (descritor do job / ponteiro da tabela de vozes no bloco de controlo / lift SPU).

### Achados laterais do re-lift (sem relação com o fix, ficaram corrigidos)

- `patch_e131_movieio_release_on_close.py` corria antes de `patch_f2b_multimb_install.py` (ordem do glob) e
  recusava "f2b_fo_mfd_get static nao encontrado": faltava a linha no `games/gow2/lift_baseline/PATCH_DEPS.tsv`.
- `patch_e136_freelist_pop_probe.py` (commitado) deixava um `{` por fechar nas sondas `lwarx`/`stwcx` → chunk 001
  não compilava ("function definition is not allowed here"); a árvore v2 tinha sido corrigida à mão sem corrigir o
  script. Script corrigido.
- `patch_2b0db0_pushh_probe.py` (sonda do E87, catalogada como FUNCIONAL) não casa a agulha num lift fresco;
  `func_002B0DB0` é idêntica entre v2 e e162 fora das linhas da sonda — a agulha já não existia no v2 sem sonda.
  Fica por reclassificar/retirar.


## §4b A parede seguinte, andada até ao produtor: o mixer do SCREAM é uma *workload* SPURS cujo import nunca resolvia (E163–E173)

Depois do E162 a voz fica atribuída mas a sua posição nunca avança. Cadeia medida (in-boot, `boot_gow2_e163..e170`):

| # | medição | resultado |
|---|---|---|
| E163a/b | trace de DMA SPU com janela (`PS3_TRACE_SPUDMA_EA`) nas tabelas t200/t300 (`0x890000-0x8E0000`) | **zero** GET/PUT: nenhum job SPU lê/escreve as tabelas de voz |
| E163c | `PS3_TRACE_USLEEP_RATE` | o pacer de áudio (`syn_tick_timer_thread`, tid 6, prio 100) cai de 138 usleep/5 s para **1/5 s** |
| E164 | cabeçalho da LFQueue `0x47CA9B80` nos dois lados (`[LFQHDR]` no push PPU, dump no `GETLLAR`/`PUTLLC` SPU) | layout coerente: 859 pushes, **1 717 PUTLLC** (o SPU pop-a tudo; 130 090 GETLLAR = spin pop-empty), cabeçalho final `h1=h4=h5=h8=0x35B` (fila vazia) |
| E165 | `sample` do processo aos 45 s | a thread principal segura o giant lock em `gow2_midasm_Ce03cWaitIdle → func_002C0508 → func_002BFD10` (leituras byte a byte, sem stores → sem ponto de preempção); o pacer passa 100 % em `ppu_giant_lock_acquire_prio` |
| — | NIDs por trampolim (libstub do ELF) | o "submit" do tick é `cellSpursReadyCountStore` (0xF843818D) e a consulta `cellSpursGetWorkloadInfo` (0x4E153E3E): o mixer é uma **workload SPURS própria**, não a LFQueue |
| E167 | `PS3_SPURS_ATTR_DUMP=1` | `AddWorkload(wid=0, pm=0, size=0)`: o atributo é lixo da stack — **`_cellSpursWorkloadAttributeInitialize` (0xEFEB2679) estava por resolver** (o gerador de NIDs deriva o NID do nome; só existia a variante sem underscore, 0xFD4204D7) → `ReadyCountStore` "cleared (no PM)" → o mixer nunca corre |
| E168/E169 | alias `_cellSpursWorkloadAttributeInitialize` (+ `cellSpursWorkloadAttributeSetName`; 9.º arg lido de `r1+0x74` via `g_last_hle_r1`) | `_WorkloadAttributeInitialize pm=0x004FD980 size=0x2D00 data=0x886280`; `AddWorkload(wid=0, size=11520)` — mas nenhum dispatch |
| E170–E172 | `ReadyCountStore(N>0)` passa a submeter o job ao kernel HLE (lazy-start como o `CreateTaskset`); `PS3_TRACE_SPURS=1` | `QUEUE_PUSH … is_task=0` sim, mas com 2 workers **7 pops em 100 s**: 9 952 jobs das tasks do taskset (≈11 ms cada, soma 101,9 s) — o tick de re-kick do E156 monopoliza os workers |
| E173 | `PS3_SPU_WORKERS=4` | **`dispatch MISS fp=0xCEDB9A67A0C3A305 size=11520`** — o policy module do SCREAM chega ao dispatcher; imagem despejada (`spu_miss_cedb9a67a0c3a305.bin`, = bytes do ELF em `0x4FD980`, código SPU cru, sem cabeçalho ELF) |

**Retractações desta parte (marcadas aqui, no mesmo acto):** (a) "o SPU só faz 33 GETLLAR e nunca PUTLLC" era o **cap de 100 linhas** do trace atómico — sobrevive só "nos primeiros 100 eventos não houve PUTLLC"; (b) "a fila enche (800) e o push bloqueia o pacer" cai — a fila está vazia no fim (E164); o pacer está preso no giant lock (E165). As medições de §1 (tabelas nunca escritas, samples 0) mantêm-se.

**O que fica por fazer (próxima parede, natural):** o PM do SCREAM (11 520 B em `0x4FD980`, entrada LS `0x3000`, lê a stack do kernel em LS `0x1540`, copia uma rotina para `0x3FEC0`, 12 `bi $r0` computados) precisa de (1) ser liftado (`tools/spu_lifter.py --base 0x3000`, 35 funções num lift experimental) e registado como `spu6` com fp `0xCEDB9A67A0C3A305`; (2) um caminho de dispatch para PMs *crus* (copiar para LS `0x3000`, entrar em `0x3000`, com o contexto do kernel SPURS que hoje só o `spu_task_ls_setup` prepara para tasksets) e o argumento `data=0x886280` em r4; (3) o que o PM carrega por DMA a partir do bloco de controlo (`0x886280`) — provavelmente o mixer real — será mais uma imagem a liftar. Além disso: o tick E156 a 2 ms ocupa os 2 workers com as tasks do taskset e deixa as workloads à fome (E172) — rever antes de o promover; `PS3_SPU_WORKERS=4` é só o instrumento.


## §4c Onde isto liga às paredes [D]/[E] (registry de shaders): medido, não inferido (E174–E177 + bissecção)

Pergunta: o registry de shaders / CGOWShader é a meta; o relógio de áudio está no caminho? Medição com o binário de cada
etapa, env por defeito do `env_gow2.sh` (`PS3_MOVIE_EOS=1`, a ponte) + `PS3_TRACE_REG=1`, 150 s:

| binário | `[LDRSTATE]` (loader) | `st620` final | "overlay done" | `[movieio] open` | m2v pelo FIOS natural |
|---|---|---|---|---|---|
| `e130` (v2, antes do M9) | **28 457** (chega ao estado 22) | 0 | 2 (os dois filmes) | 7 | 0 |
| `e146` (M9: vdec natural) | 0 | **11** | 1 | 1 | 0 |
| `e153` | 0 | 11 | 1 | 1 | 5 |
| `e161`/`e174` (v2 + runtime actual; lift e162) | 0 | 11 | 1 | 1 | — |
| `e175` (lift e162 + `PS3_VDEC_FORCE_SEQDONE_MS=8000`) | 0 | 0 | — | 1 | — (`[ICALL-BAD] ctr=0x0100C801 r2=0x1C9DC800`: o lift `--config` não tem o pad de abort do CE03C) |

Leitura: desde o **M9** o 2.º filme da intro (`smlogo_v2.m2v`) deixa de passar pela ponte (`[movie] guest movie -> host VT
overlay` + "overlay done" → EOS) e passa a ser aberto pelo FIOS natural e descodificado pelo `cellVdec` natural — cujo
relógio é o **tempo do áudio**. Sem o mixer do SCREAM o player fica na 1.ª imagem, `st620` estaciona em 11, o loader
nunca corre e **nada a jusante — WADs, `r_shella`, estado 22, o walk do typemap, `ICGLdrShader`/`CGOWShader` — é
sequer alcançado**. Em `e130` (ponte inteira) o registry mostrava exactamente a parede [D] documentada:
`time to load/compile shader (FlashUI.ps3fx [Unknown combination string, disable USE_PRECALCULATED_SHADER_CRCS in
CGOWShader.cpp])` + `Default.ps3fx`, `[LDRSH]=0`, `SHADERSRC=0`.

Conclusão operacional: (a) o caminho **natural** para [D]/[E] passa pelo mixer do SCREAM (§4b) — é o único produtor
natural de "filme acabou" para o 2.º filme; (b) se se quiser trabalhar [D] antes disso, a ponte do 2.º filme (overlay
VT a partir do `movie_cache`, que o M9 deixou de arrancar porque o open já não passa pelo `movieio`) teria de voltar
**gated e declarada como ponte**, nunca como aceite; (c) o `PS3_VDEC_FORCE_SEQDONE_MS` não serve com o lift `--config`
(falta o pad de abort, documentado no hook `Ce03cWaitIdle`).


## §4d Ponte gated para o 2.º filme, o stall pós-intro e a reserva GETLLAR/PUTLLC (E178–E194)

Para trabalhar [D] sem esperar pelo mixer, repôs-se **como ponte declarada, gated e OFF por defeito** o overlay
do 2.º Play: `PS3_MOVIE_VT_REPLAY=1` (`libs/video/movie_vt_metal.m` + `gow2-recomp/movie_eos_arm.c`) repete o
overlay do cache quando o hook EOS do Play#1 já foi consumido e a FSM volta a esperar em `st620≥10`. Medido
(e181/e182): "overlay done" ×2, EOS do Play#2, `st620 11 → 0`, `[CONTENT] boot logo queue DONE — hold cleared`.

Depois disso o loader continuava a não correr. `sample` aos 110 s (e186): a thread principal presa em
`_cellSpursLFQueuePushBody → lfq_get_push_pointer` (push **bloqueante numa fila cheia**, com o giant lock; todas as
outras threads PPU em `ppu_giant_lock_acquire`), enquanto os dois workers SPU giravam no **pop** da mesma fila
(`spu0_spu_func_0000EAF8`) como se estivesse vazia. Cabeçalhos dos dois lados (e187/e189):
`[LFQ] push BLOQUEADO q=0x47CA9B80 h8=1281 h1=1288 depth=2048` — **o cursor de pop (h1) à frente do de push (h8)**,
e um atraso constante de 64 entre h8 e h5. Causa: o caminho atómico do SPU fazia o PUTLLC **sempre** com sucesso,
escrevendo a linha inteira a partir da cópia do GETLLAR e apagando os pushes do PPU feitos entretanto.

**Fix (E190, fiel ao hardware):** `runtime/spu/spu_dma.h` guarda um instantâneo da linha no GETLLAR; o PUTLLC só
escreve se a linha não mudou (comparação+escrita sob `ps3_atomic_line_lock`, o mesmo mutex que o HLE do PPU usa em
`lfq_lock`) e devolve o resultado em `MFC_RdAtomicStat` (1 = falhou → o libspurs no SPU repete). Medido (e190–e193):
`push BLOQUEADO` 0 (antes 28 505 linhas), `PUTLLC FALHOU` ocorre e é repetido, o inflate volta a andar pós-intro
(`HOSTINFL` 10 vs 0) e o frontend chega a despachar as workloads do escalonador (`MISS fp=0x44DDB4D656A72843` /
`0xB5B483F1479DAE63` = spu5/spu4, gated `PS3_SPU4/5`).

Também: `ReadyCountStore` só submete jobs cujo PM tenha imagem liftada registada (e182 tinha 110 759 `MISS` em 4 min).

**Fica em aberto (medido, não corrigido):**
- **Corrupção intermitente pós-intro** (e190/e191, não em e192/e193): `[ICALL-BAD] ctr=0x00000001 r2=0x00000000`
  em `lr=0x14F7FC/0x165AF4/0x179544/0x45578C/0x4654E0` (a seguir a imports; `r2` restaurado de `r1+0x28` vem 0)
  → `FATAL stuck calling 0x1`. Nenhum DMA SPU nas stacks (janelas `0xD0000000-0xD1000000` e `0x0FE00000-0x10000000`,
  e192/e193). É uma corrida; a suspeita seguinte é o processamento real dos jobs do SCREAM que a reserva destravou.
- Com `PS3_SPU_ALL=1` (spu2–5 registados, necessários ao `schedul` do frontend) o e194 morre em
  `FATAL stuck calling 0xFFFFFFFF` (8×) — as imagens spu2–5 "podem faltar a meio" (nota do registo).
- **As imagens SPU que o frontend despacha estão a zeros (E195):** os `MISS fp=0x44DDB4D656A72843/0xB5B483F1479DAE63`
  têm o tamanho das spu5/spu4 (54 544 / 38 504 B) mas o conteúdo despejado começa em `00000000…` (só 5 885 / 5 217
  bytes iguais aos ELF de Julho, que começavam em `7F454C46`). Ou seja, o buffer para onde o jogo carrega o ELF do
  escalonador **ainda não foi preenchido** quando a workload é despachada: o stream pós-intro (psarc/WAD pelo caminho
  natural `FUN_002b9f00 → AREAD-HLE`, `HOSTINFL` 10 chunks e depois 0) está parado. Em Julho isto era servido pelo
  preenchimento host do F2B (`PS3_F2B_HOST_FILL`, hoje legado OFF pelo E90). Logo `PS3_SPU_ALL=1` não pode ajudar
  (as imagens registadas nunca casam) e o `schedul` nunca sinaliza. **É este stall do stream pós-intro que bloqueia
  o loader** — e, a jusante, [D]/[E].
- Nenhuma corrida chegou ao `[LDRSTATE]` com o runtime actual; o loader (e daí `ICGLdrShader`/`CGOWShader`) segue
  bloqueado atrás destes pontos. O registry mostra o mesmo de sempre: `FlashUI.ps3fx [Unknown combination
  string]` + `Default.ps3fx`, `[LDRSH]=0`.


## §4e O elo concreto do stall pós-intro: o open natural devolve `0x80010708` (E196–E202)

Com a reserva (E190) e a intro a acabar pela ponte, o loader continua sem correr. Medido com o lift `e162`
(sondas FIOS) + runtime actual (`e200`–`e202`):

| op | resultado |
|---|---|
| `open /gow2.psarc`, `open /_movies/smlogo_v2.{m2v,wav}`, `read` (0xA), `close` (0xD) | `status=0` |
| **re-open** `/_movies/smlogo_v2.m2v` no Play#2, no **mesmo objecto de op (`0x430094C0`) que acabou de fazer os dois `close`** | **`0x80010708`** (o `.wav`, no outro op, abre) |
| `open /wad/r_lglsca.wad_ps3`, `open /wad/r_perma.wad_ps3` (pós-intro) | **`0x80010708`** ×2 cada — e por isso os ELF SPU do escalonador ficam a zeros (§4d) |

`0x80010708` é atribuído em `FUN_002fe8bc` (se `bit 9` de `[fh+8]+0x30` estiver set — `fh+8` é o scheduler `0x43009270`),
em `FUN_0030a4a0` (op sem media que o aceite) e em `FUN_0030cd48` (cancelar a lista de ops pendentes, chamado por
`FUN_0030ce84`/`FUN_0030ed68`). Não muda com `PS3_MOVIEIO_NO_RELEASE=1` (E131 off) nem com `PS3_F2B_HOST_FILL=1`
(legado). Em `e130b` (antes de E131–E139) os WADs abriam pelo `movieio` (cache host), nunca por este caminho — ou seja,
**o open natural de um ficheiro dentro do psarc a seguir a um close nunca foi visto a funcionar** no nosso runtime.

Também nesta parte: `sys_tty_write` passou a ponto de escalonamento e `ppu_giant_lock_hot_backoff` larga o giant
enquanto há waiters (E198; antes girava com o lock na mão) — a thread principal deixa de ficar presa atrás do
serviço de som que imprime 16 KB de crases em laço (`[USLEEP]`-rate e `sample` e196), mas o stall do open mantém-se.
Apareceram ainda corrupções intermitentes pós-intro (`ctr=1`/`0xFFFFFFFF` com `r2=0` a seguir a imports) que não
ocorrem em todas as corridas.

**Próximo passo (oráculo):** no RPCS3 (`EBOOT_intro.BIN`), bp no `bctr`/retorno de `FUN_002fe8bc` e em `FUN_0030cd48`
durante a intro: ler `[0x43009270+0x30]` (bit 9) antes/depois do `MovieStop`, e confirmar que na consola o re-open
do m2v e os opens dos WADs voltam 0. O que estiver diferente no nosso close (E131–E137, `FUN_0030d0ac`) é o produtor.


## §4f O `0x80010708` visto pelo oráculo: op reciclado antes da fase 2, yields nossos, e o lwmutex que não exclui (E203–E210)

Oráculo (RPCS3, bps em `0x30B058`/`0x30644C`): na consola o **re-open** do m2v depois do close usa **outro op**
(`0x33009680`) — o op do close (`0x330094C0`) ainda está em curso (o `done` do close nem aparece antes do re-open); a
conclusão do open é uma só chamada `FUN_0030644C(op, 2)` a partir de `0x309D04`. Em nós, cada op recebe **duas**
conclusões (`r4=1` de `0x30F24C`/`FUN_0030ed68` e `r4=2` de `0x309D04`), o op do close é devolvido à freelist de
imediato e o re-open reutiliza-o; o `done r4=1` do op que falha vem de **`0x30CE54` (`FUN_0030ce84`)** = o scheduler a
cancelar os ops da lista pendente que têm `op+0x30 & 0x200` (`0x300` é o que a fase 2 escreve).

- **E205** — com os "yields" injectados por nós no caminho de done/cancel/stop do FIOS desligados
  (`PS3_FIOS_YIELDS=0`, gate acrescentado no lift `e162` aos blocos `FIOS-*-YIELD`; os scripts
  `patch_fios_{done,cancel,42b4_cancel,done_cancel,stop}_yield.py` ainda não têm o gate): os ops passam a ser
  distintos como na consola e **o re-open do m2v passa** (`status=0`). Os opens de `/wad/*` continuam cancelados —
  **num op novo** (`0x43009840`), logo não é reutilização: alguém marca `op+0x30|=0x300` entre o submit e a passagem
  do scheduler. Não muda com `PS3_USLEEP_PRIO=0 PS3_TTY_PREEMPT=0 PS3_SPU_TASK_TICK_MS=0` (E206).
- **E208** — dump do objecto do scheduler (`0x43009270`, 0x100 bytes) consola vs nós no submit: os lwmutex
  embebidos (`FIOSmutx`, `FIOScond`) diferem — consola `owner=FFFFFFFF attr=0x12 (PRIORITY|RECURSIVE) id=0x95009800`,
  nós `owner=0 attr=0x02 id=0x24`, e mais tarde `owner=1 recursive_count=1` com `owner=FFFFFFFF recursive_count=1`
  noutro instante (estado incoerente). Causa: `runtime/ppu/ppu_sysprx.cpp` regista handlers ctx de
  `sys_lwmutex_lock/unlock/trylock` que são um **stub "single-thread boot"** (owner:=1, sem exclusão mútua) e o
  `create` escreve `owner=0`/`attr=protocol` (sem o bit RECURSIVE). Com várias threads PPU e handoffs do giant lock
  dentro de syscalls, duas threads ficam "dentro" do mesmo lwmutex do FIOS.
- **E209/E210** — tentativa de lwmutex real (slot pthread + handoff do giant, owner/recursão como na consola):
  o boot **pára antes da intro** (`st620` fica em 0, a task SPU do inflate entra em `[SPU] indirect branch to unknown
  LS address 0x03308` ×67k). Revertido; fica como a próxima ronda: alguém bloqueia num lwmutex que nunca é largado
  (provavelmente um `unlock` por outra thread/handler que o stub tolerava) — medir com o oráculo qual.

Também nesta parte: as sondas `E150`/`E151` passam a imprimir `op+0x30/+0x34`, `lr` da conclusão, `media+0x30` e o
dump do scheduler (`[FIOSOP] SCHED@`), reaplicáveis ao lift.


## §4g lwmutex real, gated (E211–E216)

Implementado o lwmutex com exclusão mútua real: slot pthread por lwmutex (id em `+0x10`), handoff do giant lock ao
bloquear, recursão por thread, `sys_lwcond_wait` a largar/readquirir o slot real durante a espera, `owner`/`attr`
como na consola. **Resultado (E212/E214):** o boot pára antes da intro — com exclusão real, o task-lib SPU do
decoder (imagem spu1, taskset `0x43006800`) segue o caminho de espera e cai em alvos `bi` que o lift nunca cobriu:
`0x3308` (resolvido com `--extra-funcs 0x3308` no re-lift a partir do ELF despejado com o novo `PS3_SPU_DUMP_HIT=1`)
e depois **`0x96F8`**, que nesta imagem decodifica como dados (`dfceq`): é código da task edgezlib carregada em LS
pelo PM (imagens spu2/spu3, hoje opt-in), não do PM. Ou seja, o lwmutex real precisa das tasks SPU liftadas e
registadas pelo seu `image_id` — trabalho da ronda seguinte. Ficou **gated OFF**: `PS3_LWMUTEX_REAL=1` liga; o
default (stub) tem o smoke da intro igual ao anterior (e216: st620 até 11 e Play#2, 8 opens FIOS ok, sem `unknown LS`).
Re-lift do spu1 revertido (perdia 0x3128/0x4070 do lift de Julho).


## §4h Com o lwmutex real, o task-lib SPU pede o kernel SPURS a sério (E217–E219)

Os quatro ELF SPU do jogo estão no EBOOT e casam com os fingerprints registados: spu0 `0x4D8780` (PM do SCREAM),
spu1 `0x4E5D00` (task do decoder, entry 0x3050), spu2 `0x4EF380` (edgezlib, 0x4080), spu3 `0x500900` (0x4080).
Re-lift do spu2 com `--extra-funcs 0x96F8` (= o lift de Julho + essa função) e corrida com `PS3_LWMUTEX_REAL=1`:
o alvo 0x96F8 resolve, e a task passa a cair em **`0x3308`** (PM), **`0x0011A`**, **`0x29B80`** e **`0x3FDAA`** —
endereços do **kernel SPURS** em LS (área 0x0–0x3000 e trampolins no topo), não de nenhuma imagem liftada. Isto é
o que a consola faz quando uma task espera (yield/wait-signal/poll através das entradas do kernel), e o nosso HLE
do taskset só emula o par `syscallAddr=0x1FF00/0x1FF10` (`spu_task_ls_setup`). Conclusão: o lwmutex real (que é o
que a FIOS precisa para o open dos WADs não ser cancelado) só é utilizável com um HLE das entradas do kernel SPURS
que o task-lib usa no caminho de espera. Re-lifts de spu2/spu3 revertidos; `PS3_LWMUTEX_REAL` fica OFF.


## §4i O caminho de espera do task-lib fecha; o que fica é o wait/signal do FIOS com exclusão real (E220–E224)

Com spu1 re-liftado (`--extra-funcs 0x3128,0x4070,0x3308,0x9300`, a partir do ELF despejado) e spu2 (`0x96F8`), e o
helper `spu_dfceq` no runtime, **deixa de haver alvos `bi` desconhecidos** com `PS3_LWMUTEX_REAL=1` (os 0x11A/0x29B80/
0x3FDAA eram consequência dos splits em falta, não entradas do kernel). A task do decoder (spu1) corre o seu laço real
(`0x95E8 → 0x9B28/0x9A74`). Mas o boot pára no open do `/gow2.psarc`: a thread principal espera pela op
(`func_002B4CDC → func_003067DC`), as 4 threads FIOS ficam em `sys_lwcond_wait` e nunca há push na LFQueue do
inflate nem `HOSTINFL` (e223/e224). Ou seja, com exclusão mútua real o par **`lwcond_signal`/`lwcond_wait` sobre o
lwmutex do scheduler FIOS** (`FIOSmutx`/`FIOScond`) não acorda quem espera — é o próximo defeito a medir (o nosso
`sys_lwcond_wait` já larga/readquire o slot real; falta ver o `signal` e a ordem owner/slot). Re-lifts revertidos ao
lift de Julho (o novo perdia `0x4078` no spu1); os `--extra-funcs` acima ficam registados para a ronda seguinte.


## §4j O deadlock do arranque com exclusão real é um *lost wakeup* no `schedul` do FIOS (E225)

`PS3_LWCOND_TRACE=1` com `PS3_LWMUTEX_REAL=1` (spu1/spu2 re-liftados): a thread principal faz
`SIGNAL cond='schedul' slot=2` (submit do open do psarc, `0x30A8F8`/`0x30B1C0`) e entra em `WAIT-ENTER cond='opWait'`;
**só depois** a thread do scheduler entra em `WAIT-ENTER cond='schedul' slot=2 gen=1` (`caller 0x30EF3C`) — e nunca sai:
o sinal foi emitido antes de haver waiter (semântica de condvar), mas na consola isso não bloqueia porque a verificação
da fila de ops e o `wait` do scheduler são atómicos sob o `FIOSmutx`; aqui o scheduler não viu a op pendente antes de
esperar. Hipóteses a discriminar na ronda seguinte: (a) o `sys_lwcond_wait` real liberta o slot/owner antes de o
`gen0` ficar protegido pela exclusão do lwmutex (ordem `slot_lock → gen0 → release slot`), (b) o submit insere a op
e sinaliza sob um lwmutex diferente do que o scheduler usa na verificação, (c) o scheduler arrancou tarde (prio 100
vs principal 1001) e a sua primeira verificação da lista `sched+0x208` corre sem o lock. Medição barata: com
`PS3_LWCOND_TRACE=1`, imprimir também o owner/`recursive_count` do lwmutex no `WAIT-ENTER` e no `SIGNAL`, e o
número de ops em `sched+0x208` nos dois pontos.


## §4k Exclusão real a passar a intro; preempção temporal do giant lock; o cancel dos WADs tem mecanismo (E229–E233)

- **E229 — preempção temporal**: watchdog de 1 ms marca `g_ppu_preempt_req` quando há waiters do giant lock e
  `vm_read8/16/32/64` (`ppu_memory.h`) chamam `ppu_preempt_check()` (larga/readquire) se o chamador for o detentor. É
  o modelo da consola (2 HW threads + escalonador preemptivo); `PS3_GIANT_TIMESLICE=0` desliga. Smoke do modo stub
  (e230) igual ao anterior (intro até 11, 8 opens FIOS, `ICALL-BAD` 0).
- **E232/E233 — o deadlock do arranque em modo real era o `recursive_count`**: `[LWMT]` mostrou o scheduler a fazer
  `lock slot=40` (mutex das media threads) e a nunca o largar — porque o `recursive_count` guest tinha subido 1→2→3
  com os locks das três media threads que depois esperaram (o nosso `lwcond_wait` largava o slot mas não zerava o
  contador), o 4.º lock via `rc>1` no unlock e não largava o slot; as threads acordadas pelo `SIGNAL-ALL` ficavam presas
  no re-lock do condvar. Fix: `sys_lwcond_wait` guarda/zera/repõe `recursive_count` (como a consola). Com
  `PS3_LWMUTEX_REAL=1` a intro passa (st620 → 11, Play#2, 10 opens FIOS, 76 chunks de inflate).
- **Cancel dos WADs, agora com mecanismo**: em modo real e sem yields, os ops já são distintos e mesmo assim, logo após
  `submit /wad/r_lglsca` no op `0x43009680`, aparece `done op=0x43009840 tipo=0x0 r4=1 lr=0x30B034` — o **submit**
  conclui de imediato outro op (o do close anterior, já livre, tipo zerado) e o op novo sai cancelado
  (`0x30CE54`, `op+0x30=0x300`). Hipótese medível: o `fh` reciclado (`0x43018630`/`0x43018528`) guarda ainda o
  ponteiro para o op do close (fase 2 nossa não o limpa) e o submit trata-o como "op pendente neste fh". Na consola o
  open seguinte (`op 0x33009760`, t=10.71 s) completa com 0. Próximo passo: dump do `fh` (+0x90..+0xA0) no submit dos
  dois lados e confirmar qual palavra o `FUN_0030b058` lê para chegar a `FUN_0030afd0`.


## §4l O submit auto-completa o op via `FUN_0030afd0`; a decisão está em `FUN_003044ec(op+0x48)` (E234–E235)

Comparação directa consola vs nós no `submit` (`FUN_0030b058`, bp/sonda) confirma o mecanismo do cancel dos WADs:
o próprio submit chama `FUN_0030afd0 → FUN_0030644C(op,1)` (o `done ... lr=0x30B034` que víamos). O ramo:
`piVar5[0x12]` = `*(op+0x48)` (o descritor de open, na stack guest — medido `op+0x48=0x0FEFF970` para o WAD,
`op+0x1c=0`); se `≠ 0`, chama `FUN_003044ec(*(op+0x48))` e, conforme o resultado, ou marca `op+0x44=0x80010705`
e completa, ou segue para o vt do media manager (`[obj+0x148]→[+0x14]`) e completa. Na consola o mesmo submit dos
WADs (`op 0x33009760`, medido até `r_m_lghtn2` aos 105 s) completa com `status=0` e o open segue; em nós completa e
o scheduler depois cancela (`0x30CE54`, `0x80010708`). O `fh` reciclado é idêntico em estrutura (dump `FH@`); a
divergência está no que `FUN_003044ec` devolve para o descritor `op+0x48`. **Próximo passo (decisivo):** sonda em
`FUN_003044ec` (entrada `*(op+0x48)`, saída `cVar9`) e o media descriptor que ela lê, nos dois lados — é o último
ramo antes do `0x80010705`/cancel, e a partir daí o open dos WADs, o loader (estado 22) e a construção do nível que
popula o registry [D]/[E].


## §4m Retracção medida: o cancel dos WADs NÃO é `op+0x30 & 0x200` (E236)

Watch (`PS3_WATCH_STORE`) sobre `op+0x30` das cinco ops do pool (`0x430094F0..0x43009870`): **39 escritas, todas `=0x0`**,
de `func_0030633C+0x1D0`, `func_003063D8+0x190` e `func_0037750C` (os caminhos de init/reset do op). Nenhum store do
guest põe o bit `0x200` em `op+0x30`. Portanto **cai** a inferência de §4e/§4l de que o scheduler cancela por
`op+0x30 & 0x200`: o `0x300` que a sonda `[FIOSOP] done` lia em `op+0x30` é lixo pós-reciclagem (a done corre em
`FUN_0030644C`, e `op+0x30` já foi reescrito como elo da freelist). **Sobrevive** a medição: o WAD recebe `0x80010708`
de `FUN_0030ce84`/`0x30CE54` (o cancel de ops pendentes), e o submit auto-completa o op anterior via
`FUN_0030afd0 → FUN_0030644C(op,1)`. O que fica por medir: qual o campo do **nó da lista** (`sched+0x208` / `param_2[0xc]`
em `FUN_0030ce84`) que decide o splice — não é o `op+0x30`. Próximo: watch sobre `sched+0x208` e sobre o nó que
`FUN_0030ce84` percorre, comparado com a consola (bp em `0x30CE84`), para ver que op entra na lista de cancelamento e
porquê — é o último elo antes do open dos WADs e, a jusante, do registry [D]/[E].
 **Medido (E237):** watch em `sched+0x208` (0x43009478) mostra o op do WAD **`0x430095A0` a ciclar** — empurrado por
`func_00308DC0+0x18D8`, drenado por `func_0030A7C0+0xB1C`, repetidamente (o retry do open que falha). O cancel/erro
está no drenador **`func_0030A7C0`** (que alcança `func_0030ce84 → func_0030cd48`, o `0x80010708`). Estas duas funções
**não estão no dump do Ghidra** (abaixo do limiar de detecção; precisam de disasm). Passo seguinte: disasm de
`func_0030A7C0` + bp no console em `0x30A7C0` para ver o ramo sucesso-vs-cancel do drenador — é o último elo antes do
open dos WADs e, a jusante, do registry [D]/[E].



## §4n Produtor definitivo do `0x80010708`, verificado por watch (E238)

Watch sobre o **campo de status** `op+0x44` das cinco ops do pool (fim da ambiguidade das condições lidas do Ghidra):
`op+0x44` recebe `0x80010701` muitas vezes (marcador "em progresso", de `func_0030A4A0+0x550`, `func_00302E7C`,
`func_00311EA4`) e **`0x80010708` uma única vez**, de **`func_0030CD48+0x410`**, chamado de **`func_0030EDD0`
(= `FUN_0030ed68`)** — não de `FUN_0030ce84`. Cadeia definitiva:

`FUN_0030fbf8` (thread "fios scheduler") → `FUN_0030ed68` (passagem de dispatch, 621 linhas) → em `LAB_0030f410`,
se a lista `uVar15` ≠ 0, `FUN_0030cd48(sched, uVar15)` carimba `op+0x44 = 0x80010708` em cada op dessa lista.

Ou seja: o op do WAD acaba na **lista de cancelamento `uVar15`** que a passagem do scheduler `FUN_0030ed68` monta.
As hipóteses de §4e/§4l/§4m (`op+0x30 & 0x200`, submit auto-completa, `FUN_0030ce84`) ficam todas **subordinadas a
isto**: o `op+0x30` e o submit são ruído; o cancel real é `FUN_0030ed68 → FUN_0030cd48`. **Próximo passo (o último
elo):** ler o ramo de `FUN_0030ed68` que põe um op em `uVar15` (a decisão de "não despachável / falhar") e
compará-lo com a consola (bp em `0x30FBF8`/`0x30ED68` e watch de `op+0x44` no RPCS3): que estado do op ou do
scheduler faz o nosso op do WAD cair no cancel quando na consola é despachado. A partir daí: open dos WADs → loader
(estado 22) → construção do nível → registry [D]/[E].


## §4o A causa raiz do cancel dos WADs: `op+0x30` é posto a `0x300` FORA do caminho de store guest (E239–E240)

Sonda dentro da própria classificação de `FUN_0030ed68` (a linha liftada `gpr[0]=vm_read32(op+0x30)`, `rlwinm 22,22`
= bit `0x200`): **ground truth** — o op cancelado é `0x430094C0`, `op+0x30=0x00000300`, tipo `0x9` → `->CANCEL`
(`op+0x30 & 0x200`). Confirma o bit 0x200 e o offset 0x30 (Ghidra e lift concordam).

O decisivo (E240, watch + sonda no MESMO binário, cap 100 000): entre a última escrita vista em `op+0x30` (`=0x0`,
de `func_0030633C`/`func_003063D8`) e a leitura `0x300` no classificador, **o watch não apanha NENHUMA escrita de
`0x300`** — e o watch cobre `vm_write8/16/32` E `ps3_watch_store_bulk` com teste de sobreposição. Logo `op+0x30`
recebe `0x300` por um caminho que **não passa pelo store guest**: código host nosso (`memcpy(vm_base+ea,…)`) ou uma
**DMA do SPU** (que só chama o watch sob `SPU_DMA_TRACE_HOOKS`, hoje não definido no build). Um escritor fora do
guest a carimbar `0x300` (= "op completo, remover") no campo do op FIOS do WAD é o que faz o scheduler cancelá-lo
com `0x80010708`. `patch_fios_freelist_rebuild`/`HOST-POP` **não dispararam** nesta corrida (0), por isso não é isso.

**Retracções acumuladas nesta caça (todas medidas):** §4e/§4l (submit auto-completa; `FUN_003044ec`) e §4m (o campo
não é `op+0x30`) ficam subordinadas a §4n/§4o. **Sobrevive** a cadeia `FUN_0030fbf8 → FUN_0030ed68 → FUN_0030cd48`
e a medição de que `op+0x30=0x300` no op cancelado vem de fora do guest.

**Próximo passo (decisivo, estreito):** ligar `SPU_DMA_TRACE_HOOKS` (ou um watch host que cubra memcpy) e correr com
o watch em `0x430094F0` para apanhar o `who` da escrita `0x300` — é um `memcpy`/DMA nosso com EA errado a cair no op
FIOS. Corrigido isso, o WAD não é cancelado → open dos WADs → loader (estado 22) → construção do nível → registry
[D]/[E].


## §4p A armadilha das DUAS `vm_write32` e o escritor invisível (E241–E243)

`PS3_WATCH_STORE` (o `[WATCHSTORE]`) só cobre a `vm_write32` **liftada** (`ppu_loader.cpp:1594`, `w32_watch_hit`); as
**libs host** usam a `vm_write32` **inline** de `ppu_memory.h:330`, cujo watch é OUTRO (`PS3_WATCH_EA` /
`ppu_lowmem_write_hit`). Exactamente a armadilha "duas implementações de `vm_write32`" da skill de retracções — o meu
watch anterior (§4o) só via metade dos escritores. Corrigido: corridas com `PS3_WATCH_EA=0x430094F0` **e**
`0x430094F2` (o flag `0x0200`/`0x0300` é meia-palavra em `op+0x32`).

**Resultado (E242–E243): mesmo com os dois watches (liftado + host-inline, cap 100 000) e a busca por `vm_base[ea]=`
cru no host, NENHUM caminho escreve `0x300` em `op+0x30`/`op+0x32`.** E o classificador continua a ler `0x300` no op
`0x430094C0` (tipo 9 → cancel). Como `func_0030633C` zera `op+0x30` (visto no watch) e depois ninguém escreve, o
`0x300` só pode vir de um caminho que **não passa por nenhuma `vm_write*` nem `vm_base[ea]=`**: um `memcpy`/`pread` de
**dados de ficheiro** (movie_io/F2B a servir o WAD) cuja EA de destino cai sobre a op FIOS `0x430094C0` — os bytes do
psarc contêm `0x00000300` nesse offset. É a mesma classe do bug E138 (host a escrever por cima do scheduler FIOS),
mas por um escritor ainda não instrumentado.

**Próximo passo (decisivo, estreito):** (a) instrumentar as escritas de dados do `movie_io_read`/F2B pump
(`memcpy(vm_base+dst, filebytes)`) com o mesmo guarda de intervalo `[dst, dst+len) ∩ [0x43009270, 0x43009900)`; ou
(b) `mprotect` da página da op (`0x43009000`) a PROT_READ e apanhar o `SIGSEGV` do escritor — dá o stack exacto.
Corrigida a EA de destino errada, o WAD deixa de ser carimbado "completo", não é cancelado, e o open passa → loader
(estado 22) → construção do nível → registry [D]/[E].


## §4q REFRAME medido: `op+0x30=0x300` é o "completo" legítimo; o bug é o waiter não consumir a completação antes da limpeza (E244)

Instrumentados TODOS os escritores host de dados (`movie_io_pread` já tinha o seu próprio watch; `lfq_host_inflate`,
`cellSpursLFQueue`, `cellFsRead`, `movie_io_read2`) com guarda de intervalo sobre `[0x43009270, 0x43009900)`: **zero
hits**. Nenhum caminho carimba `0x300` na op. **Retracção de §4o/§4p** (a hipótese "corrupção de memória / `memcpy` de
ficheiro por cima da op" cai — medida, não confirmada).

O que sobra é a leitura correcta: `op+0x30 & 0x200` = **"op completa, remover"**. Ou seja, o open do WAD **completa
com sucesso** (a op fica marcada `0x300`), mas a nossa thread submetora está a dormir em `sys_lwcond_wait cond='opWait'`
(medido no `[LWTRACE]`) e **não é acordada a tempo de consumir a completação**; a passagem de limpeza do scheduler
`FUN_0030fbf8 → FUN_0030ed68` vê a op "completa mas ainda na lista, sem dono", e `FUN_0030cd48` re-carimba
`op+0x44 = 0x80010708` — que é o que o waiter acaba por ler. É uma **corrida de wakeup/ordem**, não uma escrita
espúria: na consola o submetora consome a op completa antes da limpeza; em nós a limpeza chega primeiro.

Isto reconcilia toda a cadeia: liga-se ao `lwcond signal/wait` e ao escalonamento do giant lock (timeslice E229,
lwmutex real E233) e explica porque a intro passa com o stub mas o open dos WADs não. **Próximo passo (o fix):** no
caminho de completação da op (quando `op+0x30` recebe `0x200`), sinalizar `opWait` e garantir que o submetora corre
antes da passagem de limpeza — ou impedir `FUN_0030ed68` de cancelar uma op completada há menos de um tick sem dono
(fiel à consola, não paliativo). A partir daí: open dos WADs → loader (estado 22) → construção do nível → registry
[D]/[E].


## §4r RETRACÇÃO PIVOTAL (AMENDADA por §4ai/E265): o `0x80010708` no OPEN é normal; no READ op é o gatilho do cancel

> **AMENDA (E265).** Sobrevive: `0x80010708` no OPEN do WAD é estado FIOS normal (a consola carimba-o e carrega na
> mesma). **Reverte-se em parte:** no `op+0x44` da op de LEITURA do R_PermA, `0x80010708` É o gatilho do cancel do
> DONE-poll (`FUN_00306610` devolve `op+0x44`; o poll cancela se não for 0/`0x8001070A`/`0x80010705`). O sinal real
> continua a ser o stream do R_PermA, e agora com o mecanismo exacto (§4ai). São ops diferentes: as duas afirmações
> coexistem.

### (texto original) o `0x80010708` é normal (a consola também o faz); o sinal real é o stream do R_PermA (E245)

Oráculo (bp em `0x30F3B4`, a classificação de `FUN_0030ed68`): na consola **muitos ops `0x33xxxxxx` são classificados
`->CANCEL (op+0x30 & 0x200)`** e os WADs carregam na mesma. Logo `0x80010708` (o que `FUN_0030cd48` carimba nesse ramo)
**é um estado interno normal do FIOS, NÃO a falha do open do WAD**. **Retractam-se §4e–§4q inteiras** enquanto
"o open do WAD falha com 0x80010708": a premissa estava errada. Sobrevive a mecânica (a cadeia de funções, os watches
que corrigiram as sub-hipóteses), mas a CONCLUSÃO ("é isto que bloqueia o loader") cai.

O discriminador real, medido comparando a corrida que **chegou ao loader** com as que não chegam:

| corrida | WADopen | `0x80010708` | R_PermA `bytes_read` | `r_shella` | `[LDRSTATE]` |
|---|---|---|---|---|---|
| **e130b** (loader CORREU) | 4 (lglsca, perma, hero01, shella) | **0** | **20 169 344** (20 MB, completo) | 3 | **18 607** |
| e233/e235 (agora) | 2 | 8 | — (não faz stream) | 0 | 0 |

Ou seja: em `e130b`, mais cedo nesta sessão, **o loader corria** — R_PermA fazia stream de 20 MB, `r_shella` (o WAD do
menu) abria, e NÃO havia `0x80010708`. As corridas de agora não fazem stream do R_PermA e o loader morre. **É uma
regressão de runtime entre o binário do `e130b` e o de agora** (mudanças acumuladas: E138 inflate, E139 rwlock, E154,
E156, E190 reserva, E229 timeslice, …), não o `0x80010708`. Isto é a parede [C]/[C2] (stream do R_PermA) a reaparecer.

**Próximo passo (o certo, tractável):** bisect do runtime entre o commit do `e130b` e HEAD, com o critério
`bytes_read do R_PermA == 20169344 && [LDRSTATE] > 0` (env por defeito: `PS3_MOVIE_EOS=1`, sem `PS3_FIOS_YIELDS`, sem
`PS3_MOVIE_VT_REPLAY`). Achado o commit que regride o stream, revertê-lo/corrigi-lo → loader (estado 22) → construção
do nível → registry [D]/[E]. O `0x80010708` fica para trás como o desvio que era.


## §4s O binário mais avançado (e130) está PARADO na parede [G] documentada — que é o portão de [D]/[E] (E246)

`[LDRSTATE]` só é impresso com `patch_e130_loader_states` presente; "LDRSTATE=0" noutros binários é **artefacto do
probe**, não prova de que o loader não corre. O marcador real de progresso é `r_shella` (WAD do menu). Medido com
env por defeito idêntico:

| binário | R_PermA `bytes_read` | `r_shella` | `wad/r_*` | `[LDRSTATE]` | shaders |
|---|---|---|---|---|---|
| **e130** (mais avançado, ~14:25) | 20 169 344 | 3 | 8 | cicla **22↔0** | só FlashUI+Default |
| boot_gow2 (produção, Ago) | 20 298 800 | 0 | — | (sem probe) | — |
| e235 (agora) | — (sem stream) | 0 | 2 | 0 (regressão) | só FlashUI+Default |

**O `e130` chega à parede [G] documentada** (`docs/re_sessions/2026-09-02-E124-E139*`): o level loader entra no
**loop do estado 22** (`[LDRSTATE] 22 ↔ 0`), os WADs abrem (`r_shella`, `r_hero`, 8× `wad/r_*`), mas
`[LDRSH]=0 / SHADERSRC=0 / ICGLdrShader=0` — o registry de shaders **nunca popula** e só compilam `FlashUI.ps3fx` e
`Default.ps3fx`. **É exactamente a parede [D]/[E]**: o loop do estado 22 é o portão; a construção do nível que
popularia o registry não avança porque o loader fica preso em 22.

Conclusão desta caça inteira (com as retracções §4o–§4r): as corridas recentes andaram em binários que **regrediram
atrás do `e130`** (perderam o stream do R_PermA — provável efeito de E138 inflate / E190 reserva / o re-lift e162, a
bisectar), e o `0x80010708` era um estado FIOS normal, não a falha. **O trabalho útil para [D]/[E] é o loop do estado
22**, exactamente a meta corrente do CLAUDE.md.

**Próximo passo (o certo, em duas frentes):** (1) restaurar no binário actual o que o `e130` tinha (bisect da
regressão do stream do R_PermA entre o build do `e130` e HEAD — critério `bytes_read==20169344`); (2) atacar o loop
do estado 22 no `e130` (o mais avançado): ler `FUN_0003f2b0 → FUN_002ba76c` que reporta "trabalho pendente" e manter
o loader preso, com o oráculo a mostrar a transição 22→16→17→23→24 que a consola faz (ledger E124-E139 §5). Passado o
22, a construção do nível popula o registry e dispara os compiles → [D]/[E].


## §4t A regressão pós-e130 é a task edgezlib SPU (`EDGE`) a não despachar: corrida de dois consumidores (E247–E249)

Isolado com o MESMO lift do `e130` (cópia `recomp_macos_v2p`) e o runtime actual: o `e130` completava a intro
(`st620 → 0`) com **EDGE=48** (a task SPU edgezlib inflava via SPU); as corridas de agora ficam em `st620=3`/`11` com
**EDGE=0** (a task SPU não corre) e caem no host inflate, que não completa. Medido:

| corrida | reserve (E190) | host inflate | EDGE | st620 |
|---|---|---|---|---|
| e130 | (não tinha) | ON | **48** | → 0 (completa) |
| e247_on | ON (default) | ON | 0 | 3 |
| e247_off | OFF | ON | **48** | 3 |
| e248b | OFF (default) | ON | 0 | 3 |
| e249 ×3 | ON | **OFF** (PS3_HOST_INFLATE=0) | 0 | 11 |

**É flaky:** `EDGE` vale 48 ou 0 entre corridas com a MESMA config (e247_off=48 vs e248b=0, ambas reserve off) — logo
**não é uma regressão de commit limpa; é uma corrida no despacho da task SPU edgezlib** (o §9 aberto do ledger
E124-E139: "dois consumidores da mesma fila, task lifted vs host; falta decidir quem serve"). O E190 (reserve) e o
timeslice E229 mexeram no escalonamento e enviesaram a corrida para o lado que NÃO despacha a task (EDGE=0). Desligar o
host inflate (PS3_HOST_INFLATE=0, gate novo em `cellSpursLFQueue.c`) não faz a task SPU correr — só muda o ponto de
stall (11). O reserve default fica ON (o flip para OFF foi flaky, não um fix); `PS3_SPU_RESERVE=0` continua a existir.

**Próximo passo (o fix da regressão, a montante de [D]):** estabilizar o despacho da task SPU edgezlib — garantir que o
push na LFQueue re-despacha a task (não só 1×/s) e que ela é o consumidor determinístico do elemento, com o host
inflate como fallback só quando a task não existe. Isto repõe o `e130` (EDGE=48, `st620→0`, R_PermA a stream, loader ao
estado 22). Só então o loop do estado 22 (§4s) fica atacável para chegar a [D]/[E].


## §4u O que o estado 22 FAZ: constrói o nível (blocos alocados), mas a condição de saída nunca dispara (E250)

Probe directo no binário QUE CHEGA ao estado 22 (`boot_gow2_e130`, via `PS3_WATCH_STORE` sobre os campos do objecto do
loader `0x869570`, sem rebuild). Durante o loop `[LDRSTATE] 22↔0` (13 724×): o campo `obj+0x1ac` (`0x86971C`) recebe
**valores crescentes de heap** — `0x1074280, 0x107C280, 0x1088280, 0x109C280, … 0x11E0280` — escritos por
`func_002BA054+0x12C` (chamado de `func_002BA9BC`/`func_002BAB88`), e `obj+0x1a8` (`0x869718`) = `0x4007FCD0` (o
stream de entrada). Ou seja: **no estado 22 o nível ESTÁ a ser construído** — o `FUN_002b9f00` lê os chunks do WAD, e o
construtor aloca e avança por blocos (`0x010xxxxx`), com `obj+0x1ac` a progredir.

Mas a condição de saída do estado 22 (`FUN_002ba76c:113`) é `obj+0x1ac < 1 && (input `obj+0x1a8→+0x10` == 0) &&
obj+0x1cc != 3` — e **nunca é satisfeita**: `obj+0x1ac` recebe ponteiros grandes (não desce a 0/`<1`), a lista de
trabalho da construção **nunca drena**. Na consola o estado 22 corre ~239× (≈5 s) e sai para 16→17→23→24; em nós fica
no loop. Portanto o portão de [D]/[E] é: **a lista de trabalho da construção do nível (`obj+0x1ac`) não drena** —
`func_002BA9BC`/`func_002BAB88` acrescentam entradas mais depressa do que o drenador as consome, ou o drenador não
corre. `func_002BA054/9BC/AB88` **não estão no dump do Ghidra** (fragmentos); a leitura tem de ser no lift.

**Próximo passo (o fix de [D], directo):** ler os fragmentos liftados `func_002BA054`/`func_002BA9BC`/`func_002BAB88`
e o drenador de `obj+0x1ac` (o que a consola faz sair do 22), e comparar `obj+0x1ac`/`obj+0x1cc`/`obj+0x1a8→+0x10`
entre `e130` e a consola no estado 22 (oráculo com `EBOOT_skipintro.BIN`, que salta a intro e vai direto ao load).
Achado porque a lista não drena, o loader sai do 22 → estados 16/17/23/24 → a construção termina → o registry popula e
os compiles do nível disparam → [D]/[E].


## §4v O portão exacto do estado 22: o stream de entrada nunca esvazia (E251) + como reproduzir o `e130`

Lift liftado (`recomp_macos_v2`): `func_002BA9BC` (o corpo do estado 22) LÊ `obj+0x1ac` (`gpr[31]+0x1AC`, linha 45898)
e DRENA-o a 0 (linhas 157/219, `vm_write32(gpr[31]+0x1ACu, 0u)`) quando termina uma unidade; `func_002BA054` volta a
pôr lá um bloco por cada chunk lido do WAD. A saída do estado 22 (`FUN_002ba76c:113`) exige, além de `obj+0x1ac<1`,
que **o stream de entrada esteja vazio**: `*(obj+0x1a8 + 0x10) == 0` (`obj+0x1a8 = 0x4007FCD0` no `e130`). Ou seja: o
loader lê os chunks do WAD pelo stream e só sai quando o stream esgota. Em nós o stream **nunca esgota** (`+0x10` nunca
chega a 0) → o `func_002BA054` continua a acrescentar trabalho → o estado 22 nunca sai. É a mesma raiz do stream do
R_PermA/WAD, uma camada acima: o alimentador (aread/psarc/edgezlib) não fecha o stream com o byte-count correcto.

**Bloqueio operacional desta ronda:** só o binário `e130` (lift v2 + runtime de ~14:25) chega ao estado 22; o runtime
actual regrediu (a task edgezlib SPU flaky, §4t) e não chega lá — e o `e130` foi apagado por engano num glob de limpeza.
**Reprodução:** `recomp_macos_v2` está intacto com o probe E130; para o binário que chega ao 22 é preciso o runtime de
~14:25 (antes de E190/E229) — `git checkout <commit-14:25> -- runtime/ libs/`, `cmake --build build-macos`, e
`OUT=boot_gow2_e130 ./build_macos.sh recomp_macos_v2`. Depois: watch de `0x4007FCD0+0x10` (byte-count do stream) no
estado 22 e comparar com a consola (oráculo `EBOOT_skipintro.BIN`, que vai direto ao load) para ver porque o stream
não esgota — é o portão directo de [D]/[E].


## §4w O elo antes do estado 22: o 2.º filme da intro não completa (E252, bisect cross-repo)

Bisect: runtime `87ab535` (pre-M9) + lift v2 reproduz **EDGE=48** (a task edgezlib SPU corre) mas fica em `st620`
a ciclar `11↔3` — o **1.º filme completa** (`[MOVIEEOS] read-hook HIT`, `CE03C wait-idle exit st620=0`) mas o **2.º
filme não** (o item aberto §9 do ledger E124-E139: "2.º filme da intro em st620=11"). O `e130` (build de ~14:25) levava
os DOIS filmes até ao load dos WADs (estado 22); reproduzi-lo exige o estado exacto dos DOIS repos a ~14:25 (o
`e130` foi compilado de uma árvore com mudanças por commitar em ambos). Ou seja, a cadeia completa para [D] é:

1. **2.º filme da intro completa** (hoje: só com a ponte `PS3_MOVIE_EOS`/`PS3_MOVIE_VT_REPLAY`, ou o M9 natural a dar
   `SEQDONE`) → `st620 → 0`.
2. **WADs fazem stream** (edgezlib SPU determinístico, §4t) → o loader entra no estado 22.
3. **O estado 22 esgota o stream de entrada** (`obj+0x1a8→+0x10 → 0`, §4v) → sai para 16→17→23→24.
4. **A construção do nível popula o registry** e dispara os compiles do nível → [D]/[E].

Cada elo está medido e localizado a uma função/campo concretos (§4b–§4v). O binário `e130` prova que os elos 1–3
chegaram a funcionar juntos numa árvore de ~14:25; o trabalho é re-alinhar os quatro elos no HEAD e depois esgotar o
stream do estado 22. `recomp_macos_v2` (com o probe E130) e a receita de reprodução ficam em §4v.


## §4x DECISIVO: a leitura do R_PermA regrediu no HEAD e não é recuperável por env (E253)

Com um binário HEAD (`recomp_macos_e162` + runtime actual) testei todas as pontes:

| env | st620 (intro) | R_PermA `bytes_read` | LDRSTATE |
|---|---|---|---|
| `PS3_MOVIE_VT_REPLAY=1 PS3_FIOS_YIELDS=0` | **0** (completa) | — | 0 |
| `…VT_REPLAY + F2B_HOST_FILL=1` | 0 | — | 0 |
| `…VT_REPLAY` (default+replay) | 0 | — | 0 |
| default | 3 | — | 0 |
| `e130` (ref, ~14:25) | 0 | **20 169 344** | 28 555 |

A ponte de replay resolve o **elo 1** de forma fiável no HEAD (`st620 → 0`, a intro completa). Mas o **R_PermA abre e
NUNCA lê** (`bytes_read=0`) em TODAS as configs — o inflate pára a ~13–15 blocos (de ~320) e o loader nunca corre. O
`e130` lia os 20 MB via o caminho host do `movie_io`. **É uma regressão de CÓDIGO** (nos commits desde o `e130`: M9 +
E147–E252), **não recuperável por env** (nem `F2B_HOST_FILL`, nem host/SPU inflate isolados). O `e130` foi compilado
de uma árvore com mudanças por commitar em AMBOS os repos, logo não há um commit limpo para bisect direto — é preciso
bisectar os ~10 commits de runtime/lift desde ~14:25 (Set 2) reconstruindo a `.a` a cada passo, com o critério
`bytes_read do R_PermA > 0`.

**Estado final medido da cadeia para [D]:** elo 1 (intro) — resolúvel no HEAD com a ponte de replay; elo 2 (R_PermA
lê/stream) — **regredido, o alvo do bisect**; elo 3 (estado 22 esgota o stream) — caracterizado no `e130` (§4v); elo 4
(registry popula) — a jusante. O trabalho concreto e ordenado está todo medido; o próximo passo é o bisect do elo 2.


## §4y O bloqueio de link 1 é cross-repo, não um único commit (E254): reverter cellVdec/sched/reserve não repõe o `e130`

Bisect por reversão pontual (todos negativos, medidos):

| revertido | 2.º filme (`st620→0`?) | WADs por movie_io | R_PermA `bytes_read` |
|---|---|---|---|
| E190 reserve (`PS3_SPU_RESERVE=0`) | não (3) | 0 | — |
| escalonamento (timeslice/usleep/tick OFF) | não (3) | 0 | — |
| runtime pre-M9 (`87ab535`) | não (11↔3) | 0 | — |
| cellVdec pre-M9 (`libs/codec@87ab535`) | não (11/3) | 0 | — |
| HEAD + ponte replay (E178) | **sim (0)** | **0** (WADs vão pelo natural) | — |
| `e130` (~14:25, ref) | sim (0) | **5** (movie_io host) | 20 169 344 |

**Conclusão:** o `e130` completava o 2.º filme SEM a ponte de replay (só a EOS) E roteava os WADs para o `movie_io`
host (que lia os 20 MB). No HEAD: (a) o 2.º filme só completa com a ponte de replay (E178), e (b) essa ponte deixa os
WADs no caminho NATURAL (não movie_io), onde não são lidos. Reverter cellVdec/reserve/escalonamento **não** repõe (a).
Logo o bloqueio de link 1 é uma **regressão multi-factor cross-repo** (ps3recomp + gow2-recomp: `movie_eos_arm.c`,
hooks, a EOS/overlay-done do 2.º filme), de uma árvore de trabalho de ~14:25 com mudanças por commitar em ambos —
não há um commit único a reverter.

**Caminho definitivo (o único que resta, medido):** reconstruir a árvore cross-repo de ~14:25 (o `e130`), OU, mais
sustentável, tornar a ponte de replay compatível com o routing dos WADs para o `movie_io` host: quando a intro completa
pela ponte, garantir que o hook de open (`patch_2b3d1c_movie_io`) continua a servir os WADs pelo `movie_io` (como no
`e130`) em vez de os deixar no natural. Isso repõe `bytes_read=20MB` → loader ao estado 22 → o portão do §4v →
construção do nível → registry [D]/[E]. Todos os elos estão medidos e localizados (§4b–§4x); este é o passo de
integração que os junta no HEAD.


## §4z Link 2 (RETRACTADO por §4aa/E256) — o wait do dearchiver era do HEADER do psarc, não do R_PermA; o filme não compete

> **RETRACTADO (E256).** Os `[FIOSOP] wait lr=0x30D94C` medidos eram do dearchive do HEADER do gow2.psarc (~14 inflates
> reais do TOC), ANTES do open do R_PermA (linha 6605). O R_PermA abre DEPOIS do filme, não compete com ele. Sobrevive
> a medição (os 14 inflates são o TOC; `lr=0x30D94C` = `FUN_0030d7d8`, o dearchive do header); cai a conclusão "o filme
> estrangula o R_PermA". Ver §4aa para o mecanismo real.

### (texto retractado) Link 2: o dearchiver pára num wait FIOS porque o 2.º filme ainda corre (E255)

Trace do dearchiver natural (HEAD + ponte replay): o inflate host **descomprime blocos REAIS do psarc** do R_PermA —
`csz=137→90→47340→58624→61448`, `out=65536`, `copied=65492/65536` (blocos de 64 KB de verdade, não só o header) — ~14
blocos. Depois **pára** num `[FIOSOP] wait r3=0 op=0 tipo=0 status=0 fh=0 lr=0x0030D94C` (o dearchiver a esperar por uma
leitura FIOS que não chega). E o log mostra a CAUSA: **imediatamente a seguir** aparecem `[cellVdec] Open` +
`StartSeq(handle=0) -> async decode thread` e `[INTROSEQ] CE03C arm +0x714 st=10` — ou seja, **o 2.º/3.º filme da
intro AINDA está a correr** (a ponte de replay mantém-no vivo) e compete com o dearchiver do WAD pelos ops FIOS. A
leitura seguinte do R_PermA não completa porque o filme está a usar o FIOS.

No `e130` o filme terminava (`st620→0`) ANTES do load dos WADs (sem ponte de replay), logo não havia competição e o
R_PermA lia os 20 MB. A ponte de replay (E178) resolve o `st620` mas mantém o decode/FIOS do filme activo — o efeito
colateral que estrangula o dearchiver.

**Fix de link 2 (concreto):** a ponte de replay tem de PARAR o filme (o decode e a actividade FIOS) quando arma o EOS,
não só marcar `st620`. Ou seja: no `movie_vt_replay_from_cache`/no arm do EOS, chamar o `movie_vt_stop` + o
`MovieStop`/close do guest de forma a libertar o FIOS antes do load do nível. Alternativa fiel: fazer o 2.º filme dar
`SEQDONE` natural (M9) para o guest o fechar sozinho. Com o FIOS livre, o dearchiver do R_PermA lê os 20 MB → loader ao
estado 22 → §4v → construção do nível → registry [D]/[E]. `lr=0x0030D94C` é o sítio de espera do dearchiver a
instrumentar/comparar com a consola.


## §4aa Link 2 (PARCIALMENTE CORRIGIDO por §4ab/E258) — early-cancel medido; a conclusão "FIOS op freelist" cai

> **CORRIGIDO (E258).** Sobrevive: o R_PermA abre, a op sofre `42B4-CANCEL-YIELD`, e o loader deref pointers TAGGED
> `0x84000017` + `FREELIST-TAG-GUARD 263040 abort`. CAI a inferência de que era o **FIOS op freelist** (`media+0x200`)
> a esvaziar: o E257 (reseed no STREAM-OPEN) **não disparou** (`head!=0` no open) e **não mudou** o tag-guard (8→8).
> `func_00263040` é a **CHUNK ARENA** (o alocador de memória), não o freelist de ops FIOS. Ver §4ab.

### (texto corrigido) O stream do R_PermA é early-cancelado no open, e o loader deref pointers TAGGED (E256)

Medido a seguir ao open do R_PermA (`FIOSOP submit tipo=0x9 path='/wad/r_perma.wad_ps3'`, op `0x43009760`, linha 6605):

```
[STREAM-OPEN] op=0x43009760 r31(stream?)=0x008695D4 fh=0x43018528
[FIOSOPEN] 42B4-CANCEL-YIELD after DONE early cancel        <- a op e' CANCELADA no early-DONE
[STREAM-RELEASE] stream=0x008695D4 fh(+4)=0x43018528 op(+8)=0x00000000   <- stream sem op
[CLOSE-ENTRY] ... freelist(+0x200)=0x43009840 ...           <- op devolvida a' freelist
[FIOSOP] done op=0x43009840 tipo=0x0 status=0                <- fechada como tipo 0
[vm] UNCOMMITTED read32 access 0x84000017 (+4) ra=func_002ACBE8+0x174    <- POINTER TAGGED
[vm] UNCOMMITTED write32 access 0x84000017 (+4) ra=func_002ACD38+0x114
[vm] UNCOMMITTED write32 access 0x84000013 (+4) ra=func_002AB0E4+0x270
[FREELIST-TAG-GUARD] 263040 entry head=0x00000000 -> abort r3=0          <- freelist vazia/corrupta
```

Ou seja: o R_PermA **abre**, mas o seu **stream é early-cancelado** (`42B4-CANCEL-YIELD after DONE early cancel`,
`patch_fios_42b4_cancel_yield`) e libertado sem op (`STREAM-RELEASE op+8=0`); a seguir o loader deref **pointers
TAGGED** — `0x84000017`, `0x84000013`, `0x8000000C`, `0x80000008` (o bit alto marca um handle/tag envenenado, a classe
do "pool de handles" do CLAUDE.md) — via `func_002ACBE8`/`func_002ACD38`/`func_002AB0E4`/`func_002B2AB0`, e o
`FREELIST-TAG-GUARD` (FUN_263040) aborta com `head=0` (freelist vazia). **É por isto que o R_PermA nunca faz stream**:
não é o filme (§4z retractado) — é o stream a ser early-cancelado no open e o processamento seguinte a bater em
tagged-pointer/freelist corrompida.

**Próximo passo (o fix de link 2, agora sobre o alvo certo):** (1) porque é que o open do R_PermA cai no early-cancel
do `func_002B42B4` (o DONE-poll cancel) — comparar com a consola (o open deve completar sem cancel); (2) de onde vem o
`0x84000017` (tagged) que `func_002ACBE8+0x174` lê logo a seguir — é o stream/handle do R_PermA a ser lido de um slot
não-committado. Corrigido o early-cancel, o R_PermA lê os 20 MB → loader ao estado 22 → §4v → registry [D]/[E].


## §4ab O REAL alvo de link 2: a CHUNK ARENA (alocador) corrompe no HEAD — o "pool de handles" reaberto (E258)

Retracção do E257 (reseed do FIOS op freelist no STREAM-OPEN): **não disparou** (`FREELIST-REBUILD-STREAMOPEN`=0 — a
`media+0x200` não estava vazia no open) e **não mudou** o `FREELIST-TAG-GUARD` (8→8). Removido. A confusão: `FUN_263040`
(o tag-guard) é o **find-fit da CHUNK ARENA** (o alocador de memória do jogo), NÃO o freelist de ops FIOS
(`media+0x200`). São estruturas diferentes.

Medido: a chunk arena corrompe-se no HEAD e não no `e130`:

| | FREELIST-TAG-GUARD abort | ALLOC-NULL-GUARD | FREELIST-REBUILD (op FIOS) |
|---|---|---|---|
| e130 | **0** | 0 | 1 (MovieStop) |
| HEAD (nat) | **8** | 1 | 0 |

O head da arena vem TAGGED — `0x84000005`, `0x84000017`, `0x84000013` (bit31 set, base `0x04000000`+índice pequeno) e
`0x8000000C`/`0x80000008` — a forma boundary-tag do **"pool de handles"** (a lição de referência do CLAUDE.md, o
megaprojeto do deref taggeado→alocador→free-list→cursor→grow→fallthrough `func_002550C8`). O fix da raiz desse
megaprojeto — o fallthrough `func_002550C8 → func_00255178` — está **intacto e idêntico** nos dois lifts (v2 e e162,
verificado linha a linha), logo **não é essa a causa desta reabertura**. É outra fonte de tagged-pointer na arena, a
caracterizar pela via `walking-a-corruption-upstream` (capturar input+output no mesmo call-site, contar a população,
descodificar a estrutura), a partir de `func_002ACBE8+0x174` (lê `0x84000017`) e do `FUN_263040` (head tagged).

**O `FREELIST-TAG-GUARD` (patch_263040) é um PALIATIVO** (aborta com `r3=0` quando o head é tagged) — mascara a
corrupção a montante; não é o fix. **Próximo passo (o walk da corrupção, a montante):** call-site que produz o
`0x84000017` que `func_002ACBE8` lê logo após o early-cancel do R_PermA; contar a população (quantas allocs sãs vs a
tagged) e comparar campo a campo com a consola. Achado o produtor do tag na arena, o loader deixa de abortar → estado
22 (§4v) → registry [D]/[E].


## §4ac Upstream, nível 1: o tag bit31 é VÁLIDO (marcador), mas `func_002ACBE8` deref sem o mascarar (E259)

Método `walking-a-corruption-upstream`, primeiro nível. `FUN_002ab0e4`:
```
iVar1 = FUN_002acbe8(param_1 + 1);
if (iVar1 != 0) { uVar3 = *(uint*)(iVar1 + 4) & 0x7fffffff; ... }   // MASCARA bit31
```
Ou seja, **o bit31 é um marcador legítimo** (free/pending) da estrutura, e o código correcto mascara-o
(`& 0x7fffffff`) antes de usar o valor. Mas o acesso UNCOMMITTED medido é
`read32 access 0x84000017 ra=func_002ACBE8+0x174` — `func_002ACBE8` faz `puVar6 = *param_1` e **deref o valor
`0x84000017` COMO PONTEIRO sem mascarar o bit31** → acesso a `0x84000017` (não-committado). O valor `0x84000017` =
`bit31 (tag) | 0x04000000 (base) | 0x17 (índice)` — um handle com o tag ainda ligado a ser desreferenciado.

Nível seguinte (por medir, com o método): capturar no call-site de `func_002ACBE8` o `*param_1` — quantos são
ponteiros limpos vs quantos vêm com bit31 (a população localiza o defeito) — e subir até quem **aloca o handle e
deixa o tag ligado** (o produtor). É a assinatura do "pool de handles": um handle com o marcador de free/pending a ser
usado como ponteiro. O guard `patch_263040` (que aborta) e o `& 0x7fffffff` de `FUN_002ab0e4` são consumidores que
lidam com o tag; o produtor que o deixa ligado no caminho de `func_002ACBE8` é o alvo.

Estado: link 2 tem a corrupção localizada ao deref sem-máscara em `func_002ACBE8` (nível 1 do walk); o produtor a
montante (o alocador/registo do handle no caminho pós-early-cancel do R_PermA) é o próximo nível. Isto é o megaprojeto
"pool de handles" reaberto — a via de resolução é a documentada (`walking-a-corruption-upstream`), não um guard.


## §4ad Upstream, níveis 2-3: o deref tagged é DENTRO do alocador (FUN_002ac2f8→FUN_263554); o lift do alocador é idêntico (E260)

Walk continuado. `func_002ACBE8+0x174` = `0x2ACD5C` = o **return address** a seguir a `bl 0x2AC2F8` — logo o acesso
UNCOMMITTED `0x84000017` acontece **DENTRO de `FUN_002ac2f8`** (que `func_002ACBE8` chama para alocar 16 B).
`FUN_002ac2f8 → FUN_00263554(*0x53fbf0)` = o find-fit da **chunk arena** (a família do alocador `263xxx`: 263554,
263040, 262808, 262610, 263178). Ou seja, o deref do handle tagged é a arena a percorrer a sua **free-list com um link
tagged** (`0x84000017` = free-flag bit31 + base 0x04000000 + índice).

Diff do lift do alocador **v2 (e130) vs e162 (HEAD)**: `FUN_263554`, `FUN_263040`, `FUN_002AC2F8` **IDÊNTICOS**;
`FUN_262808` difere só por um PROBE (`E123-ARENAGET`, diagnóstico, sem efeito). Logo **a reabertura NÃO é uma
diferença de lift no alocador**. O produtor é uma escrita a montante que mete um link tagged na free-list da arena
(um free errado, um overrun para dentro da arena, ou um handle libertado com o free-flag e re-encadeado) — encontra-se
com a `walking-a-corruption-upstream` (capturar no call-site quem escreve o head da free-list da arena com bit31;
contar a população sã vs tagged), não lendo mais código do alocador.

**Estado:** o blocker de [D] (link 2) está caracterizado até ao nível do alocador — a arena tem um link tagged na
free-list e o lift do alocador é o mesmo do `e130`, logo o produtor é uma escrita a montante (não o alocador nem o
fallthrough 2550C8, ambos intactos). Isto é o megaprojeto "pool de handles" reaberto; a resolução é o walk dedicado
(níveis seguintes: watch do head da free-list da arena — `*0x53fbf0`+free-list — para apanhar o `who` que lá mete
bit31), como o que fechou esta classe antes (6 rodadas).


## §4ae Walk níveis 4-7 e CONCLUSÃO: o lift do pool de handles é idêntico; a corrupção é DATA-DRIVEN do WAD natural (E261)

Walk completo do handle-pool (com probe `[ARENA263554]` no binário que corrompe):
- **Nível 4:** há DUAS arenas. A principal (`0x4003ED40`) é sã (head da free-list a avançar `..D60/D70/D80`). Uma
  chamada de `lr=0x2AAE3C` passa **arena=0** a `FUN_263554` → `*(0+4)` = lixo → devolve `0x84000017` → `func_002ACBE8`
  escreve nele (`stw r31,0x4(r3)` a `+0x180`) → acesso UNCOMMITTED.
- **Nível 5-6:** `lr=0x2AAE3C` = `FUN_002aad94`, a **2.ª** chamada `FUN_263554(FUN_002abfa4(...))` — **sem null-check**
  (a 1.ª tem). `FUN_002abfa4` devolve `*(no+4) & 0x7fffffff` (mascarado) OU **0**; devolve 0 mesmo **depois do GROW**
  (`FUN_002ab8dc` +0x80 entradas). É o sintoma do megaprojeto ("free-list esgota → grow herda arena do objeto").
- **Nível 7 (decisivo):** o lift de TODO o subsistema — `FUN_002AB8DC`, `FUN_002AAAA0`, `FUN_002ACBE8`, `FUN_002ABFA4`,
  `FUN_002AAD94`, `FUN_263554`, `FUN_263040`, `FUN_002AC2F8` — é **IDÊNTICO** entre v2 (e130) e e162 (HEAD).

**Conclusão:** a corrupção do pool de handles **não é uma regressão de lift** (o subsistema inteiro é byte-idêntico ao
do `e130`) nem o fallthrough `func_002550C8` (intacto). É **DATA-DRIVEN**: o pool corrompe porque o **caminho NATURAL
do WAD** (early-cancel do R_PermA, §4aa) lhe entrega estado/dados errados. O `e130` chegou ao estado 22 **só porque
roteava os WADs pelo movie_io host** (o paliativo que o E90 removeu de propósito para ir natural, "sem atalhos"). Ou
seja, **o `e130` não é o baseline correcto a repor** — é o paliativo. O trabalho real de [C]/[D] é: fazer o **caminho
natural** do WAD (dearchiver psarc → edgezlib → loader) alimentar o pool de handles com o estado certo, para o pool não
esvaziar/corromper e o loader sair do estado 22. É a meta corrente do projeto, não uma regressão a reverter.

**Estado final (medido, honesto):** a cadeia inteira para [D] está caracterizada e localizada função-a-função (§4b-§4ae),
com o pool de handles walk-ado 7 níveis. O blocker não é um bug a reverter mas o **caminho natural do WAD a completar** —
o mesmo `walking-a-corruption-upstream`, agora com o alvo certo: porque `FUN_002abfa4` devolve 0 pós-grow no caminho
natural (o input do pool), comparado com a consola (`EBOOT_skipintro.BIN`, watch de `arena+4` e do input do `FUN_002acbe8`).


## §4af Nível 8, MEDIDO: `func_002ACBE8` devolve 0 no caminho natural (pool vazio pós-grow) (E262)

Probe executado (`[ABFA4]`) no binário HEAD que corrompe: `func_002ACBE8` (o node-getter do pool, chamado por
`FUN_002abfa4`) **devolve 0x00000000** no caminho natural do WAD — 2× confirmado — ao lado de retornos válidos
(`0x4003F070`). Da lógica de `func_002ACBE8`: `puVar6 = *param_1; if(puVar6==0){ if(*(0x53fbf0+8)==0){ alloc } }
return puVar6;` — devolve 0 quando **a cabeça da lista do pool (`*param_1`) é 0 E o estado da arena (`*(arena+8)`)
bloqueia uma alloc fresca**. O `FUN_002ab8dc` (grow, +0x80 entradas) **não está a repor a lista que o pool lê**: o
pool fica vazio, `func_002ACBE8`→0, `FUN_002abfa4`→0, e a 2.ª chamada sem null-check de `FUN_002aad94` faz
`FUN_263554(0)` → lixo tagged.

**Alvo do nível 9 (o produtor):** `*(arena+8)` (o gate da alloc) e a lista que o `grow` preenche vs a lista que
`func_002ACBE8` lê (`*param_1`, com `param_1 = objeto+0xc/+0x10`). Se o grow enche uma lista e o getter lê outra
(offset trocado), ou se `arena+8` fica não-zero a bloquear, o pool nunca reenche. Medir com watch de `*param_1` e
`arena+8` à volta do grow, comparado com a consola. Isto fecha o walk: reposto o reenchimento do pool no caminho
natural, `func_002ACBE8`≠0 → sem `FUN_263554(0)` → sem corrupção → loader sai do estado 22 → registry [D]/[E].

Medição executada, resultado registado: a corrupção do pool está confirmada como **pool-empty-pós-grow no caminho
natural** (não um lift errado, não competição, não o FIOS op freelist — tudo retractado). O trabalho é o reenchimento
natural do pool.


## §4ag CONVERGÊNCIA do walk (E263): `func_002ACBE8` é um LOOKUP por chave, não um free-list — a corrupção é sintoma da R_PermA não fazer stream

Lido `func_002acbe8` inteiro: é uma **árvore de lookup por chave** (BST/RB), não um pool/free-list.
`arena+8` = raiz da árvore; os links dos nós são ÍNDICES (`node+3`=dir, `node+0xa`=esq) convertidos a ponteiro por
`índice*0x10 + arena+4`; devolve o nó com `*nó == param_2` (a CHAVE) ou **0 quando a chave NÃO está na árvore**
(chega ao nil sentinela `*(arena+8)`). Ou seja: **não é "pool vazio pós-grow" — é "a chave não está registada"**
(retracta a formulação de §4af como "pool-empty"; sobrevive a medição de que `func_002ACBE8` devolve 0).

Isto **fecha o walk**: os 8 níveis de "corrupção do pool de handles" (arena tagged, `FUN_263554(0)`, `0x84000017`) são
**sintoma a jusante** de a árvore de lookup do loader não ter a chave — porque o **caminho natural do WAD nunca
registou os dados do R_PermA**, porque o **corpo do R_PermA (20 MB) nunca faz stream** (early-cancel a ~14 blocos, o
header do psarc; §4aa). A raiz ÚNICA de todos os sintomas (pool, arena, estado 22) é: **a leitura do R_PermA é
early-cancelada** (`42B4-CANCEL-YIELD after DONE early cancel`, o DONE-poll do guest `func_002B42B4`) e o corpo não é
lido → a construção do nível não regista as chaves → o lookup devolve 0 → `FUN_263554(0)` → lixo tagged → estado 22
eterno.

**O topo do walk (o único fix que resolve tudo a jusante):** porque é que o DONE-poll do guest (`func_002B42B4`)
cancela a leitura do R_PermA cedo (a ~14 blocos = só o header) quando na consola lê os 20 MB. Comparar no oráculo
(`EBOOT_intro.BIN`): o estado da op de leitura do R_PermA (op+0x40 tipo, o campo que o DONE-poll testa) no ponto do
cancel, nós vs consola. Reposta a leitura completa do R_PermA, tudo a jusante resolve: árvore registada →
`func_002ACBE8`≠0 → sem corrupção → estado 22 sai → registry [D]/[E].

Síntese honesta: o walk de 8 níveis do "pool de handles" **convergiu** para a mesma raiz de link 2 (§4aa) — a
R_PermA natural não faz stream. Não são dois problemas; é um. O fix é fazer o dearchiver natural ler o corpo do
R_PermA (não deixar o guest cancelar cedo).


## §4ah O TOPO do walk (E264): o poll cancela porque `op+0x90` (done-word) está set e `FUN_00306610` devolve cancel

`FUN_002b4224` (o DONE-poll do dearchiver):
```
if (*(container+8) == 0) return -(flags & 0x30000)>>0x1f;
if (*(op+0x90) == 0) return 0;              // op+0x90 = done-word; 0 = ainda a ler, continua
iVar2 = FUN_00306610(1, ...);               // check de conclusao
if (iVar2 == 0) { flags |= 0x10000; ... }   // OK
else if (iVar2 != -0x7ffef8f6 && iVar2 != -0x7ffef8fb) FUN_002b3cb0();  // CANCEL
```
Ou seja: o poll só cancela quando **`op+0x90` (done-word) está != 0** (a op marcada concluída) E `FUN_00306610`
devolve um status que não é OK/EAGAIN. Para o R_PermA, a op é marcada done cedo (a seguir ao header, ~14 blocos) e o
check cancela → `STREAM-RELEASE op+8=0` → corpo (20 MB) nunca lido.

**Raiz única, converged (E256→E264):** o `op+0x90` (done-word) da op de leitura do R_PermA fica != 0 antes de o corpo
ser lido, e `FUN_00306610` devolve cancel. Na consola o done-word só é set quando os 20 MB acabam. **É este o ponto a
medir/corrigir** — quem/como põe `op+0x90` != 0 cedo (o nosso serviço FIOS host/natural marca a op done a seguir ao
header), e o que `FUN_00306610` devolve, nós vs consola (oráculo `EBOOT_intro.BIN`, bp em `0x306610` e watch de
`op+0x90` da op do R_PermA). Reposto o done-word a 0 até ao fim do corpo, o poll não cancela → o R_PermA lê 20 MB → a
árvore de lookup do nível é registada → `func_002ACBE8`≠0 → sem `FUN_263554(0)`/corrupção → estado 22 sai → registry
[D]/[E].

**Toda a cadeia convergiu num só ponto:** 4 links, 8 níveis de walk do "pool de handles", tudo a jusante de
`op+0x90 != 0` cedo na op de leitura do R_PermA. Não são vários bugs; é um. O fix é o done-word/completion do R_PermA
no caminho natural.


## §4ai (RETRACTAÇÃO E266 REVERTIDA por §4ak/E267 — o disasm CONFIRMA o mecanismo) — cancel quando FUN_00306610 devolve ∉{0,0x8001070A,0x80010705}

> **~~RETRACTADO (E266)~~ — REVERTIDO (E267).** Probe em `FUN_00306610` (o binário que corrompe): para a op do R_PermA devolve **ret=0**
> (`op+0x90=1, op+0xcc=0, op+0x44=0`) — **NÃO 0x80010708**, e o poll `func_002b4224` nesse caminho conclui OK, não
> cancela. Confundi `func_002b4224` (o poll que li) com `func_002B42B4` (o site real do `42B4-CANCEL-YIELD`, função
> diferente). Sobrevive: `FUN_00306610` devolve `op+0xcc?:op+0x44` e o poll cancela se ∉{0,0x8001070A,0x80010705};
> cai: que fosse ESSE o caminho do cancel do R_PermA. Ver §4aj.

### (texto retractado) a op de leitura do R_PermA tem status 0x80010708 via FUN_00306610

`FUN_00306610(1, &op, ...)`: quando `op+0x90` (done-word) != 0, devolve `op+0xcc` (se != 0) senão **`op+0x44` (o
status)**. O DONE-poll `FUN_002b4224` **cancela** (`FUN_002b3cb0`) se esse valor **não** for `0`, `-0x7ffef8f6`
(=`0x8001070A`) ou `-0x7ffef8fb` (=`0x80010705`). Ou seja:

| `op+0x44` | significado | poll faz |
|---|---|---|
| `0x80010705` | **in-progress / EAGAIN** | continua a ler |
| `0x8001070A` | (continua) | continua |
| `0` | done OK | conclui |
| **`0x80010708`** | (outro) | **CANCELA** → `STREAM-RELEASE`, corpo não lido |

**Raiz única, medida e converged (toda a sessão):** a op de LEITURA do R_PermA fica com `op+0x44 = 0x80010708` a
seguir ao header (~14 blocos), o `FUN_00306610` devolve-o, o DONE-poll cancela, o corpo (20 MB) não é lido → a árvore
de lookup do nível não é registada → `func_002ACBE8`=0 → `FUN_263554(0)` → arena tagged → estado 22 eterno → registry
vazio [D]/[E]. Na consola o `op+0x44` da leitura em curso é `0x80010705` (in-progress) até ao fim do corpo.

**O fix (preciso e actionable):** garantir que a op de leitura do R_PermA no caminho natural mantém
`op+0x44 = 0x80010705` (in-progress) enquanto o corpo streama, e só `0` no fim — em vez de `0x80010708`. Medir quem
carimba `0x80010708` no `op+0x44` da op de leitura (o nosso HLE do aread/cellFsRead ou o `func_0030cd48`), watch de
`op+0x44` da op de leitura do R_PermA, comparado com a consola (bp `0x306610`). Corrigido o status, o poll não cancela,
o R_PermA lê os 20 MB, e toda a cadeia a jusante (§4a–§4ah) resolve-se — é UM fix, não muitos.

Toda a investigação da sessão (4 links, 8 níveis de walk do pool de handles, 10 retracções) **convergiu neste ponto
único**: `op+0x44 = 0x80010708` vs `0x80010705` na op de leitura do R_PermA.


## §4aj HONESTIDADE: convergência-e-refutação repetida; o cancel real é `func_002B42B4` (early-DONE), ainda a pinar (E266)

Duas "convergências finais" refutadas por medição nesta caça (§4z: competição com o filme; §4ai: `op+0x44=0x80010708`
via `FUN_00306610`). Padrão reconhecido (skill `retracting-your-own-findings`: "Five coherent stories were written and
refuted"). O que SOBREVIVE, medido:
- O R_PermA infla ~14 blocos REAIS do psarc e depois a op é **cancelada** (`STREAM-RELEASE op+8=0`, corpo 20 MB não lido).
- O site do cancel é **`func_002B42B4`** (`bl 0x30AE58` = `FUN_0030AE58(media, container+8)` cancela a op async, depois
  zera `container+8`) — o "early DONE path". NÃO é `func_002b4224`/`FUN_00306610` (aí a op devolve status 0, OK).
- Toda a corrupção a jusante (pool de handles, arena tagged, estado 22 eterno) é sintoma de o corpo do R_PermA não ler.

O que FICA por pinar (honesto): **porque é que o poll leva a op do R_PermA ao `func_002B42B4` (early-DONE cancel)
depois de ~14 blocos** quando na consola continua a ler. O caller de `func_002B42B4` e a condição que o escolhe (vs
continuar a ler o próximo bloco) é o alvo — a medir com o oráculo (`EBOOT_intro.BIN`, bp em `0x2B42B4` e no ramo que
lá chega, ver o estado da op/stream que o poll testa, nós vs consola).

Esta classe (o cancel prematuro da leitura streaming do R_PermA no caminho natural) é o megaprojeto [C]/[C2] reaberto
pelo caminho natural, e precisa do **push dedicado** (a skill diz 6 rodadas focadas para esta classe) — não do walk
disperso desta sessão. A groundwork está toda medida e localizada (§4a-§4aj); o próximo é o push focado no
`func_002B42B4` early-DONE com o oráculo lado a lado.


## §4ak REVERSÃO da retractação E266: o disasm de `func_002b4224` confirma §4ai; o probe E266 foi inconclusivo (E267)

Skill `retracting-your-own-findings`: "a retraction can itself be wrong... a refutation resting on an absence needs a
known positive in the same regime." O E266 retractou §4ai com base na AUSÊNCIA de cancel no probe — mas o probe só
apanhou **3 amostras**, todas de ops `done OK` (`ret=0`), e **nunca exercitou o ramo de cancel**. Ausência sem
positivo conhecido no mesmo regime não conta. **Reverte-se o E266.**

O disasm de `func_002b4224` (0x2B4288-0x2B42C0) **confirma §4ai** por construção:
```
bl 0x306610                  ; r3 = FUN_00306610(...)
xoris r0,r3,0x8001; cmpwi 0x70A; beq 0x2B4330   ; r3==0x8001070A -> CONTINUA a ler
cmpwi r3,0; beq 0x2B42EC                          ; r3==0 -> done OK
xoris r0,r3,0x8001; cmpwi 0x705; beq 0x2B4330    ; r3==0x80010705 -> CONTINUA (in-progress)
bl 0x2B3CB0 ; bl 0x30AE58                         ; senao -> CANCELA a op
```
Ou seja: `FUN_00306610` devolve `op+0xcc?:op+0x44`, e `func_002b4224` **cancela** se esse valor ∉ {0, 0x8001070A,
0x80010705}. `0x80010708` → cancela. **§4ai (a op de leitura do R_PermA com status 0x80010708 → cancel) mantém-se**;
o `func_002B42B4` (§4aj) é o MESMO ramo de cancel desta função, não uma função diferente (confundi o offset com uma
função à parte — §4aj corrigido aqui).

**Estado (medido, verificado por disasm):** o cancel do R_PermA é `func_002b4224` a ler `FUN_00306610` = `op+0xcc?:
op+0x44` e a cancelar por não ser um status de "continua". O **fix e a medição precisa**: capturar o status (`op+0xcc`
e `op+0x44`) da op de leitura do R_PermA **no momento do cancel** (probe melhorado: imprimir quando o ramo `bl 0x30AE58`
é tomado, não filtrar por status≠0), e confirmar que é `0x80010708` (vs `0x80010705` na consola). Corrigido o status
da leitura streaming para `0x80010705` até ao fim do corpo, o R_PermA lê os 20 MB → §4a-§4ah resolvem-se.


## §4al HONESTO (E268): o probe no `func_00306610` de 0x2B428C devolve 0 (sem cancel) — §4ai fica NÃO-CONFIRMADO, o cancel do R_PermA é por outro caminho

Probe executado após o `bl 0x306610` de `0x2B428C` (dentro de `func_002b4224`): **`ret=0` em todas as amostras**
(`op+0x44=0, op+0xcc=0`), 0 cancelamentos — o ramo `bl 0x30AE58` (o cancel) **não é tomado aqui**. Ou seja, o cancel
do R_PermA **não flui por este `func_00306610`**. Há **7 call-sites** de `func_00306610` no lift (0x2B3C80, 0x2B428C,
0x2B4638, 0x306840, 0x306938, 0x31C7C8, 0x31CC4C); instrumentei o errado.

**Correção honesta (retracting-findings):** §4ai/§4ak (a op de leitura do R_PermA com status `0x80010708` a cancelar
via `func_002b4224`/`FUN_00306610`) **fica NÃO-CONFIRMADO** — a medição no site que instrumentei refuta que seja ESSE
o caminho (ret=0). Sobrevive, medido e verificado por disasm: **o mecanismo geral** (`func_002b4224` cancela se
`FUN_00306610` ∉ {0,0x8001070A,0x80010705}) e que **o R_PermA é cancelado a ~14 blocos** (`STREAM-RELEASE`, corpo não
lido) → toda a corrupção a jusante. Cai: que o cancel seja o `func_00306610` de 0x2B428C.

**O próximo passo correcto:** identificar QUAL dos 7 `func_00306610` (ou qual ramo de `42B4-CANCEL-YIELD` /
`FUN_0030AE58`) é tomado para a op do R_PermA no cancel — instrumentar os outros 6 call-sites ou pôr o probe no
próprio `bl 0x30AE58` (o cancel), imprimindo a op e o status quando o cancel dispara. Com o oráculo (`EBOOT_intro.BIN`)
a ler o mesmo ponto na consola. Só então o status/trigger exacto fica pinado.

**Meta-honestidade (skill):** nesta caça do R_PermA-cancel houve 3+ ciclos de convergência-refutação (§4z refutado;
§4ai retractado; retracção revertida por disasm; agora §4ai não-confirmado por medição no site). A groundwork está
toda localizada e é sólida (a cadeia sintoma→corrupção→estado 22 está medida função-a-função), mas o **trigger exacto
do cancel do R_PermA continua por pinar** — é o push dedicado que a classe [C]/[C2] exige, com o oráculo lado a lado,
não mais walk disperso.


## §4am RETRACÇÃO da cadeia "leitura do R_PermA cancelada" (§4aa/§4ai/§4ak/§4al): as ops canceladas são OPENs (tipo 9, status 0), e NÃO HÁ read op do corpo (E269)

Probe no PRÓPRIO cancel (`bl 0x30AE58` em 0x2B42C0, com `gpr[4]`=`container+8`=op): as ops canceladas são
**tipo=0x9 (OPEN), `op+0x44=0`, `op+0xcc=0`** — status ZERO, limpeza NORMAL da op de open depois de concluir. **Não
é a op de leitura, não é `0x80010708`.** E, decisivo: **não existe nenhuma op de leitura (tipo 0xA) do corpo do
R_PermA** — o `[FIOSOP]` só mostra o open (tipo 9) e o close; nunca um read (tipo 0xA) dos 20 MB.

**Retracta-se:** §4aa/§4ai/§4ak/§4al na parte "a op de LEITURA do R_PermA é cancelada (pelo poll, status 0x80010708)".
**Medido falso:** as ops que o poll limpa são OPENs com status 0 (comportamento normal). **Sobrevive** (medido): o
R_PermA abre; ~14 inflates do HEADER/TOC do psarc; o corpo (20 MB) nunca faz stream; a árvore de lookup do nível fica
sem as chaves; a jusante a corrupção do pool e o estado 22 eterno. **Cai:** o mecanismo do cancel (não há read a
cancelar).

**A verdade medida e o alvo real:** o guest **abre o R_PermA e nunca PEDE a leitura do corpo** (nenhum tipo 0xA). Na
consola, depois do open + TOC, o guest lê os 20 MB. Em nós, para depois do TOC. Ou seja, o defeito está no
**processamento do TOC do psarc / a decisão de ler as entradas do R_PermA** — o guest lê o header, e não avança para
pedir os blocos do corpo. É aí que medir a seguir (o que o guest faz a seguir aos ~14 inflates do TOC: a lookup da
entrada no TOC, e porque não emite o read), com o oráculo lado a lado.

**Meta-honestidade (skill `retracting-your-own-findings`):** esta caça do "R_PermA" teve 4+ ciclos de
convergência-refutação (§4z; §4ai; reversão da retracção; agora a cadeia inteira "read cancelado"). Cada refutação
veio de MEDIÇÃO, não de reler código. O que fica sólido: a topologia sintoma→corrupção→estado 22 (função-a-função) e
que **o corpo do R_PermA nunca é PEDIDO** no caminho natural. O trigger não é um cancel — é a AUSÊNCIA do pedido de
leitura. Push dedicado: porque o guest não emite o read do corpo pós-TOC.


## §4an SÍNTESE (E270, verificável): `0x2BA888` = `FUN_002ba76c` = o handler do ESTADO 22 — "read do R_PermA", "corrupção do pool" e "loop 22" são UM sistema

O `[STREAM-RELEASE] lr=0x2BA888` está DENTRO de **`FUN_002ba76c`** — exactamente o handler do estado 22 (§4v,
`FUN_0003f2b0 → FUN_002ba76c`). Logo os três fios que persegui separadamente são **o mesmo sistema entrelaçado**:

`FUN_002ba76c` (estado 22) → `FUN_002b9f00` lê o R_PermA para um **stream de entrada** (`obj+0x1a8`) → o stream é
alimentado pelo **inflate edgezlib** (dearchiver do psarc) → à medida que lê, **aloca buffers pelo pool de handles**
(`FUN_002abfa4`/`FUN_263554`/arena) → quando o chunk não cabe, entra em **estado 3** (`obj+0x1cc=3`) à espera de
alloc. Medido: o inflate faz ~14 blocos reais e pára; o stream de entrada seca (`obj+0x1a8→+0x10 < 0x20`); o pool
corrompe na alloc (handle tagged, arena `FUN_263040`); o loop 22 nunca sai (`obj+0x1ac` não drena, `obj+0x1a8→+0x10`
nunca 0). A saída exige as três: input vazio + pool sem trabalho + não-estado-3.

**Causalidade (o nó honesto):** não está isolado se (a) o inflate pára porque o pool não aloca (a corrupção bloqueia a
leitura), ou (b) o pool corrompe porque o read/registo pára (a leitura incompleta corrompe), ou (c) um terceiro (o
estado 3 preso à espera de um buffer que a arena corrupta nunca dá). São interdependentes dentro de `FUN_002ba76c`.
Isolá-lo é o **push dedicado**: instrumentar `FUN_002ba76c` no estado 22 (o valor de `obj+0x1cc`, `obj+0x1a8→+0x10`, e
o retorno de `FUN_002abfa4`/alloc a cada iteração) e comparar com a consola (`EBOOT_intro.BIN`/`skipintro`) — ver qual
das três falha PRIMEIRO.

**Estado final honesto da sessão:** o sistema [C]/[D] está **inteiramente mapeado** (estado 22 = `FUN_002ba76c` lê o
R_PermA pelo inflate edgezlib e aloca pelo pool de handles; falha entrelaçada inflate-pára/pool-corrompe/estado-3).
Não cheguei a um único fix isolado — o nó causal precisa do push dedicado com o oráculo. As ~11 retracções (por
medição) eliminaram os caminhos errados (0x80010708, op+0x30, competição com filme, FIOS op freelist, read cancelado)
e deixaram o sistema real exposto. O `[D]` (registry populado, compiles do nível) **não foi alcançado**; a via honesta
para lá é destravar `FUN_002ba76c` no estado 22 — o inflate a completar o R_PermA e o pool a alocar sem corromper.


## §4ao ROOT MEDIDO (E271-E273): o input stream do estado 22 NUNCA é preenchido — a leitura entrega 0 bytes; o inflate está desconectado da read op

Medido (probe `[ST22]` em `FUN_002ba76c` + watch de `0x4007FCE0`):
- No estado 22: `obj+0x1cc(state)=1` (**NÃO 3**), `obj+0x1ac(worklist)=0`, `obj+0x1c4(pendalloc)=0` — **o pool está
  LIMPO aqui**, não bloqueia. (Isto REFINA §4an: a corrupção do pool não é o que trava o loop neste ponto.)
- `obj+0x1a8(instream)=0x4007FCD0`, `in+0x10(bytes)=0` — **o stream de entrada tem ZERO bytes**.
- Watch de `0x4007FCE0` (=`instream+0x10`, o byte-count): **só escrito =0** (por `func_002BAD78`/`func_002E1424`),
  **NUNCA não-zero** — o stream nunca é preenchido.

`FUN_002b9f00` (o reader do estado 22) chama `FUN_002b45d0(obj+0x64)` (gere a read op: retorna 0 enquanto a op está
`0x4000` pendente e não-done; 1 quando conclui) e copia `obj+0x1a4` bytes para o stream via `FUN_002e1254`. Como o
stream fica a 0, **`obj+0x1a4` (bytes lidos) = 0**: a read op do R_PermA **conclui a entregar 0 bytes**. O inflate
edgezlib produz os ~14 blocos reais (dst `0x4B000000`/`0x436xxxxx`), mas **esses bytes NÃO chegam ao buffer da read op**
do estado 22. **Inflate e read op estão desconectados.**

**RAIZ (medida, sobrevive):** o caminho natural do dearchiver não liga a saída do inflate edgezlib à op de leitura que
o loader do estado 22 usa — a leitura do R_PermA entrega 0 bytes, o input stream fica vazio, e o loop 22 gira sem dados.
Tudo o resto (a "corrupção do pool", o "cancel", o "read não pedido") é ruído a jusante ou noutra fase; o defeito
central é este: **0 bytes entregues ao estado 22**.

**O fix (concreto):** ligar a saída do inflate host/edgezlib (o que enche `0x4B000000`/…) ao buffer que a read op do
estado 22 lê (`obj+0x1a4`/`obj+0x1a8`=`0x4007FCD0`), OU fazer a read op natural (FIOS aread do psarc) devolver os
bytes inflados. Medir com o oráculo: na consola, quem enche `0x4007FCD0+0x10` e de onde (a read op ou o inflate).
Reposto o preenchimento, o estado 22 lê o R_PermA, a construção do nível corre, o registry popula e os compiles
disparam — [D]/[E].

Esta é a raiz mais limpa e menos refutável de toda a sessão (uma medição directa: o input stream nunca é preenchido),
depois de ~11 retracções eliminarem os caminhos errados. O `[D]` continua não alcançado, mas o defeito central está
agora medido, não inferido.


## §4ap AVANÇO EXECUTADO (E275-E281): o loader PROCESSA dados reais do R_PermA (via host); aberto o cursor do ring

Executado o fix medido de §4ao (ligar o inflate/host ao read do estado 22). Patch `patch_e275_wad_host_read.py`
(gated `PS3_WAD_HOST_READ`, OFF por defeito): o open natural do R_PermA SUCEDE (FO válida, `file_new r3!=0`) mas a
leitura natural entrega 0 bytes; o patch stampa o mfd movie_io na FO natural do WAD (`f2b_fo_mfd_put`), e com
`PS3_FIOS_STREAM_PUMP=1 PS3_F2B_HOST_FILL=1` o **`F2B-STREAM-FILL` passa a encher o input stream do estado 22**
(`0x4007FCD0`) com os bytes do R_PermA do cache. **Medido: o loader PROCESSA os primeiros 256 KB** — 28 linhas
`LOOKUP`/`ATTACH`/`MDLX`, `LFQRING` activo — a **primeira vez na sessão que o loader consome dados reais do WAD**
(antes: input stream sempre vazio, §4ao).

**Aberto (o último elo, medido):** o ring enche 256 KB (o `cap` do ring guest) e **pára** — o reader do WADLD
(`func_002B9C80`) **não avança o cursor `stream+8`** que o `f2b_stream_fill` segue, logo `avail` fica no `cap`,
`space=0`, o fill não reenche, e o loader **crasha** ao ler além dos 256 KB (`func_002B9C80`/`func_002BAC48`). O
per-read fill (`f2b_stream_pre_consume`) está gated pelo E90 (`f2b_natural_on`) E o seu call-site não cobre
`func_002B9C80`; chamar `f2b_stream_fill` a cada iteração de `func_002BA76C` (E281) não ajuda porque `avail==cap`
(nada foi consumido do ponto de vista do fill). **É um desalinhamento de cursor entre o reader do WADLD e o F2B fill.**

**Próximo (concreto):** alinhar o cursor — ou o reader (`func_002B9C80`) avança `stream+8` ao consumir (e o fill
recomputa `avail = write_pos - stream+8`), ou o fill segue o mesmo cursor que o reader usa. Reposto o refill contínuo,
o R_PermA entrega os 20 MB, o loader passa dos 256 KB, chega ao estado 22→16→... e o registry [D]/[E] popula. Este é o
avanço mais próximo de [D] da sessão: o loader já processa dados reais do R_PermA (host), falta só o cursor do ring.


## §4aq O elo final medido (E282): `type_sys+0x1D4` (rem_body) = 0 — o loader não pede o corpo do R_PermA

Medido: `[F2B-STREAM-ENSURE ts=0x00869570 st=0x4007FCD0 was_av=0 rem1D4=0x0 → …]`. O campo `type_sys+0x1D4`
(`rem_body` = bytes do corpo que faltam ler) é **0**, logo o hook de top-up usa `need = rem_body ? rem_body : 0x20` =
`0x20` (só um header) e o loader **nunca pede os 20 MB do corpo**. É por isto que o fill pára a 256 KB (o pump inicial)
e não continua: não há quem peça mais. Na consola (e no caminho F2B completo do `e130`) esse campo carrega o tamanho
do conteúdo e conduz as leituras do corpo.

**Estado final honesto da sessão:** o `PS3_WAD_HOST_READ` (E275) executou o fix medido — o loader passou a **processar
dados reais do R_PermA** (256 KB, 28 LOOKUP/ATTACH/MDLX), o mais perto de [D] da sessão. O último elo, medido, é
`rem_body=0`: o loader não sabe que tem de ler o corpo do WAD. Para completar: (a) descobrir onde `type_sys+0x1D4` é
posto (o parsing do header do WAD que o `e130` fazia via o caminho F2B completo, com o WAD movie_io-routed desde o
open) e garantir que fica com o tamanho do corpo; OU (b) replicar o caminho F2B completo do `e130` (WAD roteado para
movie_io no OPEN, não o mfd stampado tarde do E275) para que todos os hooks de streaming firem. Reposto o
`rem_body`/o streaming, o R_PermA entrega os 20 MB, o loader passa dos 256 KB, sai do estado 22, e o registry [D]/[E]
popula.

**[D]/[E] NÃO alcançado** (registry vazio, só FlashUI/Default compilam). Mas o caminho está executado até ao loader
consumir dados reais do WAD, e o elo restante é um campo concreto (`rem_body`) medido, não inferido.


## §4ar (RETRACTADO por §4as/E284) — hand-decode do header errado (direção do byteswap); medir, não decodificar à mão

> **RETRACTADO (E284).** O hand-decode deu `uVar16=0x08000000` e depois `8` (inconsistente) e NENHUM bate com a
> medição `rem1D4=0`. Decodificar endianness à mão é inferência. Ver §4as: medir `uVar16` com probe no parse.

### (retractado) hand-decode: uVar16=0x08000000

Decodificado o parse do estado 22 (`FUN_002ba76c` linha 55-73) sobre os bytes do R_PermA entregues pelo host:
- O cache `movie_cache/R_PermA.wad_ps3` começa com `15 00 00 00 | 08 00 00 00 | "WAD_R_Perm…"` — um **header de FICHEIRO
  WAD** (contagem + nome), não um chunk-header do stream do dearchiver.
- O loader lê 0x20 bytes para `obj+0x7c` (via `FUN_002e1480`), tira `uVar15 = byteswap(u16@0x7c) = 0x1500` (tag) e
  `uVar16 = byteswap(u32@0x80) = 0x08000000` (size). `0x08000000` = 128 MB — **não é um tamanho de chunk válido**;
  o loader põe `rem_body = 0x08000000`, entra em estado 3 à espera de 128 MB, e crasha.
- Ou seja: o estado 22 espera o **stream de CHUNKS que o dearchiver do psarc produz** (framing tag+size por bloco),
  não o ficheiro WAD cru extraído. Entregar o `R_PermA.wad_ps3` cru (o header de ficheiro) faz o parse ler lixo.

**Conclusão medida:** o `PS3_WAD_HOST_READ` (E275) entrega os BYTES certos do R_PermA mas no FRAMING errado (ficheiro
cru vs stream de chunks do dearchiver). O `e130` funcionava porque o seu caminho F2B completo (com o WAD movie_io-routed
desde o open) apresentava o stream com o framing que o loader espera — não o ficheiro cru. Para completar por host: o
fill/pump tem de entregar o stream **no framing de chunks do dearchiver** (ou o loader tem de saltar o header de ficheiro
e ler os chunks corretos). Pelo caminho NATURAL (a meta): o dearchiver do psarc tem de produzir esse stream de chunks
para o read do estado 22 — que hoje aceita o open mas entrega 0 bytes.

**[D]/[E] NÃO alcançado.** O avanço real e committado: o loader passou de girar num stream vazio a **processar 256 KB de
R_PermA** (E275). O elo restante, agora decodificado ao byte: o **framing** do stream (chunks do dearchiver vs ficheiro
cru), não a entrega de bytes. É o trabalho do dearchiver natural / da reprodução fiel do stream F2B do `e130`.


## §4as MEDIÇÃO (E284) do header: inconclusiva (dedup do probe); o §4ar fica retractado, o framing por medir

Probe `[HDR]` em `FUN_002ba76c` (o parse do estado 22): a única amostra capturada foi `obj+0x7c=0xFFFFFFFF
obj+0x80=0x00000000 state=1 rem_body=0 in_avail=0x0` — um estado INICIAL (input vazio, header buffer não-preenchido),
ANTES do fill. O dedup do probe (`obj+0x80` mudou) não apanhou os estados com dados. **Inconclusivo** sobre o que o
loader parseia dos 256 KB entregues. Sobrevive (medido): o `§4ar` hand-decode está errado (direção do byteswap; deu
`0x08000000` e depois `8`, nenhum bate com `rem_body=0`), retractado.

**O que fica sólido e committado desta sessão (sem inferência):**
- `PS3_WAD_HOST_READ` (E275): o loader passou de girar num input stream vazio a **processar 256 KB de R_PermA** (28
  LOOKUP/ATTACH/MDLX) — primeira ingestão de dados de WAD da sessão. Medido, committado, gated.
- O input stream do estado 22 (`0x4007FCD0`) era medido a 0 no caminho natural (E273); com o host fill passa a receber
  dados do R_PermA do cache.
- ~13 retracções por medição eliminaram os caminhos errados (0x80010708, op+0x30, competição com filme, FIOS op
  freelist, read cancelado, hand-decode do header).

**O elo restante (honesto, por medir com rigor — não hand-decode):** o loader lê um chunk-header do stream e o parse
não progride (crasha/estado 3). Falta MEDIR (probe sem dedup, a cada iteração, com o header cru em `obj+0x7c`) o que o
loader lê dos bytes entregues, e comparar com o stream que o dearchiver natural / o caminho F2B do `e130` produz. É
trabalho de push dedicado sobre o framing do stream do WADLD, não single-edits.

**[D]/[E] NÃO alcançado.** O avanço real e committado é o loader a ingerir dados reais do R_PermA; o framing correto do
stream (chunks do dearchiver) é o que falta, e a medição precisa de um probe por-iteração (não o dedup do E284).


## §4at (CORRIGIDO por §4au/E287) — o "size=0" era o campo errado; o loader LÊ os records certos e AVANÇA

> **CORRIGIDO (E287).** O probe RING (`FUN_002e1480`, base+cursor) mostra o loader a **consumir records válidos**
> (`b@cur=0xXX000080`, size 0x80 no byte baixo; cursor 112→240→384→…, avail a descer). O `size=0` do §4at era o campo
> `cur+4` (que É 0), não o size (no byte baixo). O loader NÃO fica preso — avança 19 records e depois CRASHA. Ver §4au.

### (corrigido) o loader lê size 0

Probe por-iteração (`[HDR2]`, sem dedup, só com `in_avail>0`): **`in_avail=0x3F4E0 state=2 rem=0 hdr7c=0x00160000
hdr80=0x00000000 base=0x40083D40`**. Ou seja, com os dados do R_PermA no ring:
- O loader **AVANÇOU do estado 1 para o estado 2** (processou o header de ficheiro, está a ler chunks) — mais longe do
  que alguma vez esteve na sessão.
- O ring está cheio (`in_avail ≈ 256 KB`) e o loader lê um chunk-header `hdr7c=0x00160000, hdr80=0x00000000` — **size = 0**.
- Com size 0 o loader não consome nada (o cursor não avança), fica em loop a ler o mesmo chunk de size 0.

**Interpretação (medida, não hand-decode):** os bytes entregues dão ao loader um chunk de size 0 na posição de leitura.
Não é falta de entrega (o ring tem 256 KB) — é **framing/alinhamento**: a posição/endianness do campo de size não bate
com o que o loader espera (ou o cache cru não é o stream de chunks do dearchiver que o estado 22 consome). O `e130`
apresentava o stream com o framing certo (chunks do dearchiver), por isso o size era != 0 e o loader avançava.

**Estado committado e sólido:** o `PS3_WAD_HOST_READ` (E275) levou o loader do estado 1 (input vazio) ao **estado 2 a
ler chunks do R_PermA** — o avanço mais concreto da sessão. O elo restante, agora medido ao byte: o chunk-header lido
tem size 0 (framing errado), logo o loader não consome e fica preso. Fix: apresentar o stream no framing de chunks do
dearchiver (o size no offset/endianness certos), pelo caminho natural (o dearchiver a produzir os chunks) ou replicando
o F2B do `e130`.

**[D]/[E] NÃO alcançado** (registry vazio). O avanço real: loader no estado 2 a ler chunks do R_PermA; o elo é o framing
do chunk (size=0 medido), não a entrega.


## §4au BREAKTHROUGH (E287): a CONSTRUÇÃO DO NÍVEL EXECUTA — 19 records do R_PermA processados; crash num handler (resource lookup)

Probe RING em `FUN_002e1480` (o leitor do ring) + stack do crash. Medido, com o R_PermA entregue (E275):
- O loader **consome records válidos do R_PermA**: `base=0x40083D40 cur=112→240→384→…→2672`, `avail` a descer
  (`0x3FF90→0x3F590`), cada `b@cur=0xXX000080` (tag `XX`, size `0x80`). **A construção do nível ESTÁ a correr** — o
  mais longe que o projeto alguma vez chegou.
- Ao 19.º record (`~2.6 KB` de 256 KB consumidos — **NÃO é falta de dados**) **crasha**. Stack:
  `func_000B71B8 → func_00041D5C → func_002BAC48 → func_002B9C80 → [dispatch] → func_002B0DB0 → [dispatch] →
  func_0039E794 → [indirect call CRASH]`.
- `func_002B0DB0` é o **dispatcher do registry**: `puVar1 = *(toc-0x1568)` (a tabela de handlers), `piVar2 =
  puVar1[type*4]` (o handler por tipo de record), depois chama `(*piVar2+0x14)()`. `func_0039E794` (o handler) faz o
  SEU lookup — `*(param_1[0x11]*0xc + param_1[9])` — e o vtable call `(*piVar3+0x14)()` crasha porque `piVar3` é lixo:
  o resource/handler que o record 19 pede **não está na tabela** (registry sem essa entrada).

**Significado:** isto É o mecanismo de [D]/[E] — a construção do nível a percorrer os records do R_PermA e a resolvê-los
pela tabela de handlers (o "registry"). Crasha num record cujo handler/resource não está registado. Não é falta de
entrega (256 KB disponíveis, 2.6 KB usados) nem framing (os records lêem-se bem, size 0x80). **CORRIGIDO (E288):** o probe `[REGDISP0DB0]` mede `handler=0x400D7338 (ok, VALIDO)` para o type 0x12 (idx 0x48) —
a tabela de handlers ESTÁ populada. A crash NÃO é registry incompleto; é **dentro do handler** `func_0039E794`, no
SEU lookup de RESOURCE (`param_1[9]`/`param_1[0x11]`). Ver §4av.

**Próximo (medido, concreto):** instrumentar `func_002B0DB0` (probe `[REGDISP]`, já no lift, gated `PS3_TRACE_REGDISP`)
para ver o `type` do record 19 e a entrada `puVar1[type*4]` (handler nulo/lixo), e `func_0039E794` para o
`param_1[0x11]`/`param_1[9]` (o resource table). Isso mostra QUAL tipo de record não tem handler — a lacuna exacta do
registry. Reposto o handler/resource desse tipo, a construção passa do record 19 e continua a popular o registry -> [D].

**Estado:** [D]/[E] ainda não alcançado (registry incompleto, crash ao record 19), MAS a construção do nível EXECUTA
pela primeira vez (19 records do R_PermA). O elo é a entrada em falta na tabela de handlers, localizada ao dispatch de
`func_002B0DB0`/`func_0039E794`.


## §4av (CORRIGIDO por §4aw/E289) — a tabela de handlers ESTÁ populada; o lookup de RESOURCE também RESOLVE (não é 0)

> **CORRIGIDO (E289).** O probe `[RESIDX]` mede `res[r3]` **VÁLIDO** (0x401002AC…, 6× "ok") — o resource RESOLVE, não
> é 0. Cai a inferência de que o lookup devolvia 0/NULL. A crash é mais fundo: no MÉTODO do resource. Ver §4aw.

Probes `[REGDISP0DB0]` (dispatch) e `[HANDLERLOOKUP]` (dentro do handler):
- `func_002B0DB0` (dispatcher): `tbl=0x00868D48 idx=0x48 (type 0x12) handler=0x400D7338 **ok/VÁLIDO**` — **a tabela de
  handlers ESTÁ populada** (o registry tem o handler do type 0x12). Retracta a inferência de §4au ("registry incompleto").
- `func_0039E794` (o handler chamado): `param1` válido (0x400D7338 e outros), `param1[9]` (resource table @+0x24) =
  VÁLIDO (0x400D7668…), `param1[0x11]` (idx @+0x44) = **0**. O handler faz `iVar1 = (*(*param1+0x4c))(param1)` (uma 1.ª
  vtable call que devolve um índice), depois `iVar1 = *(iVar1*4 + *(param1[0x11]*0xc + param1[9]))`, `piVar3 = iVar1-4`,
  e a 2.ª vtable call `(*piVar3+0x14)()` **crasha** — `piVar3` é lixo porque o índice `iVar1` da 1.ª vtable call
  aponta fora do resource table.

**Significado (medido):** a construção do nível EXECUTA e o registry de handlers está populado; a crash é 3 níveis
fundo, no **sistema de resources** do handler (a 1.ª vtable call de `func_0039E794` devolve um índice que resolve um
resource inexistente/não-registado). É o sistema de recursos do nível (CGOWShader/resource pool), não o dispatch.

**Próximo:** instrumentar a 1.ª vtable call de `func_0039E794` (o índice `iVar1`) e o conteúdo do resource table
(`0x400D7668`) para ver que resource o record 0x12 pede e porque o índice está fora. Provavelmente um resource que um
record anterior devia ter registado (ou de outro WAD) — a lacuna do pool de resources, não do registry de handlers.

**Estado:** [D]/[E] não alcançado, MAS a construção do nível EXECUTA (19 records do R_PermA, registry de handlers
populado) e a crash está localizada ao resource lookup de `func_0039E794` — o mais fundo e o mais perto de [D] do
projeto. Elo: o pool de resources (índice fora de alcance na 1.ª vtable call do handler type 0x12).


## §4aw MEDIDO (E289): o resource RESOLVE; a crash é no MÉTODO do resource (`func_0039F520`), ainda mais fundo — a construção do nível progride

Probe `[RESIDX]` na 1.ª vtable call de `func_0039E794`: `r3=0 i11=0 res[r3]=0x401002AC… (6× ok)` — **o resource é
encontrado, válido**. Retracta §4av ("resource 0/NULL"). A construção do nível **resolve os resources**. O crash move-se
mais fundo: stack `… func_002B0DB0 → func_0039E794 → func_0039D6F0 → func_0039F520+0x1424 → CRASH`. Ou seja, o handler
resolve o resource e chama o **método do resource** (`func_0039D6F0 → func_0039F520`), que crasha. `func_0039F520`/
`func_0039D6F0` **não estão no dump do Ghidra** (métodos de resource fundo, região não-decompilada).

**Padrão medido (importante):** a cada probe/run a crash **move-se mais fundo** (E288: indirect call de `func_0039E794`;
E289: `func_0039F520+0x1424`) — a construção do nível PROGRIDE e resolve mais de cada vez. Não é um ponto fixo; é o
sistema de resources/shaders do nível a executar e a crashar em métodos fundos.

**Estado (o mais perto de [D] do projeto, medido):** a construção do nível EXECUTA — consome records do R_PermA,
despacha por uma tabela de handlers POPULADA, RESOLVE os resources (RESIDX ok), e chama os métodos dos resources. A
crash está nos métodos de resource fundos (`func_0039F520`, fora do dump). [D]/[E] ainda não (registry de shaders vazio,
sem compiles de nível), mas o mecanismo de construção do nível está a correr, três+ níveis fundo.

**Próximo (ongoing, dedicado):** debug do crash em `func_0039F520+0x1424` (fora do dump Ghidra — liftar/decompilar a
região 0x39F520, ou probe do ponteiro/indirect que crasha lá). Cada resource method resolvido aproxima do compile de
shader do nível. É o trabalho de construção do nível, agora a EXECUTAR (não bloqueado a montante como toda a sessão
assumiu até E275).


## §4ax MEDIDO (E290): a crash é `stuck calling 0x4BFFFE34` — indirect call a um ponteiro GARBAGE na cauda não-inicializada do buffer HOSTRES

O FATAL não é segfault: **`[ppu] FATAL: stuck calling 0x4BFFFE34 (2000 times) -- aborting run`**. Contexto no dump:
`r3=0 r11=0x4003ED70 lr=0x002B0DDC`, host_ra `func_0039D6F0 → ps3_indirect_call → func_0039F520`. O alvo `0x4BFFFE34`
está na **região do buffer de decode HOSTRES** (`[sys_memory] allocate_from_container -> 0x4B000000`, chamado repetido
para cada recurso), a `0x4B000000 + 0xFFFE34` — perto do fim do buffer de 1 MB. Os recursos decodificados têm
`outlen < 0x71000` (`scep_e.ctxr`=460928, `movie.cfx`=54512, `default.cfx`=16172), logo `0xFFFE34` está na **cauda
NÃO-INICIALIZADA** do buffer. O ponteiro de função (`*(*piVar3+0x14)`) que a construção do nível chama é lixo dessa
cauda.

**Significado:** um OBJETO de recurso do nível tem um vtable/método a apontar para memória não-inicializada do buffer
HOSTRES → `ps3_indirect_call` não resolve (não há função liftada lá) e faz loop 2000× → FATAL. Os recursos EM CAUSA são
os `*.cfx`/`*.ctxr` do nível (efeitos/texturas — o sistema CGOWShader), que a construção do nível decodifica (HOSTRES) e
processa. É um bug de **construção do objeto de recurso** (vtable não preenchido / relocação em falta / o objeto lê de
`0x4B000000` depois de um decode seguinte reescrever o buffer), não um bloqueio a montante nem falta de dados.

Retracta a inferência de §4aw de que a crash era um "método de resource fundo" genérico e que o `ra` da stack
(`func_0039F520+0x1424`) apontava o sítio — o `ra` do host é trampolim (CLAUDE.md) e `0x3A0944` é um `cmpwi`, não uma
chamada. O sítio real é o **indirect call a `0x4BFFFE34`** (medido pelo FATAL).

**Próximo (concreto):** ver de onde o objeto tira o vtable `0x4BFFFE34` — `piVar3=res[r3]-4` (res válido 0x400D788C),
`*piVar3` = vtable. Se `*piVar3` aponta para `0x4B...` (o buffer) em vez de código guest (`0x00...`), o objeto foi
construído com um ponteiro para o buffer HOSTRES (que é reutilizado/não-inicializado). Watch de `*piVar3` do objeto
0x400D7888, e ver se o decode HOSTRES seguinte reescreve os dados que o objeto referencia. Corrigido, a construção do
nível passa deste recurso e continua a popular o registry -> compiles de shader do nível [D]/[E].

**Estado:** [D]/[E] não alcançado, MAS a construção do nível EXECUTA e a crash está agora num **ponto único e concreto**
(indirect call a `0x4BFFFE34`, cauda do buffer HOSTRES) — um bug de construção de objeto de recurso, muito mais próximo
e mais preciso do que "bloqueado a montante".


## §4ay MEDIDO (E291): a cadeia de vtable ESTÁTICA é VÁLIDA (→0x0039D670); a crash é um objeto ESPECÍFICO corrompido em runtime

Watch do objeto de recurso (`0x400D7888`): o campo vtable é escrito com valores VÁLIDOS por `func_002BE560`
(`0x517688`, `0x511628`, … — no segmento de dados do EBOOT). Cadeia estática lida do EBOOT:
`*(0x517688)=0x005300D0`, `*(0x517688+0x14)=*(0x51769C)=0x0051B160`, `*(0x0051B160)=0x0039D670` — **código guest válido**.
Logo o dispatch de vtable FUNCIONA para os objetos que capturei (RESIDX 6× ok; a cadeia resolve para `func_0039D670`).

**Conclusão (medida):** a crash `stuck calling 0x4BFFFE34` **NÃO é sistemática** — é um objeto ESPECÍFICO, numa invocação
POSTERIOR de `func_0039E794` que os meus probes (RESIDX cap 20, watch cap 200) não apanharam, cujo `*piVar3` (vtable) ou
a cadeia de método está corrompida em runtime para `0x4BFFFE34` (cauda do buffer HOSTRES). Os objetos iniciais têm a
cadeia certa; um objeto tardio não.

**Próximo (concreto, para isolar):** instrumentar `func_0039E794` no PONTO do 2.º vtable call (antes do
`ps3_indirect_call`), imprimir `piVar3` e o método resolvido `*(*(piVar3)+0x14)` SÓ quando o método ∉ código guest
(`>= 0x4F000000` ou `< 0x10000` ou na região `0x4Bxxxxxx`), para apanhar o objeto corrompido e o seu `piVar3`. Depois
subir: quem construiu esse objeto (que record/resource) e porque o seu vtable/cadeia aponta para o buffer HOSTRES.
Provavelmente um resource cujo objeto foi construído a apontar para os dados decodificados em `0x4B000000` (que são
reutilizados/sobrescritos pelo decode seguinte) em vez de para uma cópia persistente.

**Estado:** [D]/[E] não alcançado, MAS a construção do nível EXECUTA, o dispatch de vtable funciona para os objetos
iniciais (cadeia → `func_0039D670`), e a crash está isolada a um objeto de recurso ESPECÍFICO com vtable/cadeia
corrompida para o buffer HOSTRES — um bug de construção de UM resource, não sistemático. É o ponto mais preciso e mais
perto de [D] do projeto.


## §4az MEDIDO (E292): 5 constructs de SHELL type-15 (CGOWShader) SUCEDEM; o 6.º (idx 0x12) crasha — objeto capturado

Probe pré-existente `[TYPE15SHELL]` (type 15 = o construct de shell/shader, `func_0039E794` — o caminho CGOWShader):
- **5 shells constroem OK** (`post#1..#5`, this=0x401002A8/0x400FE108/0x400FDB88/0x400D82D8/0x40301D38, idx word[1]
  =0x0C/07/08/06/1B). O construct de shader EXECUTA e completa 5×.
- O **6.º crasha**: `pre#6 this=0x400D7888` **sem `post#6`**. Objeto: `words: 00517688 40120012 0…0 00000080 00000080
  FFFFFFFF …`. word[0]=vtable **0x00517688 (VÁLIDO**, cadeia → `0x0039D670`); word[1]=**0x40120012** (idx 0x12).
- A crash (`stuck calling 0x4BFFFE34`) é DENTRO do método do construct (`func_0039D670 → …`), não no vtable do objeto
  (que é válido, como os 5 que passaram).

**Significado (medido, o mais perto de [D]):** o sistema de construct de shells/shaders (CGOWShader) ESTÁ a correr — 5
shells construídos com sucesso — e falha no 6.º (idx 0x12). Não é bloqueio a montante nem framing nem registry vazio de
handlers; é UM shell/shader específico (idx 0x12) cujo construct crasha num indirect call a `0x4BFFFE34` (cauda do buffer
HOSTRES). Os 5 anteriores (idx 0x0C/07/08/06/1B) constroem bem.

**Próximo (concreto):** o construct do shell idx 0x12 difere dos 5 que passam — comparar o que `func_0039D670` faz para
idx 0x12 vs 0x0C (o resource/dados que idx 0x12 referencia e que dá `0x4BFFFE34`). Provavelmente o resource de idx 0x12
(um .cfx/.ctxr específico) tem um ponteiro para o buffer HOSTRES reutilizado, ou o seu decode HOSTRES foi sobrescrito
por um decode seguinte antes do construct. Watch do resource de idx 0x12 e do buffer HOSTRES à volta do seu construct.

**Estado:** [D]/[E] não alcançado (registry de shaders de nível ainda vazio — o crash trava antes de completar), MAS o
**construct de shells CGOWShader EXECUTA e completa 5×** — a prova mais forte da sessão de que a construção do nível
popula o sistema de shaders; falta só o 6.º shell (idx 0x12) não crashar. É o ponto mais próximo de [D] alguma vez
medido no projeto.


## §4ba (RETRACTADO por §4bb/E294) — `func_002BE75C` NÃO é a origem de 0x4BFFFE34 (medido: +4=0x40000000)

> **RETRACTADO (E294).** Probe `[BE75CDATA]`: `func_002BE75C` é chamado com `param2=0` e computa `+4=0x40000000`, **não**
> `0x4BFFFE34`. Logo NÃO é este construct que produz o ponteiro garbage. A origem de `0x4BFFFE34` fica por pinar. O
> resto de §4ba (a hipótese de reuso do HOSTRES) é inferência não confirmada.

Diff estático das vtables shell #6 (idx 0x12, 0x517688) vs #1 (idx 0x0C, 0x5162D8): iguais em +0x68/+0x58/+0x14;
diferem em **+0x4C** (#6 → `func_002BE75C`; #1 → `func_00422CC8`). `func_002BE75C` (o construct específico do shell
idx 0x12):
```
puVar6 = FUN_00263b70(0x48);              // aloca o objeto
*puVar7 = *param_2 & 0xfffffff | 0x40000000;  // +4 = normaliza um ponteiro dos dados p/ 0x4xxxxxxx
puVar6[5] = 0xdeadbeef;                    // sentinela
*puVar6 = <vtable>;
```
`*param_2` (os DADOS do resource do shell) contém `0x4BFFFE34` (ou `0x0BFFFE34`); `& 0xfffffff | 0x40000000` re-normaliza
para `0x4BFFFE34`. Esse valor cai na **cauda reutilizada/não-inicializada do buffer HOSTRES** (`0x4B000000`, que
`allocate_from_container` devolve SEMPRE igual — bump não avança). Depois é chamado como função → `stuck calling
0x4BFFFE34`.

**Root (medido, isolado):** o resource do shell idx 0x12 carrega um **ponteiro interno** (`0x4BFFFE34`) que assume o
LAYOUT DE MEMÓRIA da consola (resources em endereços 0x4xxxxxxx distintos/contíguos). Em nós, os decodes HOSTRES
partilham `0x4B000000` (reuso), logo esse ponteiro aponta para lixo. Os 5 shells anteriores (idx 0x0C/07/08/06/1B) não
dependem desse ponteiro (o +0x4C deles é outro método). É **relocação de resource / addressing do buffer HOSTRES**, não
o registry nem o dispatch.

**Fix (concreto):** (a) `allocate_from_container` dar endereços DISTINTOS por resource (bump real) para os offsets/
ponteiros internos baterem; OU (b) relocar os ponteiros internos do resource no load (somar a base real); OU (c) copiar
cada decode HOSTRES para uma zona persistente distinta antes do próximo decode. Medir: `*param_2` do shell idx 0x12
(o ponteiro cru no resource) e o layout que a consola espera (offset base dos resources).

**Estado:** [D]/[E] não alcançado, MAS o construct de shaders CGOWShader EXECUTA (5 shells OK) e o 6.º está isolado a um
**ponteiro interno de resource + reuso do buffer HOSTRES** — root preciso, concreto e fixável (relocação/addressing),
não bloqueio a montante. O ponto mais fundo e mais acionável do projeto.


## §4bb HONESTO (E294): a origem exacta de 0x4BFFFE34 NÃO está pinada; várias hipóteses refutadas por medição

Estado honesto após medição direta: `func_002BE75C` (a hipótese de §4ba) computa `+4=0x40000000` com `param2=0` — NÃO
produz `0x4BFFFE34`. Nesta caça do crash do shell idx 0x12 houve várias hipóteses sobre a origem de `0x4BFFFE34`, todas
refutadas por medição (o vtable do objeto é válido §4ay; o resource resolve §4az; `func_002BE75C` não é a fonte §4bb).
Padrão reconhecido (skill `retracting-your-own-findings`).

**O que SOBREVIVE, medido e sólido:**
- Com o R_PermA entregue (E275) a construção do nível EXECUTA e o construct de shells CGOWShader completa **5 shells**
  (idx 0x0C/07/08/06/1B); o 6.º (idx 0x12) crasha em `stuck calling 0x4BFFFE34` (loop no `ps3_indirect_call`).
- `0x4BFFFE34` está na região do buffer HOSTRES (`0x4B000000`+), que `allocate_from_container` devolve sempre igual.
- `func_002BE75C` é chamado com `param2=0` (null) — na consola seria um ponteiro válido; isto PODE ser o defeito real
  (o caller passa null), mas não está ligado ao `0x4BFFFE34` por medição.

**O que CAI:** que `func_002BE75C` produza `0x4BFFFE34`; a narrativa do "ponteiro interno do resource" de §4ba.

**Próximo (o método certo, não mais inferência):** capturar o `0x4BFFFE34` NO SÍTIO onde é chamado — instrumentar o
`ps3_indirect_call` (ou o trampolim) para imprimir o alvo E o `lr`/caller guest quando o alvo ∈ `0x4Bxxxxxx`, e a
partir daí subir (quem escreveu esse ponteiro, em que objeto/campo). Sem hand-decode; capturar no ponto do evento. E
investigar o `param2=0` de `func_002BE75C` (caller que passa null) como defeito separado, medido.

**Estado:** [D]/[E] não alcançado. Sólido: a construção do nível EXECUTA e o CGOWShader constrói 5 shells (o mais perto
de [D] do projeto). Por pinar: a origem exacta do `0x4BFFFE34` do 6.º shell — a caracterizar por captura no ponto do
indirect call, não por inferência.


## §4bc SÍNTESE (E295): a crash 0x4BFFFE34 é o LOOKUP no registry-tree (`func_002acbe8`) a devolver 0 — a chave do 6.º shell NÃO está registada

Capturado NO SÍTIO (probe `PS3_TRACE_ICALL_TARGET=0x4BFFFE34`, sem inferência): `ctr=0x4BFFFE34 lr=0x002B0DDC r3=0
r11=0x4003ED70`. `lr=0x2B0DDC` = o retorno após `bl 0x2AAC5C` em `func_002B0DB0`. A cadeia:
`func_002B0DB0 → func_002AAC5C (return *(FUN_002aab98(record)+4)) → FUN_002aab98`. E **`FUN_002aab98` faz o lookup pela
CHAVE via `func_002acbe8`** (a árvore key→node do §4ag):
```
uVar1 = FUN_002aaa6c(...);           // a chave
iVar2 = FUN_002acbe8(param_1+1, uVar1);  // lookup na arvore
if (iVar2 != 0) uVar4 = *(iVar2+4) & 0x7fffffff;  // achou
return uVar4;                        // 0 se NAO achou
```
`func_002AAC5C` devolve 0 (chave não achada) → em `func_002B0DB0`, `r3=iVar5=0`, `lhz r0,0x2(r3)` lê o type de NULL →
`puVar1[garbage]` → handler lixo → método `0x4BFFFE34` → `stuck calling`.

**Síntese (fecha o círculo com §4ag):** a crash é a **MESMA árvore de registry** (`func_002acbe8`) — 5 chaves de shell
resolvem (os 5 shells que constroem), a **6.ª (do shell idx 0x12) NÃO está registada** → lookup devolve 0 → dispatch em
lixo → 0x4BFFFE34. NÃO é ponteiro de resource (§4ba retractado), NÃO é reuso do HOSTRES (inferência não confirmada); é
a **população do registry**: uma chave em falta. É literalmente a parede [D] (o registry) medida no ponto exacto.

**Próximo (o certo):** ver que CHAVE o 6.º shell procura (probe em `FUN_002aaa6c`/`func_002acbe8` a imprimir a chave e
o resultado do lookup para as 6), e quem REGISTA as chaves na árvore (o insert em `func_002acbe8`/`func_002abfa4`) —
porque as 5 primeiras registam e a 6.ª não. Provavelmente um record anterior que devia registar a chave do idx 0x12 e
não foi processado, ou a chave vem de dados ainda não entregues (só 256 KB do R_PermA) ou de outro WAD.

**Estado:** [D]/[E] não alcançado, MAS o registry ESTÁ a ser populado (5 chaves de shell registadas e resolvidas) e a
crash é uma chave em falta no registry — a caracterização mais precisa e mais próxima de [D] do projeto, medida no
ponto exacto (o lookup da árvore), ligada ao §4ag.


## §4bd VERIFICADO (E296): o lookup do registry (`func_002ACBE8`) devolve 0 mesmo antes do 6.º shell — §4bc confirmado por medição

Probe `[AAB98RET]` no retorno de `func_002ACBE8` dentro de `FUN_002aab98` (o lookup do registry), correlacionado com
`[TYPE15SHELL]`:
```
[TYPE15SHELL] post#5 ... (5.º shell OK)
[AAB98RET] iVar2=0x4003F2F0   (achado)
[AAB98RET] iVar2=0x4003F300   (achado)
[AAB98RET] iVar2=0x00000000   (NAO achado -> 0)
[TYPE15SHELL] pre#6 this=0x400D7888   (6.º shell) -> crash
```
**Confirmado (não inferência):** o lookup `func_002ACBE8` devolve **0** imediatamente antes do 6.º shell — a chave do
6.º shell NÃO está registada na árvore. §4bc está correcto: 5 chaves resolvem, a 6.ª devolve 0 → `r3=0` em
`func_002B0DB0` → dispatch em lixo → `stuck calling 0x4BFFFE34`.

**Root VERIFICADO:** a construção do nível popula o registry-tree com 5 chaves de shell e resolve-as; a 6.ª chave (do
shell idx 0x12) não está registada, o lookup devolve 0, e o código não guarda o 0 → crash. É a **população incompleta
do registry [D]** — medido no ponto exacto, verificado.

**Próximo (fix, com o root verificado):** (1) medir a CHAVE do 6.º shell (`FUN_002aaa6c`) e comparar com as 5 registadas;
(2) achar o INSERT na árvore (`func_002abfa4`, §4) e ver que record/passo regista as chaves — porque as 5 primeiras
registam antes do seu construct e a 6.ª não (ordem de registo, ou a chave vem de um record ainda não processado —
o loader crasha a ~2.6 KB de 256 KB). Reposto o registo da 6.ª chave, o dispatch resolve, o shell constrói, e o registry
completa-se → compiles de shader do nível [D]/[E].

**Estado:** [D] PARCIAL e VERIFICADO — o registry ESTÁ a popular (5 chaves de shell registadas e resolvidas, medido); a
6.ª chave em falta é o único bloqueio, no ponto exacto do lookup. É a caracterização mais forte e verificada de [D] do
projeto: o registry a popular, uma entrada a faltar.


## §4be FULL CIRCLE (E297): a chave do 6.º shell não falha por não estar registada — o OBJETO do 6.º shell é um HANDLE TAGGED `0x84000013` (a corrupção do pool de §4a)

Probe `[AAB98KEY]` (a chave e o arg/árvore no lookup) correlacionado com `[TYPE15SHELL]`:
```
[AAB98KEY] key=0x4DF4C5BD arg=0x4003ECFC   (achado -> 0x4003F2F0)
[AAB98KEY] key=0x59E88BEA arg=0x4003ECFC   (achado -> 0x4003F300)
[AAB98KEY] key=0x25CEBF82 arg=0x84000017   (NAO achado -> 0)   <<< arg TAGGED
[TYPE15SHELL] pre#6 ... -> crash
```
O `arg` (=`param_1+1`, o ponteiro da árvore que `func_002ACBE8` percorre) do lookup que falha é **`0x84000017`** — um
**handle TAGGED** (bit31). Os 5 que resolvem usam `arg=0x4003ECFC` (válido). `param_1 = 0x84000013` (também tagged;
`+1 word` = `0x84000017`). Ou seja, **o OBJETO passado ao lookup do 6.º shell é um handle tagged do pool** — NÃO é uma
chave em falta. Os inserts (`[ABFA4KEY]`, 40×) vão a uma árvore VÁLIDA (`0x00868C10`); o 6.º shell procura numa árvore
tagged `0x84000017` porque o SEU objeto é lixo do pool.

**FULL CIRCLE:** `0x84000017`/`0x84000013` é EXACTAMENTE a corrupção "pool de handles" que persegui desde o INÍCIO desta
sessão (§4a-§4ah — `func_002ACBE8`, arena tagged, `FREELIST-TAG-GUARD`). Toda a cadeia converge: o objeto do 6.º shell é
alocado do pool corrompido → é um handle tagged → o lookup do registry percorre uma árvore tagged → devolve 0 → dispatch
em lixo → `0x4BFFFE34`. Retracta §4bc/§4bd na parte "chave não registada": **o registry ESTÁ completo para os 5; o 6.º
falha porque o SEU OBJETO é tagged**, não por falta de registo da chave.

**Root (verificado, full-circle):** a corrupção do pool de handles (`0x84000013`) — o objeto do 6.º shell é um handle
tagged. O pool aloca um handle tagged para o objeto do 6.º shell. É o megaprojeto "pool de handles" (a lição de
referência do CLAUDE.md), agora localizado ao ponto EXACTO: a alocação do objeto do 6.º shell type-15.

**Próximo (o walk da corrupção, com o alvo exacto):** de onde vem o objeto tagged `0x84000013` do 6.º shell — quem o
alocou (o pool `func_002abfa4`/`func_002ac2f8`/arena) e porque devolveu um handle tagged em vez de um objeto válido.
`walking-a-corruption-upstream` a partir do construct do 6.º shell (idx 0x12): capturar quem escreve `0x84000013` no
campo do objeto/na entrada do pool. Os 5 anteriores alocam objetos válidos; o 6.º um tagged.

**Estado:** [D]/[E] não alcançado. Mas o root está VERIFICADO e FULL-CIRCLE: a corrupção do pool de handles (§4a) é o que
trava o 6.º shell do CGOWShader — o objeto do 6.º shell é um handle tagged `0x84000013`. O registry popula 5 shells; o
6.º tem o objeto corrompido pelo pool. Toda a sessão converge nesta corrupção.


## §4bf PRODUTOR ISOLADO (E298): `func_00263178` (alocador da arena) escreve o handle tagged `0x84000013` no slot da tabela de objetos

Watch de `0x400044C0` (o slot tagged do 6.º shell) e `0x400044BC` (o slot válido adjacente):
- `[0x400044C0]=0x84000013 by func_00263178` — o **alocador da arena** escreve o handle TAGGED.
- `[0x400044BC]=0x4003ECF8 by func_002AAC84` — o **registrador de objetos** escreve um objeto VÁLIDO no slot ao lado.

A tabela de objetos está em `base=0x40004480` (de `PTR_DAT_0053fbdc`+4); slot 16 (`0x400044C0`) = tagged, slot 15
(`0x400044BC`) = válido. Disasm de `func_00263178`: constrói um valor com **boundary-tag** (`rlwinm r27,r31,26,0,5` =
índice nos bits de topo; `or r0,r0,r27`; `stw r0,0x0(r8/r9/r10)`) e escreve-o a endereços computados de um índice
(`r31`). Ou seja, `func_00263178` (arena) escreveu a sua **bookkeeping tagged na TABELA DE OBJETOS** — uma **colisão de
base/índice** entre a arena e a tabela de objetos em `0x40004480`. `func_002AAC84` (que devia ser o único a escrever
objetos válidos ali) escreve o slot 15 correcto; `func_00263178` clobbra o slot 16 com `0x84000013`.

**Cadeia completa, produtor a consumidor (medida, full-circle):**
`func_00263178` (arena) escreve `0x84000013` em `0x400044C0` (tabela de objetos, colisão) → `FUN_002aab64` lê o slot 16
= `0x84000013` (o "objeto atual" do 6.º shell) → `FUN_002aab98`/`func_002ACBE8` percorrem a árvore em `0x84000017`
(tagged) → devolvem 0 → `func_002B0DB0` dispatch em lixo → `stuck calling 0x4BFFFE34`.

**Root (o mais fundo e exacto do projeto):** `func_00263178` escreve boundary-tag na tabela de objetos (colisão de
layout arena↔tabela em `0x40004480`), ou com uma base/índice errados. É o produtor EXACTO da corrupção pool-de-handles
que a sessão inteira perseguiu (§4a).

**Próximo (o fix, com o produtor isolado):** porque `func_00263178` escreve em `0x400044C0` (que devia ser da tabela de
objetos) — comparar a base/índice que usa com a base da arena esperada; ver se a tabela de objetos e a arena deviam
estar em zonas distintas e colidem por uma alocação errada (a mesma classe do reuso `0x4B000000`). `walking-a-corruption-
upstream` a partir de `func_00263178`: que base (`r7`/`r12`/`r8`) usa e de onde vem.

**Estado:** [D]/[E] não alcançado, mas a corrupção pool-de-handles está isolada ao PRODUTOR EXACTO (`func_00263178`
escreve `0x84000013` no slot 16 da tabela de objetos `0x40004480`) — a caracterização mais completa e acionável do
projeto, do CGOWShader ao alocador da arena.


## §4bg CONTEXTO DO PRODUTOR (E299): `func_00263178` (de `lr=0x263074`, gestão da free-list da arena) escreve tags na região da tabela de objetos — arena e tabela PARTILHAM memória

Probe `[263178CALL]`: `func_00263178` é chamado repetidamente de `lr=0x00263074` (a gestão da free-list da arena),
escrevendo valores boundary-tag a endereços ESPALHADOS por `0x40004050`–`0x400044E4` (`r3`=target: 0x40004050, 84, 94,
D0, 41C, 430, 454, 478, **4C4**, 4E4; `r5`=índice/size decrescente). **Essa é a MESMA região da tabela de objetos**
(`0x40004480`; slot 16 = `0x400044C0`). Ou seja, a **free-list da arena e a tabela de objetos PARTILHAM memória**: a
arena escreve tags por toda a região; o registrador `func_002AAC84` sobrescreve ALGUNS slots com objetos válidos (slot
15 = `0x4003ECF8`); o slot 16 (`0x400044C0`) do 6.º shell **fica com o tag da arena** (`0x84000013`) porque nunca foi
sobrescrito por um objeto válido.

**Cadeia completa (medida, produtor→consumidor):** `func_00263178` (arena, de `lr=0x263074`) escreve tags na região
`0x40004050+` que colide com a tabela de objetos `0x40004480` → o slot 16 fica `0x84000013` → `FUN_002aab64` lê o slot
16 (o "objeto atual" do 6.º shell) = tagged → `func_002ACBE8` percorre árvore tagged `0x84000017` → 0 → dispatch lixo →
`0x4BFFFE34`.

**Root (o mais fundo e completo do projeto):** a **tabela de objetos** do level loader e a **free-list da arena**
(`func_00263178`) estão na MESMA memória (`0x40004050+`) — uma **colisão de layout**. Na consola seriam zonas distintas;
em nós colidem (a mesma classe do reuso `0x4B000000`/HOSTRES §4ba e do `allocate_from_container` sempre igual). O 6.º
shell lê um chunk livre/tagged da arena como se fosse o seu objeto.

**Próximo (o fix):** dar à tabela de objetos e à arena zonas de memória DISTINTAS (achar quem aloca cada uma e porque
recebem a mesma base `0x40004050`), OU garantir que os slots da tabela são todos sobrescritos com objetos válidos antes
de serem lidos. É a mesma classe de colisão de layout/alocação que aparece em toda a sessão (HOSTRES, container). É o
megaprojeto pool-de-handles, agora com o produtor E o contexto isolados: a arena a partilhar memória com a tabela de
objetos.

**Estado:** [D]/[E] não alcançado. A corrupção está isolada ao produtor E ao seu contexto: `func_00263178` (gestão da
arena) partilha a região `0x40004050+` com a tabela de objetos do loader, deixando o slot 16 tagged para o 6.º shell.


## §4bh (REFINADO por §4bk/E303 — a raiz NÃO é a arena adjacente; o índice é que over-conta) ORÁCULO (E300): na consola a arena de `func_00263178` está numa zona SEPARADA (`0x307D9D64`); em nós COLIDE com a tabela de objetos (`0x40004050`)

Oráculo RPCS3 (`EBOOT_skipintro.BIN`, bp em `0x00263178`, headless) — CONSOLA:
```
[bp] pc=0x00263178 r3=0x307D9D64[+0=0x00000012,+4=0x3083DC5C] r4=0x4 r5=0x12 r6=0x613.. ra=[0x00263074,0x003BE..]
```
NÓS (probe `[263178CALL]`): `r3=0x40004050..0x400044E4`, `r5=0x00BFFFxx`, `r7=r3`, `lr=0x00263074`.

**Comparação decisiva:**
| | consola | nós |
|---|---|---|
| `r3` (target da arena) | `0x307D9D64` (heap alto `0x30xxxxxx`, ZONA SEPARADA) | `0x40004050`+ (COLIDE com a tabela de objetos `0x40004480`) |
| `r5` (arg) | `0x12` | `0x00BFFFxx` (runaway) |
| caller `ra` | `0x263074` (igual) | `0x263074` (igual) |

**CONFIRMADO pelo oráculo:** na consola a arena (`func_00263178` target) está em `0x307D9D64` — **distinta** da tabela de
objetos. Em nós está em `0x40004050`, a **COLIDIR** com a tabela de objetos (`0x40004480`). É um **bug de alocação de
heap NOSSO**: a arena e a tabela de objetos recebem memória sobreposta. O `r5` também diverge muito (`0x12` vs
`0x00BFFFxx`), o que aponta para o caller (`func_00263054`, `ra=0x263074`) a computar a base/args errados a partir de um
ponteiro NOSSO errado.

**Root (verificado consola vs nós):** o alocador de heap NOSSO dá à arena um endereço que colide com a tabela de objetos
(`0x40004050` vs `0x40004480`), enquanto na consola a arena está em `0x307D9D64` (separada). Mesma classe de colisão de
alocação do `0x4B000000`/HOSTRES e do `allocate_from_container`. É o produtor da corrupção pool-de-handles, agora
**confirmado contra a consola**.

**Próximo (o fix, com a consola como referência):** achar onde o caller `func_00263054` (`ra=0x263074`) obtém a base da
arena (`r3`/`r7`) — na consola `0x307D9D64`, em nós `0x40004050`. Subir até à alocação dessa base (que devia ser um bloco
de heap distinto e é o mesmo/sobreposto à tabela de objetos). Corrigida a base da arena (bloco distinto), a arena deixa
de clobbrar a tabela de objetos, o 6.º shell lê um objeto válido, o registry completa-se e os shaders do nível compilam.

**Estado:** [D]/[E] não alcançado, mas o root está VERIFICADO CONTRA A CONSOLA: a arena colide com a tabela de objetos
em nós (`0x40004050`) e é distinta na consola (`0x307D9D64`). Bug de alocação de heap nosso, produtor da corrupção do
6.º shell CGOWShader. A caracterização mais forte e verificada do projeto.


## §4bi (REFINADO por §4bk/E303 — o índice da consola é ESTÁVEL a 14, nunca chega a 16) ORÁCULO (E301): mesma tabela/offset; a ARENA está mal-localizada (adjacente) e o índice corre à frente

Oráculo (bp `0x2AAB64`, ler `*(0x53fbdc+4)`=base, `+8`=idx, o slot) — CONSOLA vs NÓS:
| | consola | nós |
|---|---|---|
| tabela | `0x00868C00` | `0x00868C00` (igual) |
| base | `0x30004480` (heap+0x4480) | `0x40004480` (heap+0x4480, MESMO offset) |
| idx | **14** | **16** |
| slot | `0x300044B8` | `0x400044C0` |
| val | `0x30636900` (objeto VÁLIDO) | `0x84000013` (TAGGED) |
| arena (`func_00263178`) | `0x307D9D64` (LONGE da tabela) | `0x40004050` (ADJACENTE → clobbra a tabela) |

**Refinamento (medido, oráculo):** a tabela de objetos é estruturalmente IGUAL (mesma tabela, mesmo offset base+0x4480).
A divergência é: (1) a **arena está a `0x40004050` em nós** (imediatamente ANTES da tabela `0x40004480`) → a free-list da
arena cresce/escreve para dentro dos slots da tabela; na consola a arena está a `0x307D9D64` (LONGE). (2) o **índice
corre à frente** (16 vs 14). A arena estar adjacente/antes da tabela é o que a clobbra; na consola há 8 MB alocados antes
da arena (`0x7D9D64`), em nós só ~16 KB (`0x4050`), o que a põe antes da tabela.

**Root (verificado consola vs nós, refinado):** o **layout de heap** difere — em nós a arena é alocada CEDO (`0x4050`,
antes da tabela `0x4480`) porque o heap está sub-populado (entregamos 256 KB do R_PermA por host, não os 20 MB naturais
que a consola processa antes de chegar aqui). A arena adjacente clobbra a tabela de objetos. NÃO é a tabela que está mal
(mesmo offset); é a arena a cair no sítio errado por o heap ter menos alocações que a consola.

**Isto liga de volta à entrega natural:** com a entrega natural completa do R_PermA (20 MB) e as alocações que ela gera,
o heap ficaria populado como a consola e a arena cairia longe (`~0x7D9D64`), sem clobbrar a tabela. O host-fill de 256 KB
(E275) destravou a construção do nível mas deixa o heap sub-populado → a arena colide. Retracta a parte de §4bg/§4bh que
punha a culpa num "bug do alocador de heap" genérico: é o **heap sub-populado pela entrega host parcial**.

**Próximo (o fix, agora claro):** completar a entrega natural do R_PermA (os 20 MB, não os 256 KB do host-fill) para o
heap popular como a consola e a arena cair longe da tabela — OU (paliativo) forçar a arena a um bloco distinto. A via
natural é a meta; o host-fill parcial (E275) foi o que revelou tudo isto mas é ele próprio que sub-popula o heap.

**Estado:** [D]/[E] não alcançado. Root VERIFICADO vs consola e refinado: a arena cai adjacente à tabela de objetos
porque o heap está sub-populado pela entrega host parcial de 256 KB; a consola tem a arena longe (heap cheio). O fix é a
entrega natural completa do R_PermA.


## §4bj O setup da tabela de objetos: `func_002AB070` põe base=`0x40004480` e índice=16 (consola: 14) (E302)

Watch de `0x00868C08` (índice da tabela) e `0x00868C04` (base): `func_002AAC84` incrementa o índice para 15 (registo de
objeto), depois **`func_002AB070`** põe índice=**16** E base=**`0x40004480`** (um grow/realloc da tabela de objetos).
`func_002AB070` não está no dump do Ghidra. Consola (oráculo §4bi): índice=**14**, base `0x30004480`.

**Síntese das duas divergências medidas (consola vs nós):**
1. **Arena** (`func_00263178`): consola `0x307D9D64` (LONGE); nós `0x40004050` (ADJACENTE à tabela `0x40004480`).
2. **Índice da tabela** (`func_002AB070`): consola 14; nós 16.

Em nós, a tabela de objetos (`0x40004480`) e a arena (`0x40004050`) são alocações ADJACENTES; a arena escreve até
`0x400044E4` (dentro da tabela) e o índice 16 lê `0x400044C0` (território clobbrado pela arena). Na consola a arena está
a 8 MB de distância e o índice (14) lê um slot válido.

**Duas hipóteses (não resolvidas por medição, precisam do fix/oráculo lado a lado):**
- (A) **Off-by-one do índice**: 16 vs 14 — se a tabela tem 16 slots (0-15), o índice 16 está FORA (overflow para a
  arena). `func_002AB070` sobre-conta.
- (B) **Colisão de alocação**: a tabela e a arena caem adjacentes por o heap estar sub-populado (256 KB host vs 20 MB
  natural); na consola a arena está longe.

Ambas apontam para o mesmo sintoma (o 6.º shell lê um slot tagged) e provavelmente estão ligadas (a arena adjacente +
o índice a apontar para ela). O fix precisa de comparar `func_002AB070` e a alocação da arena lado a lado com a consola
(oráculo, agora funcional).

**Estado (o mais completo do projeto, verificado vs consola):** [D]/[E] não alcançado. O 6.º shell CGOWShader lê um
objeto tagged porque (em nós) a tabela de objetos e a arena são adjacentes (`0x4050`/`0x4480`) e o índice (16) cai em
território da arena; na consola a arena está longe (`0x7D9D64`) e o índice (14) lê válido. Produtor: `func_00263178`
(arena); setup da tabela: `func_002AB070`. Fix: separar a arena da tabela (via entrega natural completa que popula o
heap, OU corrigir a alocação/índice) — comparação directa com o oráculo disponível.


## §4bk ORÁCULO DECISIVO (E303): o índice da tabela é ESTÁVEL a 14 na consola (400 hits, nunca 16); o NOSSO over-conta para 16

Oráculo (bp `0x2AAB64`, 400 hits, ler o índice `*(*(0x53fbdc)+8)`): na consola o índice fica **CONSTANTE a 14** em TODOS
os 400 hits — **nunca chega a 16** — e o slot 14 é sempre válido (`0x30636900`). Em nós chega a **16** (`func_002AAC84`
→15, `func_002AB070`→16) e o slot 16 é tagged.

**Conclusão (medida, decisiva):** a consola NUNCA usa o slot 16. O nosso índice **over-conta ~2** (16 vs 14 estável).
O 6.º shell lê o slot 16, que na consola nem existe como "objeto atual". Isto **REFINA/retracta §4bh/§4bi**: a raiz NÃO
é a arena estar adjacente por heap sub-populado (a arena adjacente só importa porque o nosso índice cai lá; na consola o
índice fica a 14 e nunca toca a arena). A raiz é o **over-count do índice da tabela de objetos** — registamos ~2 objetos
a mais que a consola.

**Sobrevive (medido):** a arena está a `0x40004050` (adjacente) em nós vs `0x307D9D64` na consola; a tabela é o mesmo
offset. **Cai:** que a adjacência da arena seja a causa — a causa é o índice ir a 16 (a consola fica a 14 e o slot é
válido).

**Root (o mais preciso, oráculo-decisivo):** o índice da tabela de objetos over-conta para 16 em nós (via `func_002AAC84`
+`func_002AB070`) vs 14 estável na consola. Registamos ~2 objetos a mais. O 6.º shell lê o slot 16 (fora do range válido
0-13 da consola) → tagged (território da arena/não-inicializado).

**Próximo (o fix):** achar porque `func_002AAC84`/`func_002AB070` empurram o índice para 15→16 em nós quando a consola
fica a 14 — os ~2 registos extra de objeto. Comparar os hits de `func_002AAC84`/`func_002AB070` (o que registam) com a
consola (oráculo, bp nesses endereços). Provavelmente processamos ~2 records/objetos a mais (ou duplicados) por causa da
entrega host parcial/diferente do R_PermA. Corrigido o over-count, o índice fica a 14, o 6.º shell lê um objeto válido,
o registry completa-se e os shaders do nível compilam.

**Estado:** [D]/[E] não alcançado. Root oráculo-decisivo: o índice da tabela de objetos over-conta (16 vs 14 estável na
consola). A caracterização mais precisa e verificada do projeto — e diferente do que §4bh/§4bi inferiram (a arena não é
a causa; o over-count do índice é).


## §4bl CORREÇÃO (E304): o índice DECREMENTA (down-counter) — 16 = MENOS objetos que 14; §4bk (over-count) estava errado na DIREÇÃO

`func_002AAC84` (o register de objeto): `iVar2 = piVar1[2]; ...; piVar1[2] = iVar2 - 1` — **DECREMENTA** o índice
(`0x868C08`) a cada objeto registado. Ou seja, o índice é um **down-counter**: quanto MAIS objetos registados, MENOR o
índice. Logo:
- Consola índice=**14** = MAIS objetos registados.
- Nós índice=**16** = MENOS objetos registados (~2 a menos).

**Correção (skill `retracting-your-own-findings` — "a retraction can itself be wrong"):** §4bk concluiu "over-count
(registamos 2 a MAIS)" — ERRADO na direção. O índice decrementa, logo 16 vs 14 significa que registamos ~2 objetos a
MENOS que a consola. Isto **re-suporta §4bi**: sub-registamos porque a entrega do R_PermA é PARCIAL (256 KB host, não
os 20 MB naturais) — faltam-nos ~2 objetos/records que a consola regista. O 6.º shell procura uma chave que um desses
objetos em falta registaria; sem ele, a chave não está na árvore → lookup 0 → o slot 16 (não-inicializado, com o tag da
arena) → crash.

Sobrevive (medido, oráculo): índice consola=14 estável, nosso=16; o índice decrementa (func_002AAC84). Cai: a direção
de §4bk (não é over-count; é UNDER-count).

**Root (re-convergido, oráculo + código):** sub-registamos ~2 objetos vs a consola porque a entrega do R_PermA é parcial
(host 256 KB). Faltam os records/objetos que registariam a chave do 6.º shell. É a mesma raiz de §4bi (entrega natural
incompleta), agora com a prova do índice (down-counter: 16<14 objetos... 16 index = menos objetos).

**Fix (o mesmo, re-confirmado):** completar a entrega NATURAL do R_PermA (20 MB) para registar todos os objetos como a
consola (índice desce a 14) — a meta do projeto. O host-fill (E275) destravou a construção mas entrega só 256 KB, logo
sub-regista. Alternativa de diagnóstico: comparar os hits de `func_002AAC84` (o register) com a consola para ver os ~2
que faltam.

**Estado:** [D]/[E] não alcançado. Root oráculo+código: sub-registamos ~2 objetos (índice 16 vs 14, down-counter) por
entrega parcial do R_PermA; a chave do 6.º shell fica por registar. A caracterização final e verificada: a entrega
natural completa é o fix, e o índice da tabela de objetos (down-counter, 16 vs 14) é a prova medida de que faltam
objetos.


## §4bm CONSOLIDAÇÃO (E305): a tabela de objetos está VAZIA no crash porque a arena a sobrepõe — §4bh confirmado; §4bk/§4bl (índice) eram sintomas

Medição DIRECTA (probe `[SLOTS]`, lê os slots da tabela no crash): `base=0x40004480 idx=16: [10..15]=0x00000000
[16]=0x84000013T [17]=0x80000008T`. **Os slots 10-15 estão a ZERO** (vazios), 16-17 têm valores tagged da arena. Os
shells 1-5 construíram (a tabela TINHA objetos), depois o grow (`func_002AB070`, ao 6.º) realocou a tabela (via
`FUN_002637d8`→`FUN_00263040`, o alocador da arena) e **os objetos ficaram perdidos** — a tabela nova sobrepõe a arena.

**Consolidação honesta (fim do flip-flop):** houve várias interpretações do índice (§4bk over-count, §4bl under-count) —
AMBAS eram a ler LIXO de uma tabela já corrompida (índice/slots garbage). A medição directa mostra o que importa: a
**tabela está VAZIA** no crash porque a **arena a sobrepõe** (arena escreve `0x40004050-0x400044E4`, tabela `0x40004480`).
Isto **confirma §4bh** (colisão arena↔tabela) e torna §4bk/§4bl sintomas, não causa.

**Facts sólidos (medidos, não inferidos):**
1. Tabela de objetos VAZIA no crash (slots 10-15 = 0; 16-17 = tagged da arena).
2. Arena sobrepõe a tabela em nós (`0x40004050-0x400044E4` vs `0x40004480`).
3. Consola: arena LONGE (`0x307D9D64`), tabela com objetos válidos (slot 14 = `0x30636900`).
4. A tabela é alocada da arena (`func_002AB070`→`FUN_002637d8`→`FUN_00263040`); a arena corrompida/mal-localizada
   corrompe a tabela.

**Root (consolidado, medido + oráculo):** a arena está mal-localizada em nós (`0x40004050`, a sobrepor a tabela de
objetos `0x40004480`), enquanto na consola está longe (`0x307D9D64`). A tabela, alocada da arena, é corrompida (vazia +
tagged). O 6.º shell (e o grow) leem a tabela corrompida → crash. É a corrupção pool-de-handles (§4a) manifestada como
a arena a sobrepor a tabela de objetos.

**Meta-honestidade:** esta caça teve MUITOS ciclos de interpretação-e-correção (~21 retracções). A âncora fiável é a
medição DIRECTA (tabela vazia + arena sobreposta), não as interpretações do índice. O fix é separar a arena da tabela
(porque `FUN_00263040`/`FUN_002637d8` alocam a tabela num sítio que colide com a própria arena) — comparar com a consola
(arena longe) via oráculo.

**Estado:** [D]/[E] não alcançado. Root medido e consolidado: a arena sobrepõe/corrompe a tabela de objetos (vazia no
crash), enquanto na consola estão separadas. É a corrupção pool-de-handles no ponto exacto (a alocação da tabela pela
arena). Fix: separar as duas alocações.


## §4bn CORREÇÃO + HONESTIDADE (E306): a arena PRINCIPAL (*0x53fbf0) IGUALA a consola — §4bi (heap sub-populado) refutado; o root exacto resistiu a isolamento estável

Oráculo + nosso, o objeto da arena principal (`*0x53fbf0` = `0x868D2C`):
| campo | consola | nós |
|---|---|---|
| +0 | `0x3003ED40` | `0x4003ED40` |
| +4 | `0x3003ED10` | `0x4003ED10` |
| +8 | `0x3003ED70` | `0x4003ED70` |
| +C | `0x30013C80` | `0x40013C80` |
**IDÊNTICOS excepto a base do heap (0x30 vs 0x40).** A arena principal está em `heap+0x3ED40` (256 KB) em AMBOS.

**Isto REFUTA §4bi (heap sub-populado):** as alocações principais BATEM com a consola (mesmo offset, 256 KB). O heap NÃO
está sub-populado de forma geral. A divergência é ESPECÍFICA: o array de backing da tabela de objetos (onde
`func_00263178` escreve) está em `0x40004050` em nós vs `0x307D9D64` na consola, enquanto a arena principal (`0x3ED40`)
e o struct da tabela (`0x4480`) batem.

**Honestidade meta (skill `retracting-your-own-findings`, caso extremo):** esta caça teve ~22 ciclos de
interpretação-e-correção — colisão de heap (§4bh), over-count do índice (§4bk), under-count (§4bl), sub-população
(§4bi), agora refutada. **Não consegui isolar de forma ESTÁVEL o único root fixável.** Cada medição corrigiu a anterior.
O que fica FIÁVEL, repetidamente confirmado:
1. A tabela de objetos está corrompida (vazia/tagged) no crash do 6.º shell.
2. A arena PRINCIPAL (`*0x53fbf0`) bate com a consola (`0x3ED40`, 256 KB) — o heap não está sub-populado.
3. O array de backing da tabela (`func_00263178` targets) diverge: `0x40004050` (nós) vs `0x307D9D64` (consola).
4. O índice da tabela diverge (16 vs 14) — mas lido de uma tabela já corrompida.

**O que NÃO está estabelecido:** o mecanismo exacto que corrompe a tabela / mal-aloca o seu backing. As interpretações
(colisão, índice, sub-população) foram todas parcial ou totalmente refutadas por medição posterior.

**Estado honesto:** [D]/[E] não alcançado. A parede está isolada ao sistema de alocação da tabela de objetos do loader
(o backing diverge da consola), mas o root exacto e fixável NÃO foi isolado de forma estável nesta sessão — 22
retracções por medição. O próximo passo é um push DEDICADO com o oráculo lado-a-lado em `func_00263178`/`func_002AB070`
(o setup/grow da tabela), comparando `func_002637d8`→`func_00263040` (a alocação do backing) passo a passo com a
consola, SEM as interpretações prematuras que esta sessão acumulou.


## §4bo HONESTIDADE FINAL (E307): a arena do object-table (0x40004020) tem data FAR (0x406373F4, OK) e IGUALA a consola; a fonte da corrupção do slot é um estrutura DISTINTA não-isolada

Leitura directa de `0x40004020` (a arena struct do object-table): `+0=0xC0DE1111` (magic OK), `+4=0x406373F4`
(data em heap+0x6373F4, **6.5 MB — LONGE da tabela 0x4480**), `+1C=0x526F6F74` ("Root"). Consola: `0x30004020`
`+0=0xC0DE1111 +4=0x307D9D64` (data far, 8 MB). **Esta arena BATE com a consola (data far).**

Ou seja: a arena que eu tinha responsabilizado (§4bh/§4bm) tem a data LONGE da tabela, como a consola. As escritas em
`0x40004050-0x400044E4` (que sobrepõem a tabela `0x40004480`) vêm de uma **estrutura DISTINTA** que NÃO isolei
limpamente. Nesta região há múltiplas arenas/estruturas (a consola mostra arenas em `0x30004020`→`0x307D9D64` E
`0x33004020`→`0x33004050`); persegui várias e cada medição refutou a anterior.

**HONESTIDADE (caso extremo de `retracting-your-own-findings`):** ~23 ciclos de interpretação-e-refutação nesta caça.
NÃO consegui isolar de forma estável a estrutura/alocação exacta que corrompe o slot 16 da tabela de objetos. As teorias
(colisão da arena principal, over/under-count do índice, heap sub-populado, esta arena 0x40004020) foram todas parcial
ou totalmente refutadas por medição posterior. Isto é uma falha de convergência da minha parte, não do método.

**O que fica FIÁVEL, medido e repetido:**
1. E275 (`PS3_WAD_HOST_READ`) destravou a construção do nível — o CGOWShader constrói 5 shells (avanço real, committado).
2. O 6.º shell crasha; a tabela de objetos do loader está corrompida (slots vazios/tagged) nesse ponto.
3. A arena PRINCIPAL (`*0x53fbf0`) e a arena do object-table (`0x40004020`) BATEM com a consola (data far).
4. Algo escreve `0x40004050-0x400044E4` (via `func_00263178`) sobrepondo a tabela — fonte NÃO isolada.
5. O oráculo RPCS3 funciona (receita corrigida) e pode comparar qualquer estrutura lado-a-lado.

**Próximo (push DEDICADO, começar limpo):** vigiar QUEM chama `func_00263178` com target `0x40004050` (o `lr`/caller
guest e a arena struct que passa), e comparar essa arena struct específica com a consola via oráculo — sem arrastar as
interpretações desta sessão. É a única forma de isolar a estrutura certa entre as várias arenas desta região.

**Estado honesto:** [D]/[E] não alcançado. A parede está isolada à corrupção da tabela de objetos do loader no 6.º shell,
mas a estrutura/alocação exacta que a corrompe NÃO foi isolada de forma estável (~23 retracções). O avanço real e
committado é o E275 (5 shells CGOWShader). O oráculo funcional é a ferramenta para o próximo push.


## §4bp RE-CONVERGÊNCIA LIMPA (E308): a arena (0x40004020) é um BUMP que começa em 0x40004050 (ABAIXO da tabela) e cresce ATRAVÉS dela

Probe `[A263040]` (a arena struct quando o seu data cai na região da tabela): `arena=0x40004020 magic=0xC0DE1111
data=0x40004050→0x40004084→0x40004094→0x400040D0→… size=0x4..0x34 lr=0x0038C708/0x00385778/…`. Ou seja, a arena
`0x40004020` é um **bump allocator** cujo ponteiro de data COMEÇA em `0x40004050` (struct+0x30, **ABAIXO** da tabela de
objetos `0x40004480`) e CRESCE para cima à medida que objetos são registados (callers em `0x385xxx-0x38Cxxx`). Ao passar
por `0x40004480` **reescreve a tabela**. Depois continua até `0x406373F4` (por isso §4bo, a ler o estado TARDIO, viu o
data "longe" — já tinha crescido para além da tabela).

**Consola:** o buffer da arena é uma alocação SEPARADA em `0x307D9D64` (8 MB), LONGE da tabela (`0x30004480`) — nunca lhe
toca. **Nós:** a tabela (`0x40004480`) e a arena (`0x40004020`, a crescer de `0x40004050`) estão no MESMO bloco de
memória; a arena cresce através da tabela.

**Resolve o mistério das arenas múltiplas (§4bo):** era A MESMA arena `0x40004020` lida em tempos diferentes — cedo o
data está em `0x40004050` (a passar pela tabela), tarde em `0x406373F4` (já além). Não eram estruturas distintas; era o
bump a crescer. **Re-confirma §4bh/§4bm** (colisão arena↔tabela) com o mecanismo exacto e verificação vs consola.

**Root (re-convergido, medido + oráculo):** a tabela de objetos do loader e o buffer da arena partilham o mesmo bloco
(`0x40004020+`) em nós; o bump da arena cresce de `0x40004050` (abaixo) através da tabela (`0x40004480`), corrompendo-a.
Na consola são blocos SEPARADOS (tabela `0x30004480`, arena `0x307D9D64`). É uma diferença de LAYOUT DE ALOCAÇÃO: em nós
a tabela é colocada dentro do bloco de crescimento da arena; na consola são alocações distintas.

**Fix (concreto):** dar à tabela de objetos e ao buffer da arena blocos de memória DISTINTOS (como a consola:
`0x30004480` vs `0x307D9D64`). Achar onde ambos são alocados e porque caem adjacentes (`0x40004020`/`0x40004480`) em vez
de separados. Provavelmente o mesmo alocador/container os empacota adjacentes.

**Estado:** [D]/[E] não alcançado, mas o root está RE-CONVERGIDO e verificado: a arena e a tabela de objetos partilham o
bloco em nós (a arena cresce através da tabela) enquanto na consola são separadas. §4bo (arena longe/OK) era o estado
tardio da mesma arena. O fix é separar as duas alocações.


## §4bq HONESTIDADE (E309): o SWAR strcmp está CORRECTO — hipótese de lifter-bug refutada; a diferença de layout da arena NÃO foi explicada

Probe `[SWARRES]` no resultado de `FUN_0037304c` (o strcmp que gateia o grow em `func_00263878`):
- `"Root"` vs `"BP"` → `res=1` (DIFERE, correcto — são diferentes) → SEM grow.
- `"BP"` vs `"BP"` → `res=0` (IGUAL, correcto) → COM grow.
- `"Debug"`, `"RB NODE Heap"`, `"Levels"` → resultados sensatos.

**O SWAR strcmp está CORRECTO** (resultados batem com a comparação real das strings). **Refuta a hipótese de
lifter-bug do SWAR.** A condição de grow ("nome == BP") funciona; a arena "Root" correctamente NÃO cresce por este
caminho. Logo o buffer LONGE da "Root" na consola (`0x307D9D64`) vem de OUTRO mecanismo que não isolei.

**Honestidade (caso extremo de `retracting-your-own-findings`):** ~24 hipóteses sobre o root desta parede foram
refutadas ou contraditas por medição posterior nesta sessão — colisão da arena principal, over/under-count do índice,
heap sub-populado, colisão de bloco, e agora o SWAR strcmp. Consegui isolar o MECANISMO (a arena "Root" cresce inline de
`0x40004050` através da tabela `0x40004480` em nós; na consola tem buffer longe em `0x307D9D64`) mas NÃO a causa
fixável dessa diferença de placement. Cada medição que julguei decisiva foi contradita pela seguinte.

**O que fica FIÁVEL:**
1. E275 destravou a construção do nível — 5 shells CGOWShader construídos (avanço real, committado).
2. A arena "Root" (`0x40004020`) cresce inline de `0x40004050` através da tabela de objetos (`0x40004480`), corrompendo-a.
3. Na consola a arena "Root" tem buffer LONGE (`0x307D9D64`), separado da tabela.
4. O SWAR strcmp e as arenas que verifiquei estão correctos/batem.
5. O oráculo funciona.

**Não isolado:** porque o buffer da "Root" fica inline (a passar pela tabela) em nós vs longe/separado na consola. O
`func_00263878` põe o buffer inline (struct+0x30) em ambos, mas na consola a arena acaba com buffer longe — uma
contradição (grow/realloc) que não resolvi.

**Estado honesto:** [D]/[E] não alcançado. Isolei o mecanismo da corrupção (arena "Root" cresce através da tabela) mas
não a causa fixável, após ~24 refutações. É uma falha de convergência minha nesta sessão. O avanço real e committado é
o E275 (5 shells). Próximo push dedicado: instrumentar `func_00263878` para o `param_2` (tamanho do buffer) da "Root" e
comparar com a consola via oráculo — porque na consola o buffer acaba longe e em nós inline/overflow.


## §4br NARROWING (E310): a "Root" faz REALLOC para buffer separado na consola (0x307D9D64) quando cheia; em nós fica inline e transborda

Oráculo `func_00263878` (consola): cria arenas WAD em endereços ALTOS (`0x31708900`, `0x324479A8`, `0x314873C8`…,
sizes `0x33300`–`0x2C0F4C` = 200 KB–2.8 MB, nomes "WAD_…"). O struct da "Root" é BAIXO (`0x30004020`) mas o seu buffer
de data é uma alocação SEPARADA e ALTA (`0x307D9D64`). Como `func_00263878` põe o buffer INLINE (struct+0x30), a "Root"
na consola tem de fazer **REALLOC para um buffer separado quando o inline enche**.

**Narrowing (reasoned + medido):** em nós a "Root" fica INLINE (`0x40004050`) e o bump (`func_00263178`) cresce ATRAVÉS
da tabela (`0x40004480`) SEM realloc. Na consola, quando o inline enche, realoca para `0x307D9D64` (separado, longe da
tabela). **O bug é o REALLOC-ON-FULL da arena "Root"** — não dispara em nós, logo o bump transborda inline para a tabela.

**Fix (mais específico):** achar o realloc-on-full da arena (quando `param_1[1]`/bump atinge `param_1[6]`/end) — em
`func_00263178`/`func_002AB070` — e porque não realoca para um buffer separado em nós como na consola. Instrumentar o
ponto onde o bump atinge o end e comparar com a consola (que aí realoca).

**Estado:** [D]/[E] não alcançado. Narrowing real: o mecanismo é o realloc-on-full da arena "Root" (consola realoca para
separado; nós ficamos inline e transbordamos a tabela). Ainda não achei o código exacto do realloc nem porque falha em
nós — mas é mais específico que antes. Avanço committado: E275 (5 shells). ~24+ hipóteses refutadas; esta (realloc) é a
mais consistente com todas as medições (arena inline vs separada).


## §4bs HONESTIDADE FINAL (E311): o threshold da decisão inline/separado BATE com a consola; ~25 causas específicas refutadas, root não isolado

`FUN_0026258c` (a decisão inline vs separado) compara o endereço da arena contra `*(0x83692c)`: **nós `0x43004020`,
consola `0x33004020` — MESMO offset (0x4020), só a base do heap difere (0x43 vs 0x33).** O threshold BATE. Logo a
decisão inline/separado é a MESMA nos dois (a arena "Root" está abaixo do threshold em ambos). **Refuta a hipótese do
threshold.**

**Balanço honesto (extremo `retracting-your-own-findings`):** nesta sessão testei e refutei ~25 causas específicas para
a corrupção do 6.º shell — colisão da arena principal, over/under-count do índice, heap sub-populado, colisão de bloco,
SWAR strcmp, e agora o threshold da decisão. **Cada mecanismo específico BATE com a consola, mas o resultado (arena
"Root" inline em nós vs buffer separado 0x307D9D64 na consola) continua a divergir.** Isolei o MECANISMO (a "Root"
cresce inline através da tabela) mas NÃO a causa fixável — cada medição que julguei decisiva foi refutada pela seguinte.

**Isto é uma falha de convergência minha nesta sessão.** Não é falta de método (medi tudo contra a consola), mas não
consegui fechar o funil: o ponto onde o comportamento diverge (a "Root" acaba inline vs separada) está downstream de
tudo o que verifiquei (threshold, strcmp, arena principal — todos batem). O próximo teria de instrumentar o CAMINHO
EXACTO que a "Root" toma (FUN_0026300c/separado vs inline) e comparar passo-a-passo com a consola no MESMO ponto — não
mais valores isolados.

**O que fica FIÁVEL e committado:**
1. E275 (`PS3_WAD_HOST_READ`) destravou a construção do nível — 5 shells CGOWShader (avanço REAL).
2. A "Root" cresce inline através da tabela de objetos em nós; na consola tem buffer separado (0x307D9D64).
3. Threshold, SWAR strcmp, arena principal — todos batem com a consola.
4. O oráculo funciona (receita corrigida).

**Estado honesto:** [D]/[E] não alcançado. Isolei o mecanismo mas não a causa fixável, após ~25 refutações. É uma falha
de convergência minha. O avanço real e committado é o E275. A parede é o realloc/placement da arena "Root", downstream
de tudo o que verifiquei bater. Próximo push: instrumentar o caminho exacto da "Root" (separado vs inline) passo-a-passo
vs consola, com o oráculo.


## §4bt CONVERGÊNCIA ORÁCULO-CONFIRMADA (E312): a decisão/threshold/overlap BATEM; a diferença é a FREE-LIST da arena corrompida (= §4a pool-de-handles)

Oráculo `FUN_0026258c` (a decisão inline/separado) na consola:
- "BP" (0x33004020) → res=1 → SEPARADO.
- **"Root" (0x30004020) → res=0 → INLINE** (IGUAL a nós).

**Resolve a contradição:** a "Root" decide INLINE nos DOIS. A free-list da arena "Root" abrange a região da tabela de
objetos (`0x?0004480`) nos DOIS (o +4 cresce de `0x?0004050` para MB adentro). MAS na consola a tabela é um chunk
VÁLIDO (slot 14 = objeto), e em nós é um chunk CORROMPIDO (slot 16 = `0x84000013` = um nó da free-list: flag free
`0x80000000` | size `0x13`). Ou seja, em nós a **tabela de objetos (um chunk da arena) SOBREPÕE um nó da free-list** — a
arena deu chunks que se sobrepõem. Na consola a free-list é correcta e os chunks não se sobrepõem.

**Convergência (oráculo + código):** a decisão (`FUN_0026258c`), o threshold (`*0x83692c`), o SWAR strcmp
(`FUN_0037304c`), a arena principal, e o facto de a "Root" ser inline — TUDO BATE com a consola. A ÚNICA diferença é
que a **free-list da arena "Root" está corrompida em nós** (chunks sobrepostos; o chunk da tabela sobrepõe um nó livre
`0x84000013`), enquanto na consola está limpa. **É a corrupção "pool de handles" de §4a** — a tabela de objetos é a
vítima. Tudo o que refutei (~25 hipóteses) eram sintomas ou coisas que batem; o root é a corrupção da free-list da
arena, exactamente §4a-§4ah.

**Fix (o megaprojeto §4a, agora com o alvo confirmado):** a free-list da arena "Root" (`0x40004020`) dá chunks
sobrepostos em nós (o chunk da tabela `0x40004480` sobrepõe um nó livre). Achar onde a free-list ganha nós sobrepostos —
o `func_00263040` (find-fit/split) e o `func_00263178` (init dos nós) — e comparar o estado da free-list passo-a-passo
com a consola (que a mantém correcta). É o walk da corrupção da free-list, a via documentada (`walking-a-corruption-
upstream`), com o oráculo a dar o estado correcto.

**Estado:** [D]/[E] não alcançado. O root está oráculo-confirmado: a free-list da arena "Root" corrompe (chunks
sobrepostos; a tabela de objetos sobrepõe um nó livre), = §4a pool-de-handles. Tudo o resto (decisão, threshold, strcmp,
overlap) BATE com a consola. Avanço real committado: E275 (5 shells). O fix é o walk da free-list da arena vs consola.


## §4bu DADO CONCRETO (E313): a free-list da arena "Root" é um buffer SEPARADO (0x307D9D64) na consola; inline em nós

> **RETRACTADO por §4bv (E314).** A inferência "consola separada vs nós inline" caiu: medido com bp `0x263878`
> no return, as arenas de RECURSO da consola (`Hero`, `WAD_R_ShellA`) são **INLINE** (free-head = arena+0x30,
> span = size), IGUAL a nós. O `0x307D9D64` era o free-head do pequeno alocador "Root" de arranque **já avançado
> pelo uso**, não um buffer separado por design. SOBREVIVE: a consola cria ~13 arenas de recurso nomeadas
> (`func_00263878`, nome=param_3/r5), `WAD_R_ShellA`=5.9MB em `0x31803D50`, espalhadas ALTO no heap, LONGE da
> tabela de objetos (`0x30004480`). Não construir sobre a conclusão abaixo.

Oráculo (bp `0x263040`, filtrar `nome=="Root"`): a **free-list head (arena+4) da "Root" é ESTÁVEL em `0x307D9D64`**
ao longo de TODAS as 147 chamadas (r4=size=4). É um buffer SEPARADO e GRANDE (~8 MB, heap+0x7D9D64), longe da tabela de
objetos (heap+0x4480).

Em nós (E308): a "Root" arena+4 CRESCE de `0x40004050` (struct+0x30, INLINE) — o head avança consumindo, e passa pela
tabela de objetos (`0x40004480`), corrompendo-a. E o `FREELIST-TAG-GUARD` acaba por ver head=NULL (free-list esgotada).

**Diferença EXACTA (medida, oráculo):**
| | consola | nós |
|---|---|---|
| Root free-list head (arena+4) | `0x307D9D64` (buffer SEPARADO, estável) | `0x40004050` (INLINE, struct+0x30, cresce p/ a tabela) |
| natureza | buffer separado ~8 MB, longe da tabela | inline pequeno, sobrepõe a tabela → esgota (head=NULL) |

**A contradição por resolver (a única aberta):** `func_00263878` põe o buffer INLINE (arena+4 = struct+0xc = struct+0x30)
nos DOIS (mesmo código). Mas na consola a "Root" acaba com buffer SEPARADO em `0x307D9D64`. Logo a consola MOVE o buffer
para separado por um mecanismo que NÃO isolei — e não é `FUN_0026384c` ("Root" difere de "BP", não o recebe). Em nós o
buffer fica inline e sobrepõe/esgota.

**Próximo (o fix, agora com o alvo exacto):** achar o mecanismo que dá à "Root" um buffer SEPARADO (`0x307D9D64`) na
consola. Ou (a) `func_00263878` aloca um buffer separado quando `param_2` é grande (medir o `param_2` da "Root" — o probe
`ROOTSIZE` não disparou, a "Root" é criada cedo; usar o oráculo com bp em `0x263878` e connect a t=6-8), ou (b) uma
realocação pós-init move o buffer. Watch de `0x30004024` (consola) / `0x40004024` (nós) desde o BOOT para apanhar quem
escreve `0x307D9D64` vs quem deixa inline. É a diferença EXACTA e medida.

**Estado:** [D]/[E] não alcançado. A parede está reduzida a UMA diferença medida: a free-list da "Root" é um buffer
separado (`0x307D9D64`) na consola e inline (sobrepondo a tabela) em nós. O mecanismo que aloca o buffer separado na
consola é o fix. Avanço real: E275 (5 shells). O oráculo deu o alvo exacto.


## §4bv REFRAME MEDIDO (E314): func_00263878 cria as arenas de RECURSO nomeadas (WAD_R_*), inline, espalhadas alto — LONGE da tabela de objetos

Oráculo (bp `0x263878`, nome=param_3/r5, ler no return): a consola cria arenas de recurso, uma por resource do WAD,
cada uma um bloco contíguo grande com magic `0xC0DE1111`, free-head INLINE em arena+0x30, span=size:

| nome (r5) | arena | size (r4) | free-head@ret | end (+0x18) | span |
|---|---|---|---|---|---|
| Hero | 0x314FE148 | 0x1FF870 | 0x314FE178 (=+0x30) | 0x316FD9B4 | 0x1FF86C |
| WAD_R_Hero | 0x314FE180 | 0x1F9500 | 0x314FE1B0 (=+0x30) | 0x316F767C | 0x1F94FC |
| **WAD_R_ShellA** | **0x31803D50** | **0x5A85D4 (5.9MB)** | 0x31803D80 (=+0x30) | 0x31DAC320 | 0x5A85D0 |
| (13 no total: SubWea, SavePt, rhod10, Rhsold, Colsus, M_Eart/Medu/Wind/Lght…) | 0x31xxxxxx–0x32xxxxxx | | | | |

**Ponto-chave:** `WAD_R_ShellA` é a arena dos SHELLS do CGOWShader (o nosso crash é o 6.º shell). Na consola vive em
`0x31803D50` (18 MB dentro do heap), **LONGE da tabela de objetos** (`0x30004480`, junto à base). As 13 arenas
espalham-se por `0x314x–0x327x`. NENHUMA colide com a tabela de objetos.

**Hipótese a testar (o próximo passo, mede a colisão):** na consola a tabela de objetos (base do heap) e as arenas de
recurso (altas) não se sobrepõem. Em nós a tabela está em `0x40004480` e a corrupção pool-de-handles escreve
`0x84000013` nela. Se o NOSSO alocador puser uma arena de recurso (p.ex. `WAD_R_ShellA`, 5.9MB) BAIXA — em
`0x40004xxx`, sobre a tabela — a tabela é a vítima. Medir: instrumentar o NOSSO `func_00263878` (ppu_recomp_000.cpp)
para logar r3(arena)/r4(size)/r5(nome) e ver se alguma arena aterra perto de `0x40004480`. Comparar as 13 (nome+size)
com a consola: faltam? tamanho errado? endereço baixo?

**Estado:** [D]/[E] não alcançado. A parede reduz-se a: as 13 arenas de recurso da consola espalham-se alto e não
tocam a tabela de objetos; falta medir se as NOSSAS colidem com `0x40004480`. Avanço real: E275 (5 shells).


## §4bw WALK COMPLETO (E315): o crash do 6.º shell sobe até "size do recurso = 0xFFFFFFFF" -> arena WAD_R_ nunca criada

> **RETRACTADO em PARTE por §4bx (E316).** CAI a inferência de que "size=0xFFFFFFFF é lixo/corrupção = a raiz": o
> `0xFFFFFFFF` é o valor LEGÍTIMO do header-contentor do R_PermA (medido no ficheiro de cache: `r_perma.wad_ps3` em
> +0x20 = `ff ff ff ff`; type=21 nome "WAD_R_Perm"). `func_002BBA64` tomar BC40 para ele é CORRECTO (o contentor não
> cria arena fixa). SOBREVIVE tudo o resto medido: a stack do crash, os 5 shells, a consola criar 13 arenas com sizes
> válidos via func_002BBA64, o buffer 0x40083D40 escrito por movie_io_pread (R_LglScA e depois R_PermA), o cache
> R_LglScA[+0x20]=0x0007A120 vs R_PermA[+0x20]=0xFFFFFFFF. A raiz verdadeira está em §4bx.

Método walking-a-corruption, do uso (crash) até à produção (size garbage), com comparação de população consola vs nós:

**A cadeia (medida, de baixo para cima):**
1. **Crash:** `[ppu] FATAL: stuck calling 0x4BFFFE34` no 6.º shell CGOWShader. Stack: `func_002B9C80 → func_002B0DB0
   → func_0039E794 → func_0039D6F0 → func_0039F520`. Reg no crash: `this=0x400D7888`, `r27=0xD0800012` (tagged),
   `r11=0x4003ED70` (dentro do "RB NODE Heap" 0x4003ED08), write UNCOMMITTED a `0x84000017` (handle tagged usado como ptr).
2. **Root exausta:** `FREELIST-TAG-GUARD 263040 entry head=0x00000000` — a free-list da arena de onde os shells são
   alocados esgota (head NULL). Os shells caem na "Root" (0x40004020, 48MB) por falta da sua arena própria.
3. **A arena do recurso NUNCA é criada:** `PS3_TRACE_RSZ` (func_00263878) só mostra 5 arenas em nós
   (Root/BP/Debug/RB NODE Heap/Levels), ZERO `WAD_R_*`. Na consola cria 13 (`WAD_R_ShellA`=5.9MB em 0x31803D50 etc.).
4. **Onde a criação é saltada:** a arena vem de `func_002B9C80 (type=21) → func_002BA3F0 → func_002BBA64 → func_00263878`.
   `func_002BA3F0` (handler do type-21) é despachado (medido: `PS3_TRACE_TYMAP` → type=21 opd=0x52FAB8 code=0x2BA3F0)
   e chama `func_002BBA64` INCONDICIONALMENTE. Mas `func_002BBA64` (`PS3_TRACE_BBA64`) toma o **early-return BC40**
   (`func_002BBC40`, ppu_recomp_001.cpp:47290) e NUNCA chega a `func_00263878` (0x2BBB74). BC40 é tomado porque
   `r28 == -1` na cmp de 0x2BBA9C (r28 = key de entrada quando o global r11==0).
5. **RAIZ medida (população consola vs nós):**
   | | key(r3) em func_002BBA64 | resultado |
   |---|---|---|
   | consola (bp 0x2BBA64, 10 hits) | SIZES válidos: 0x1E1400, 0x800000, 0x57800, 0x78C00, 0x30C00… | cria 13 arenas |
   | **nós (1 hit)** | **0xFFFFFFFF (-1)** | BC40 → 0 arenas |

   O key (r3) é o **SIZE do recurso**, computado por `func_002BA3F0` de `vm_read32(buf+0)` (byte-swap rlwinm 8,24,31 + ORs).
   Em nós dá `0xFFFFFFFF` (all-ones = campo não inicializado / EOF). r5=0x008695F4 (base do nome "WAD_R_") bate nos dois.

**Conclusão:** o crash do 6.º shell é SINTOMA. A raiz está 5 níveis acima: o **size do recurso lido pelo type-21
handler é 0xFFFFFFFF** em vez do tamanho real, logo `func_002BBA64` decide "não criar" (BC40) e a arena per-recurso
(`WAD_R_ShellA`, 5.9MB) nunca existe; os shells caem na Root que esgota. NÃO é corrupção do pool-de-handles a montante
(isso é o sintoma final); é **dado de recurso errado** (size=0xFFFFFFFF) no ponto de dispatch do type-21.

**Retracta** a framing de §4be/§4a de que "o objeto do 6.º shell é alocado de um pool corrompido" como RAIZ — o pool
corrompido/head=NULL é consequência de a arena `WAD_R_ShellA` não existir. Sobrevive: os valores tagged medidos, os 5
shells que constroem, a stack do crash.

**Próximo (alvo exacto):** porque é que `vm_read32(buf+0)` (buf=0x40083D60, record type-21 #1) dá 0xFFFFFFFF em vez do
size? Duas hipóteses a discriminar: (a) **framing do R_PermA host-fill (E285)** — a entrega E275 (`PS3_WAD_HOST_READ`)
mete os dados do cache no ring com framing errado, logo o record type-21 lê o campo errado; (b) **ordem** — o objeto em
0x40083D60 é lido antes de preenchido. Medir no console (bp 0x2BA3F0) o `buf` e `vm_read32(buf+0)` do record que gera
size=0x1E1400, e comparar o layout do record com o nosso `0x40083D60`. Provas: `PS3_TRACE_TYMAP`, `PS3_TRACE_BBA64`,
`PS3_TRACE_RSZ` (probes gated OFF no lift gitignored; re-adicionar via os inserts deste E315).

**Estado:** [D]/[E] não alcançado, mas a parede desceu de "corrupção de pool inescrutável" para "1 leitura de size dá
0xFFFFFFFF no type-21 handler" — um alvo estreito e comparável com a consola. Avanço real: E275 (5 shells).


## §4bx CORREÇÃO + RAIZ CONFIRMADA (E316): R_PermA é um CONTENTOR; consumimos só os primeiros 256KB e nunca chegamos aos recursos internos (= E285)

Medido nos ficheiros de cache e no trace de leitura:

**R_PermA é um contentor de 20MB com 154 recursos WAD_R_* internos:**
- `r_perma.wad_ps3` +0x00: `15 00 00 00` (type 21) `08..` nome "WAD_R_Perm" +0x20: `ff ff ff ff` (size = **sentinela de
  contentor/stream**, não um tamanho fixo) +0x30: `02 00 00 00` "GroupStat…".
- `strings` do cache: **154 entradas WAD_R_*** (WAD_R_Weapon, WAD_R_SubWeapon, WAD_R_M_Earth…), nomes de recurso a
  offsets PROFUNDOS: SBI_Hero @0x11D418 (~1.2MB), a tabela WAD_R_ @0x132E74A (~19MB).
- Contraste: `r_lglsca.wad_ps3` (legal screen, 3072B) +0x20 = `20 a1 07 00` = size VÁLIDO 0x0007A120 (não é contentor).

**O que lemos (trace `PS3_TRACE_FIOS`, op=R_PermA=1297481730):** `off=0 len=131072`, `off=131072 len=131072`,
`off=0 len=262144` para buf=0x40083D40. **Máximo alcançado: ~256KB.** NUNCA lemos o offset 0x11D418+ (1.2MB) nem a
tabela de recursos a ~19MB. Logo o loader nunca vê os records dos recursos internos (ShellA/Hero/… com sizes válidos)
que criam as arenas — só processa o header do contentor + os primeiros records e depois crasha no 6.º shell.

**Ligação à consola:** os keys válidos de `func_002BBA64` na consola (0x1E1400, 0x800000, …) são exactamente esses
recursos INTERNOS do R_PermA, que a consola alcança porque consome os 20MB inteiros. Nós paramos aos 256KB.

**RAIZ (confirmada, = a parede [C]/[D] histórica E285):** a **profundidade de consumo do stream do R_PermA**. O ring do
estado 22 (0x4007FCD0) enche ~256KB e o cursor não avança -> sem refill -> nunca chega aos records internos ->
arenas WAD_R_* não criadas -> shells caem na Root -> Root esgota -> handle tagged -> `stuck calling 0x4BFFFE34`. A
cadeia inteira de §4bw (crash<-tagged<-Root esgota<-arena não criada<-BC40) é REAL e SINTOMA; a causa é a montante: não
streamamos o R_PermA fundo o suficiente. O `movie_io_pread off=0 len=262144` re-lê o início em vez de avançar
(cursor/refill parado — o mesmo defeito do E285: o reader `func_002B9C80`/`func_002B9C80`-consumer não avança o
cursor stream+8 que o fill segue -> avail=cap -> sem refill).

**Próximo (o fix real, agora com prova de que é ISTO):** fazer o stream do R_PermA avançar além dos 256KB — o F2B
host-fill / o reader do estado 22 têm de avançar o cursor do ring e refill até o loader alcançar os records internos
(offset 0x11D418+). Medir: `PS3_TRACE_FIOS` deve mostrar `off` a crescer (0, 256K, 512K, …, 19M), não a re-ler off=0.
Provas gated OFF: `PS3_TRACE_TYMAP`, `PS3_TRACE_BBA64`, `PS3_TRACE_RSZ`, `PS3_TRACE_FIOS`, `PS3_WATCH_STORE=0x40083D60`.

**Estado:** [D]/[E] não alcançado. A parede está CORRECTAMENTE identificada (= E285, profundidade do stream do R_PermA)
com evidência muito mais forte: os recursos internos estão a 1.2–19MB, lemos só 256KB. NÃO é corrupção de pool nova.
Avanço real: E275 (5 shells) — que agora se entende: os 5 shells vêm dos primeiros 256KB; o 6.º precisa de dados mais
fundos que nunca chegam.


## §4by REFUTAÇÃO + FIX PARCIAL + CARACTERIZAÇÃO NÍTIDA (E317): a arena não é a causa; a free-head da Root colide com a tabela de objetos

> **CORREÇÃO PARCIAL (E318, mesma sessão):** CAI o mecanismo "a free-head da Root colide com a tabela de objetos".
> Medido com `PS3_WATCH_STORE=0x40004024` (Root+4 = free-head): a free-head da Root **AVANÇA bem PARA ALÉM da tabela**
> — escreve `0x40004604 → 0x400047C4` (+0x20 cada), toda PASSADA a tabela `0x40004480`. Logo a free-list da Root está
> saudável; NÃO colide com a tabela. SOBREVIVE: a refutação da teoria arena-ordering (console 27 shells sem arena), o
> fix E316-REARM (3072->20MB), e que a free-head do console está a 8MB (0x307D9D64) vs a nossa baixa (~0x780). O que MUDA:
> o `head=NULL` do FREELIST-TAG-GUARD é de uma arena DIFERENTE da Root (a Root avança) — provavelmente a "RB NODE Heap"
> (0x4003ED08, ~256KB, o heap dos nós do registry-tree; o reg do crash tem r11=0x4003ED70 lá dentro). Falta CONFIRMAR
> qual arena esgota: fazer o guard 263040 logar `arena=gpr[31]` (não só head). A comparação de tamanhos console vs nós
> dessa arena pequena é o próximo passo — não a Root.

**Refutação (medida no console, bp 0x263878 arena-create + 0x39E794 shell-construct, ordem):** o console executa
**27 shell-constructs ANTES da 1.ª arena** (Hero @#28, Upgrade @#29) e NÃO crasha. Logo a arena `WAD_R_ShellA` em falta
**NÃO é a causa** do crash do nosso 6.º shell. RETRACTA a cadeia causal de §4bx/§4bw ("arena não criada -> shells caem na
Root -> crash"): o console constrói shells sem arena nenhuma. SOBREVIVE: R_PermA é contentor de 20MB; consumíamos 256KB;
o crash é um esgotamento da free-list da Root (head=NULL).

**Fix parcial REAL (E316-REARM, gated `PS3_F2B_REARM`, OFF):** o F2B host-fill ficava preso no tamanho do R_LglScA
(3072B) quando a MESMA FO (0x43018580/0x43018528) era reusada para o R_PermA (20MB): `g_f2b_fill_mfd/sz/file_pos` não
re-armavam. Fix (3 edits em ppu_recomp_001.cpp): flag `g_f2b_fill_rearm`; no site E275 WAD-HOST-READ, se `_fo==g_f2b_fill_fo
&& _sz>g_f2b_fill_sz` re-armar (mfd/sz, file_pos=0, rearm=1); em `f2b_stream_fill`, ao ver rearm, limpar o ring
(avail/cursor/write_pos=0). **Medido:** `[E316-REARM] sz->20169344`; `F2B-STREAM-FILL file_pos=262144/20169344` (antes
3072/3072). CORRECTO e necessário, mas **insuficiente sozinho**: o guest ainda crasha no 6.º shell antes de consumir
fundo (o fill pára a 256KB à espera de consumo que não vem).

**Caracterização nítida da RAIZ (medida, console vs nós):**
| | arena "Root" | free-head (arena+4) | tabela de objetos | relação |
|---|---|---|---|---|
| console | 0x30004020 | **0x307D9D64** (~8MB dentro) | 0x30004480 (arena+0x460) | free-head PASSOU a tabela; allocs nunca lhe tocam |
| **nós** | 0x40004020 | **0x40004050** (arena+0x30, o INÍCIO) | 0x40004480 (arena+0x460) | free-head 0x430 ANTES da tabela; ~5 allocs alcançam-na e corrompem-na -> head=NULL -> crash |

A free-head do console está 8MB dentro porque o console **já alocou ~8MB da Root** (o load completo dos 20MB do R_PermA)
antes deste ponto, empurrando a free-head para além da tabela. Nós, com o stream truncado a 256KB, alocamos quase nada,
a free-head fica no início e as allocs dos shells colidem com a tabela de objetos a arena+0x460. **É a corrupção
pool-de-handles de §4a, agora com o mecanismo EXACTO: a posição da free-head da Root vs a tabela de objetos, governada
pela QUANTIDADE alocada da Root — que depende da profundidade do stream do R_PermA.** Ou seja o E316 (stream depth) e o
§4a (pool corruption) são o MESMO problema por dois ângulos: sem o stream completo, a Root aloca pouco, a free-head não
passa a tabela, e a tabela é a vítima.

**Próximo (as duas frentes convergem):** (a) fazer o guest consumir/streamar o R_PermA fundo o suficiente para a Root
alocar ~8MB e a free-head passar a tabela — o E316-REARM é o 1.º passo; falta o guest não crashar antes de consumir (ou
pré-carregar mais para a Root); OU (b) perceber porque a tabela de objetos aterra a arena+0x460 (baixo) em nós vs efeito
equivalente no console — se a tabela fosse colocada DEPOIS da free-head (alto), não haveria colisão. Medir: a ordem
alloc-da-Root vs criação-da-tabela-de-objetos (quem escreve 0x40004480) nos dois. Probes gated: `PS3_F2B_REARM`,
`PS3_TRACE_FIOS/RSZ`, `FREELIST-TAG-GUARD`. Ledger §4a, §4bt–§4by.

**Estado:** [D]/[E] não alcançado. Ganhos reais: (1) refutada a teoria arena-ordering (console: 27 shells sem arena);
(2) fix E316-REARM do stream stuck (3072->20MB); (3) raiz caracterizada com precisão (free-head da Root vs tabela de
objetos, governada pela profundidade do stream). O §4a e o E285/E316 são a mesma parede por dois ângulos.


## §4bz CLARIFICAÇÃO DECISIVA (E319): o FREELIST-TAG-GUARD dispara pelo PONTEIRO DA ARENA (r31), não pela free-head — é o pool-de-handles de §4a

Lido o guard em `func_00263040` (ppu_recomp_000.cpp:565007): `uint32_t _head=(uint32_t)ctx->gpr[31]; if(_head==0 ||
_head<0x10000 || (_head&0x80000000))` -> o "head" do log **é o `r31` = o PONTEIRO DA ARENA ("this")**, NÃO a free-head
(`arena+4`). Logo `[FREELIST-TAG-GUARD] head=0x00000000` significa que **`func_00263040` é CHAMADO com um ponteiro de
arena NULL/tagged**, não que uma free-list esgotou. (O nome do log é enganador.)

**Consequência (fecha o círculo em §4a):** o alocador recebe uma arena tagged/NULL porque o CHAMADOR obteve um
**handle tagged** (`0x84000013`/`0x84000017`) de um lookup do registry (a árvore `func_002ACBE8` / a tabela de objetos
`PTR_DAT_0053fbdc`) e passou-o como arena. Isto é EXACTAMENTE a corrupção pool-de-handles de §4a–§4be: o objeto/arena do
6.º shell é um handle tagged. A Root está saudável (§4by-corrigido: a free-head avança 0x40004604->47C4 passada a
tabela); o problema é o LOOKUP devolver um handle tagged em vez do ponteiro real da arena/objeto.

**Ganhos REAIS e duráveis desta sessão (E313–E319):**
1. **Fix E316-REARM** (gated `PS3_F2B_REARM`): o F2B host-fill ficava preso no tamanho do R_LglScA (3072B) ao reusar a FO
   para o R_PermA; agora re-arma para 20MB (medido `file_pos=262144/20169344`). Bug real, corrigido.
2. **Refutada** a teoria "arena WAD_R_ em falta -> crash": o console faz 27 shell-constructs ANTES da 1.ª arena, sem crash.
3. **Refutada** a teoria "free-head da Root colide com a tabela": a free-head da Root avança bem para além da tabela.
4. **Clarificado**: o guard dispara pelo ponteiro de arena tagged (r31), = pool-de-handles §4a.
5. **Medido**: R_PermA é um contentor de 20MB (154 recursos internos); o 0xFFFFFFFF é o header legítimo do contentor.

**Próximo (o alvo real, sem os becos desta sessão):** o pool-de-handles §4a — porque é que o lookup do registry
(`func_002ACBE8` árvore / tabela `0x53fbdc`) devolve `0x84000013` (handle tagged) para o objeto/arena do 6.º shell,
quando os 5 anteriores resolvem para ponteiros reais. Comparar consola vs nós no lookup do 6.º (a chave do 6.º vs as dos
5). NÃO é: stream depth (E316 tratado), arena-ordering, ou free-head da Root (todos refutados/tratados aqui).


## §4ca CARACTERIZAÇÃO MAIS FINA DE §4a (E320): a tabela de objetos é uma pilha de SCOPE; o nosso índice é 16 vs 14 na consola e lê um slot tagged

Lido `func_002AAB64` (o lookup que produz o objeto do shell, ppu_recomp_001.cpp:30919): devolve **`*(base + index*4)`**
onde `base=*(tbl+4)`, `index=*(tbl+8)`, `tbl=*(r2-0x159C)=0x868C00`. É o **topo de uma pilha de scope/namespace** (índice
= scope corrente), não uma free-list. `func_002AAB84` faz `index+1` (push), `func_002AAC84` faz `index-1` (pop).

**Medido (oráculo bp 0x2AAB64, consola):** `tbl=0x868C00 base=0x30004480 index=14` (ESTÁVEL nas 30 chamadas)
`slot[14]=0x30636900` (objeto REAL), `slot[15]=0x3003ECF8` (válido, RB-NODE-Heap), slots[12,13]=0. O índice NÃO cresce
durante os constructs — é um scope fixo (14).

**Nós (medido antes, §4be):** `base=0x40004480 index=16`, `slot[16]=0x84000013` (TAGGED, free-list boundary). O nosso
índice de scope é **16 vs 14** — 2 acima — e lê um slot NUNCA inicializado (tag). Daí o objeto do 6.º shell ser um handle
tagged -> lookup do registry devolve 0 -> dispatch em lixo -> `stuck calling 0x4BFFFE34`.

**RAIZ afinada (o alvo exacto para [D]):** porque é que o nosso índice de scope da tabela de objetos chega a **16** quando
a consola fica em **14**? São 2 pushes a mais (ou 2 pops a menos), ou 2 namespaces extra entrados durante o parse do
nível. Os slots 14/15 (e 16) na consola têm ponteiros reais (0x30636900/0x3003ECF8); em nós o 16 é tag. Próximo:
instrumentar `func_002AAB84`/`func_002AAC84` (push/pop do índice em 0x868C08) — logar cada push/pop com o `ra` e o valor
escrito no slot — e comparar a sequência de scope-enter/exit consola vs nós até ao 6.º shell. O desequilíbrio (ou o
namespace extra) é a causa do §4a. (O watch de 0x868C08 não apanhou escritas nesta corrida — o índice muda cedo/raro;
usar probe nas próprias funções push/pop.)

**Estado:** [D]/[E] não alcançado. §4a agora caracterizado ao nível do mecanismo: pilha de scope da tabela de objetos,
índice 16 (nós) vs 14 (consola), topo tagged. Ganhos da sessão intactos (fix E316-REARM; 3 teorias refutadas; R_PermA
contentor). O próximo passo é o desequilíbrio push/pop do índice de scope — estreito e comparável com a consola.


## §4cb ROOT DECISIVO (E321): a arena ativa é a Root em vez da BP — os shells alocam da MESMA arena que a tabela de objetos

Trace completo do crash até ao alocador: `func_002AAC84` (regista objeto) chama `func_00263B70` (alloc 8B) que chama
`func_00262808()` (provedor da arena do scope corrente) e depois `func_00263040(arena, 8)`. `func_00262808` lê a pilha de
scope de arena em `*(r2-0x20D8)`: `base=*(stk+0)`, `index=*(stk+4)`, `limit=*(stk+8)`; devolve `*(base+index*4)` (ou 0 se
`index>=limit`).

**MEDIDO (a MESMA pilha `0x008370A8` nos dois, é uma estrutura de dados fixa):**
| | index | limit | arena devolvida (`func_00262808`) |
|---|---|---|---|
| consola (bp 0x262808) | 126 | 128 | **0x33004020 = BP** |
| **nós** (`PS3_TRACE_ARENASTK`) | 127 | 128 | **0x40004020 = Root** |

**A RAIZ:** os shells (e os objetos do registry) alocam da arena **Root** em nós, mas da **BP** na consola. A tabela de
objetos vive DENTRO da Root (`0x40004480`, = Root+0x460). Logo em nós as allocs dos shells vêm da MESMA arena que contém
a tabela de objetos -> colidem/corrompem a tabela -> os slots ficam com boundary tags `0x84000013` -> o objeto do 6.º
shell é um handle tagged -> lookup do registry devolve 0 -> `stuck calling 0x4BFFFE34`. Na consola os shells alocam da BP
(arena SEPARADA da Root/tabela) -> sem colisão -> 27 shells constroem sem crash.

**Mecanismo exacto:** a nossa pilha de scope de arena está em **index=127 (Root no topo)** quando devia estar em
**index=126 (BP no topo)** como a consola — **um push de arena a mais** (Root empurrado quando não devia, ou um pop de
B->Root a menos). É a origem única do §4a inteiro: não é corrupção aleatória do pool, é a **arena ATIVA errada** (Root em
vez de BP) por um desequilíbrio de 1 no push/pop da pilha de scope de arena (`*(r2-0x20D8)`).

**Próximo (o fix, alvo exacto):** achar o push/pop da pilha de scope de arena (`*(r2-0x20D8)`, index em +4) que difere —
porque é que ficamos em 127/Root quando a consola fica em 126/BP. Instrumentar cada push (index+1) e pop (index-1) com o
`ra` e a arena empurrada; comparar a sequência enter/exit de scope de arena consola vs nós até ao 6.º shell. O push de
Root a mais (ou o pop em falta) é o defeito NOSSO. Provas: `PS3_TRACE_ARENASTK` (func_00262808), oráculo bp 0x262808.

**Mecanismo do push (E321b):** `func_00263878` (init de arena) **empurra cada arena criada** para a pilha de scope
(`*(r2-0x20D8)`, index+1, linhas 565985-565991). Logo o index (127 nós / 126 consola) é o net de todos os pushes de
arena menos os pops de scope. O topo difere: em nós **Root está no slot 127** (re-empurrada tarde — Root é criada 1.º,
slot ~0), na consola **BP está no 126**. O desequilíbrio de 1 (Root no topo em vez de BP) é o defeito: um scope que
entramos/re-empurramos e a consola não, ou um pop em falta. Traçar a sequência push/pop de scope de arena (126 níveis)
consola vs nós é a tarefa focada seguinte.

**Estado:** [D]/[E] não alcançado, mas a RAIZ do §4a está isolada a UMA divergência medida e comparável: a arena ativa é
Root (nós) vs BP (consola), por um desequilíbrio de 1 na pilha de scope de arena. Todos os becos da sessão (arena WAD_R_,
0xFFFFFFFF, free-head da Root, stream depth) foram refutados ou tratados; este é o defeito real. Fix E316-REARM mantido.


## §4cc DIFF PUSH/POP CONSOLA vs NÓS (E322): a consola corre os shells num loop BALANCEADO a base 125; nós temos 1 push a mais (base 126) num caminho DIFERENTE

Medido: pilha de scope de arena, push=`func_00262C8C` (0x262C8C, index+1), pop=`func_00262C64` (0x262C64, index-1).

**Consola (oráculo, bp 0x262C8C/0x262C64):** loop PERFEITAMENTE BALANCEADO à volta de cada shell:
`PUSH idx 125->126 (caller 0x2E0C18 <- 0x0039E770)` depois `POP idx 126->125 (caller 0x2E0BF4 <- 0x0039E770)`, dezenas de
vezes. Os shells constroem via **`func_0039E770`** (o construtor de shell, através do wrapper 0x2E0C18) a **base index 125**
(arena BP no 126). Balanceado: o índice volta sempre a 125.

**Nós (`PS3_WATCH_STORE=0x008370AC`, o índice em stk+4):** oscila a **126<->127** (base 126, +1 vs consola) e os
push/pop perto do crash vêm de **`func_002BB884`, `func_002BA3F0`, func_00053994, func_002AC364** — o caminho do
get-or-create de arena de recurso (`func_002BBA64`, onde o contentor 0xFFFFFFFF do R_PermA tomou BC40), **NÃO** do
`func_0039E770` limpo da consola. A última op antes do crash é um PUSH (126->127) desse caminho.

**RAIZ afinada:** os dois têm o loop de shell de `func_0039E770` balanceado, mas nós corremo-lo sobre uma **base 1 mais
alta** (126 vs 125) porque o nosso fluxo entra num scope EXTRA de recurso-WAD (via `func_002BB884`/`func_002BA3F0`) que
fica aberto — a consola não o tem aberto durante os shells. A base+1 expõe a arena **Root** (slot 127) em vez de **BP**
(slot 126). Como os shells alocam da arena do scope corrente (`func_00262808`), a base errada = arena errada (Root, que
contém a tabela de objetos) = a colisão de §4cb.

**Porquê o scope extra:** liga-se ao processamento do R_PermA. O nosso stream truncado a 256KB (§4bx) + o contentor
0xFFFFFFFF a tomar BC40 em `func_002BBA64` levam o loader a um caminho de construção diferente (`func_002BB884`) que
empurra um scope de recurso a mais e nunca o fecha (crasha lá dentro). A consola, com os 20MB e os recursos internos,
percorre o caminho limpo `func_0039E770` a base 125.

**Próximo (o fix):** (a) achar o push de `func_002BB884` (0x2BB8B8 -> func_00262C8C) que fica sem pop no nosso caminho e
porquê (é o scope do recurso cujo arena não foi criado por BC40?); OU (b) a montante: fazer o R_PermA streamar fundo
para o loader seguir o caminho limpo `func_0039E770` da consola (= a frente E316 do stream depth). As duas frentes (scope
de arena §4cb e stream depth §4bx) são o MESMO defeito: o R_PermA truncado leva ao caminho de construção errado com um
scope de recurso a mais.

**Estado:** [D]/[E] não alcançado. Root isolada a: base do scope de arena 126 (nós) vs 125 (consola), por um scope extra
de recurso-WAD aberto no caminho `func_002BB884` (nosso), ligado ao R_PermA truncado. Fix E316-REARM mantido (necessário,
insuficiente). Diff push/pop consola vs nós é a prova.


## §4cd FECHO DO CÍRCULO (E323): o crash 0x4BFFFE34 é o indirect call vtable+0x4C de func_0039E6B4 sobre o objeto tagged

`func_0039E6B4` (o wrapper de construct de shell na consola) faz `r9=vtable; r11=*(r9+0x4C); ps3_call_opd(r11)` — um
INDIRECT CALL via **vtable+0x4C**. Na consola vtable+0x4C = `func_002E0BE8` (o wrapper que faz o push de scope +
construct, 0x2E0C18 no stack). Em nós, para o 6.º shell, o objeto/vtable é o handle TAGGED (§4cb) e vtable+0x4C resolve
para lixo `0x4BFFFE34` -> `stuck calling 0x4BFFFE34`.

**A cadeia unificada COMPLETA (medida, consola vs nós):**
1. R_PermA truncado a 256KB (§4bx) -> o loader segue um caminho de construção diferente.
2. Esse caminho (`func_002BB884`/`func_002BA3F0`, get-or-create de arena onde o contentor 0xFFFFFFFF tomou BC40) empurra
   um scope de recurso EXTRA que fica aberto (§4cc) -> a base do scope de arena é 126 (nós) vs 125 (consola).
3. Base+1 -> `func_00262808` devolve a arena **Root** (slot 127) em vez de **BP** (slot 126) (§4cb).
4. Os objetos dos shells alocam da Root, que CONTÉM a tabela de objetos (0x40004480) -> colisão -> slots com boundary
   tags `0x84000013` (§4a).
5. O objeto do 6.º shell é um handle tagged -> a sua vtable é lixo -> `func_0039E6B4` faz indirect call a vtable+0x4C =
   `0x4BFFFE34` -> crash (§4cd). E `func_002AAB98`/`func_002ACBE8` (lookup do registry) devolvem 0 pela mesma razão.

Na consola: R_PermA completo -> caminho limpo -> base 125 -> arena BP (separada) -> objetos limpos -> vtable+0x4C =
func_002E0BE8 -> 27 shells constroem, e a partir do 28.º as arenas WAD_R_* criam-se e o registry popula.

**A FRENTE DE FIX (uma, com duas alavancas):** fazer o R_PermA streamar fundo (frente E316, já com o re-arm 3072->20MB)
para o loader seguir o caminho limpo da consola; OU cortar o scope de recurso extra do caminho BC40. As duas são a mesma
parede. É o pré-requisito de [D] (registry popula) e [E] (compiles do nível).

**Estado final da sessão (E313–E323):** [D]/[E] não alcançado, mas a parede está caracterizada de ponta a ponta com
medição consola-vs-nós em CADA nível, e um fix real entregue (E316-REARM). 6 teorias refutadas por medição. A raiz é uma:
o R_PermA truncado leva ao caminho de construção errado (scope de arena base 126 vs 125 -> arena Root vs BP -> tabela de
objetos corrompida -> objeto/vtable tagged -> 0x4BFFFE34).


## §4ce PROVA DA RAIZ + CRASH ELIMINADO (E324): redirecionar a arena Root->BP remove o 0x4BFFFE34 e o boot chega ao MENU

Experimento confirmatório (gated `PS3_ARENA_ROOT2BP`, OFF por default, DIAGNÓSTICO declarado): em `func_00262808`
(provedor da arena do scope corrente), quando devolve `0x40004020` (Root) redireccionar para `0x43004020` (BP) — ou seja
forçar a arena que a CONSOLA usa (§4cb). **Medido:**
- **O crash `stuck calling 0x4BFFFE34` DESAPARECE** (0 ocorrências vs sempre presente).
- O boot **PROGRIDE até ao menu**: o filme toca (330 frames iosurface), `EOS`, `[CONTENT] boot logo queue START scep_e.ctxr`,
  `bluepoint.ctxr`, `[CONTENT] boot logo queue DONE — hold cleared (guest/menu)`. O crash bloqueava o FLUXO INTEIRO.
- Os shells constroem (post#1..#5) sem crash; a construção não crasha, apenas **pára aos 5** (o `FREELIST-TAG-GUARD`
  ainda apara arenas tagged residuais 25x, mas sem crash).

**PROVA:** o crash 0x4BFFFE34 é CAUSADO pela alocação dos objetos do shell da arena **Root** (que contém a tabela de
objetos) — redireccionar para BP (como a consola) elimina-o. Confirma toda a cadeia §4cb–§4cd: arena errada (Root vs BP)
-> colisão com a tabela -> handles tagged -> crash.

**O que AINDA falta para [D]:** com o crash removido, o limitador passa a ser o **stream do R_PermA capado a 256KB**
(`file_pos=262144/20169344`, o guest não consome mais e segue para o menu). Só 5 shells, 0 arenas WAD_R_*, registry não
totalmente populado. [D] (registry cheio + compiles do nível) precisa do **load completo do WAD do nível** — que acontece
depois do menu (ao entrar num nível) ou exige o stream mais fundo no preload. As duas frentes (arena §4cb + stream §4bx)
continuam a ser o mesmo defeito, agora com o crash desbloqueado.

**Estatuto do ROOT2BP:** é um DIAGNÓSTICO gated (prova a raiz, desbloqueia o boot), NÃO o fix próprio. O fix próprio é
corrigir o índice do scope de arena (126 vs 127, §4cc) para `func_00262808` devolver BP naturalmente — ou o stream deep
que leva o loader ao caminho limpo `func_0039E6B4` da consola. Patch: `patch_e324_arena_root2bp.py` (gated OFF).

**Estado:** avanço grande — o crash que bloqueava tudo desde E287 está ELIMINADO (com prova da raiz) e o boot chega ao
menu. [D]/[E] ainda não (falta o load completo do nível), mas a barreira imediata caiu. Fixes reais: E316-REARM + a prova
E324.


## §4cf O QUE O CRASH-FIX REVELOU (E325): o boot chega ao menu mas LOOPA; [D] depende agora do load de nível (parede [G])

Com o crash removido (`PS3_ARENA_ROOT2BP`) + `PS3_PAD_AUTOSTART=1` + 90s: o boot chega ao handoff boot-logo/menu
(`[CONTENT] boot logo queue DONE — hold cleared (guest/menu)`) mas **LOOPA** aí (re-arma a fila do boot logo em ciclo);
NÃO entra em gameplay. Medido: continua **5 shells, R_PermA a 256KB (file_pos=262144), 0 arenas WAD_R_*, registry
não populado**.

**Diagnóstico do gap [D]:** o R_PermA (WAD permanente, 154 recursos de gameplay) só carrega COMPLETO ao entrar num
nível. A consola-oráculo é **skip-intro = já dentro do 1.º nível** -> carrega os 20MB -> 27 shells + 13 arenas ->
registry. O nosso boot faz **intro -> menu** e LOOPA no handoff; os 256KB de R_PermA que streamamos são o preload do
intro, não o load completo do nível. Logo [D] (registry cheio + compiles do nível) **não é alcançável no boot atual sem
passar do menu para gameplay** — que é a parede [G] (transição do estado da aplicação, `0x0052F3F0` eterno / relógio de
áudio do SCREAM), distinta da parede [C]/[D] do crash.

**O que o crash-fix (E324) DESTRAVOU:** o crash `0x4BFFFE34` bloqueava TUDO desde E287 — antes nem se chegava ao menu.
Agora chega-se, e o próximo bloqueador é o loop menu->gameplay ([G]). É progresso real na cadeia de paredes, mesmo que
[D] fique subordinado a [G].

**Duas frentes para [D], agora ordenadas:** (1) parede [G]: fazer o boot transitar do menu para gameplay (entrar no 1.º
nível) — então o R_PermA carrega completo e o registry popula naturalmente; OU (2) replicar o caminho skip-intro da
consola no nosso boot (carregar o nível directamente), à la `tools/rpcs3_probe` mas no nosso runtime. A frente da arena
(§4cb, o índice de scope 126 vs 127) e do stream (§4bx) continuam a ser o fix próprio do crash (substituir o ROOT2BP
diagnóstico), mas mesmo resolvidas, [D] precisa do load de nível.

**Estado final da sessão (E313–E325):** avanço grande e PROVADO — o crash que bloqueava tudo está eliminado (raiz
provada, boot chega ao menu). [D]/[E] não alcançado porque agora dependem da parede [G] (menu->gameplay), revelada pelo
crash-fix. Fixes/provas entregues: E316-REARM (stream 3072->20MB), E324 (prova da raiz Root->BP), 6 teorias refutadas.


## §4cg PROVA MEDIDA: o stall a 256KB NÃO é bug de stream — o loader só precisa de 256KB no intro (E326)

Reconsiderei se o stall do R_PermA a 256KB era um bug de refill (o que tornaria [D] alcançável no intro sem [G]).
**Medido (run ROOT2BP), REFUTA a hipótese de bug:**
- `F2B-STREAM-EMPTY = 0`: o reader do ring (`func_002E1480`) NUNCA famintou (avail==0 && need>0). Nunca pediu mais do
  que tinha.
- `F2B-STREAM-CLAMP = 0`: nunca pediu mais do que avail.
- `func_002E1480` ATUALIZA o cursor (+0x8) e o avail (+0x10) correctamente (lido no lift: vm_write32(+0x10) e
  vm_write32(+0x8) nos dois ramos). O reader consome bem; as 437 iterações consumiram os 256KB.
- `F2B-STREAM-EOF-DONE = 0`: o stream não foi marcado drenado cedo.

**Conclusão:** o loader consome os 256KB e **para de pedir dados** — não é starvation nem refill partido. O loader só
precisa de 256KB nesta fase (intro/preload). O `file_pos/20169344` mostra que o fill SABE que o ficheiro tem 20MB, mas o
loader não os consome todos agora. (Isto reconcilia o E285: o "no refill → crash" era o caminho do CRASH; sem crash
(ROOT2BP) o loader consome 256KB limpo e segue para o menu.)

**Confirma §4cf:** [D] (registry cheio: 27 shells + 13 arenas WAD_R_*) precisa do **load COMPLETO do R_PermA**, que só
acontece ao **entrar num nível** — a consola-oráculo é skip-intro=in-level e por isso carrega os 20MB. O nosso boot
loopa no menu (parede [G]) e nunca entra no nível, logo o R_PermA fica nos 256KB do preload. **[D] é genuinamente
subordinado a [G]** (menu→gameplay), provado por medição, não é um beco por conveniência.

**O caminho real para [D] (definitivo):** vencer [G] — fazer o boot transitar do menu para gameplay (entrar no 1.º
nível). Então o loader entra na fase de load de nível, consome os 20MB do R_PermA, constrói os 27 shells + 13 arenas, e o
registry popula naturalmente. O fix do crash (E324) é pré-requisito (senão crasha ao 6.º shell no nível). O fix próprio da
arena (§4cb, índice de scope) substitui o ROOT2BP diagnóstico.

**Estado:** a cadeia está fechada e provada de ponta a ponta. [D]/[E] não alcançado, subordinado a [G] (medido). Avanço
da sessão: crash eliminado (raiz provada), boot ao menu, e a prova de que [D] precisa de [G].


## §4ch A PAREDE [G] CARACTERIZADA (E327): o jogo CORRE mas cicla os logos de arranque, não entra em gameplay

Com o crash removido (ROOT2BP) + autostart + trace de estado, medido o que bloqueia [G]:
- **O guest CORRE de verdade:** 2023 `SetFlipCommand` (flips), `draw_indexed`, 3784 `[SPUJOB] returned cleanly`, 434
  `[LFQ] push`, `[spu_workload] dispatch HIT` (SCREAM SPU workload entry=0x03050 a RUNNING). Não está congelado.
- **O input É injectado:** `PS3_PAD_AUTOSTART` bate START+CROSS ~8 frames a cada ~3s (cellPad.c:456). O jogo RECEBE o
  input mas não avança.
- **O FSM do filme CICLA os logos:** `st620` faz `0->1->11->3->11->0` em ciclo (SmLogo/scep_e/bluepoint), re-armando a
  fila de boot logo. Não sai do ciclo dos logos para o título/menu/gameplay.
- SCREAM PS3 (v5.5.2) carregado; o SPU workload do mixer corre.

**Conclusão [G]:** o jogo passa o crash, corre o loop de render, toca os logos e recebe input — mas o FSM de front-end
não transita dos logos para gameplay. É a parede [G] (transição do estado da aplicação), tocando o FSM do filme e
provavelmente o relógio de áudio do SCREAM (o "próximo passo 0" do CLAUDE.md: a posição das vozes que o mixer SPU nunca
escreve, deixando o relógio a 0 e o front-end sem o tick que gateia a transição). Caracterizar porque o ciclo de logos
não sai é a tarefa focada de [G] — separada e grande.

**A CADEIA DE DEPENDÊNCIA COMPLETA (provada esta sessão):**
`[E] pixels de jogo <- [D] registry cheio <- load COMPLETO do R_PermA <- fase de load de NÍVEL <- [G] menu/logos->gameplay
<- transição do estado da app <- (FSM do filme sai do ciclo + relógio de áudio do SCREAM avança)`.
E, em paralelo, o **crash [C]/[D] (0x4BFFFE34) está FIXO (E324)** — era o que impedia sequer chegar aqui.

**Estado final (E313–E327):** avanço grande e provado — o crash que bloqueava tudo desde E287 está eliminado (raiz
provada), o boot corre até ao loop de logos/menu, e a cadeia inteira até [D]/[E] está mapeada e medida. [D]/[E] não
alcançado: subordinado a [G] (provado), que por sua vez toca o relógio de áudio do SCREAM. Fixes/provas: E316-REARM,
E324 (crash+raiz), 7 teorias refutadas, a cadeia [E]<-[D]<-[G] provada.


## §4ci CORREÇÃO + FRONTEIRA DA SESSÃO (E328): o loop de logos é overlay HOST (cosmético); o guest está VIVO; [G] = progressão do estado do guest

Correção a §4ch: o `[CONTENT] boot logo queue` é o **overlay HOST** (`libs/video/rsx_host_content.c`,
`rsx_metal_backend.m:3204`), que re-arma no fim de cada movie — **cosmético**, não o guest a ciclar. O **guest está VIVO
e a renderizar** (2023 flips, draw_indexed, SPU jobs, SCREAM a correr). Logo [G] não é "o guest preso num loop de logos" —
é a **progressão do estado da aplicação do guest** do front-end para gameplay, que (docs + CLAUDE.md próximo passo 0)
depende do **relógio de áudio do SCREAM** (a posição das vozes que o mixer SPU nunca escreve -> relógio a 0 -> o tick que
gateia a transição não avança). O skip-intro NÃO ajuda: só remove a espera do intro (`func_000CE0A0`, `*0x00592668==4`),
que nós já fazemos via FORCE/EOS; o divergência é POST-intro, no nosso front-end recompilado (o RPCS3 progride para
nível, nós não).

**Fronteira honesta da sessão (E313–E328):** vencer [G] (progressão do estado do guest -> relógio de áudio do SCREAM) é
o próximo passo 0 documentado — um marco grande e separado (liftar o PM do mixer SCREAM como spu6, dar-lhe o contexto de
kernel SPURS). Está fora do que é produtivo continuar nesta sessão sem começar esse marco de raiz.

**O que ESTA sessão entregou e provou (resumo):**
1. **Crash `0x4BFFFE34` ELIMINADO** (E324, raiz provada: shells alocavam da arena Root em vez de BP) — desbloqueou o boot
   desde E287.
2. **Cadeia de dependência COMPLETA e medida:** `[E]<-[D] registry<-load de nível<-[G] guest state->gameplay<-relógio de
   áudio SCREAM`.
3. **[D] subordinado a [G] PROVADO** (E326: `F2B-STREAM-EMPTY=0`, o loader só precisa de 256KB no intro; o registry só
   popula no load de nível).
4. **Fix E316-REARM** (F2B stream 3072->20MB) + patches idempotentes gated.
5. **7 teorias refutadas por medição** (marcadas no lugar).

[D]/[E] não alcançado — subordinado a [G] (relógio de áudio SCREAM), com o crash que o precedia agora removido.


## §4cj AVANÇO NO PRÓXIMO PASSO 0: o PM do mixer SCREAM liftado + registado como spu6 (E330)

Depois de provar que [D] depende de [G] (§4cg), ataquei [G] e encontrei o hook EXACTO: `cellSpursReadyCountStore(wid=0)`
reporta **"PM sem imagem SPU liftada registada"** (`fp=0xCEDB9A67A0C3A305 pm=0x4FD980 size=11520`) — é o PM do mixer
SCREAM, EXATAMENTE o "próximo passo 0" do CLAUDE.md, byte-a-byte. O comentário do código: sem imagem, o game "livelocka
no GetWorkloadInfo pós-AUTO_LOAD" — ou seja o guest FAZ AUTO_LOAD (carrega nível!), submete a workload do mixer, e espera
por ela; sem imagem SPU não corre → livelock → o estado não avança → nunca chega ao load completo do R_PermA (§4cg).

**Feito (E330):** (1) dump do PM via `PS3_DUMP_MISS_PM` (probe em cellSpurs.c); (2) lift com `spu_lifter --base 0x3000
--symbol-prefix spu6_ --extra-funcs <34 targets>` → **34 funções, compila OK** (entry `spu6_spu_func_00003000` = LS
0x3000); (3) wire: `spu_lifted/spu6_v2/` (auto-apanhado por build_macos.sh como os spu0-5) + registo em
`gow2_spu_register.c` gated `PS3_SPU6`. Medido: `[spu_workload] registered 'gow2_spu6' fp=0xCEDB9A67A0C3A305 (3 no
total)` — a imagem está REGISTADA (o "sem imagem" desaparece), sem crash.

**Aberto (o que falta para spu6 CORRER):** com `PS3_SPU6=1` o spu6 regista mas **ainda não dispatcha** (0 HIT p/ 0xCEDB).
Duas razões, alinhadas com os docs: (a) as corridas de teste não alcançaram a fase AUTO_LOAD/wid=0 (o boot varia; ficou
nos boot logos); (b) o dispatcher precisa de um **caminho para PMs crus** (LS 0x3000, contexto do kernel SPURS, `data` em
r4) — o caminho normal dos spu0-5 pode não bastar; e o PM carrega a imagem real por DMA de `0x886280`. Próximo: (i)
garantir que o boot chega ao wid=0 (correr mais longo/estável); (ii) dar ao `spurs_kernel`/dispatcher o contexto de PM cru
para o spu6; (iii) resolver o DMA de `0x886280`.

**Estado:** avanço concreto e committável no marco que gateia [D] (não apenas diagnóstico): o PM do mixer SCREAM está
liftado (34 fns) e registado como spu6. É a primeira vez que o PM que produz o relógio de áudio existe no nosso binário.
Falta o dispatch (caminho de PM cru + DMA). [D]/[E] continua não alcançado, mas o "próximo passo 0" saiu do papel.


## §4ck GATE ANTES DO spu6: o boot não alcança AUTO_LOAD/ReadyCountStore(wid=0) de forma fiável (E331)

Testar o spu6 exige que o boot chegue a `cellSpursReadyCountStore(wid=0)` (o pedido de execução da workload do mixer, que
vem DEPOIS do AUTO_LOAD). Medido: `AddWorkload(wid=0, pm=0x3004fd980, size=11520)` acontece SEMPRE (o PM é adicionado) e o
`[spurs kernel] started`, mas `ReadyCountStore(wid=0)` **é intermitente** — a corrida do E326/ldr.log alcançou-o (o "sem
imagem" disparou), mas 4 corridas E330–E331 ficaram presas nos boot logos (`st620 0->0` idle com overlay_done=1) e nunca
chegaram ao AUTO_LOAD. Os 5 shells registam (registry com 5 entradas, `cur=-1` não resolvidas) mas o `ReadyCountStore
(wid=0)` não corre.

**Consequência:** há um GATE de progressão de front-end ANTES do spu6 — o FSM dos logos (st620) tem de sair do idle para
o AUTO_LOAD. Sem isso, o spu6 (mesmo registado) nunca é despachado. É outra peça da parede [G], a montante do relógio de
áudio.

**Cadeia [G] refinada (peças, todas medidas):**
1. FSM dos logos (st620) sai do idle -> AUTO_LOAD (intermitente/preso em nós).
2. AUTO_LOAD -> `ReadyCountStore(wid=0)` submete a workload do mixer.
3. o dispatcher corre o **spu6** (PM cru, LS 0x3000, contexto SPURS, DMA de 0x886280) -> escreve a posição das vozes.
4. o relógio de áudio avança -> o estado da app transita -> load completo do R_PermA -> [D] registry popula + compiles.

**Feito esta sessão nessa cadeia:** peça 3 tem agora a imagem (spu6 liftado+registado, §4cj); peças 1, 2 (fiabilidade) e o
dispatch/DMA da 3 ficam. **Estado:** [D]/[E] não alcançado; a parede [G] é multi-peça e a progressão do front-end até
AUTO_LOAD é o gate imediato (a montante do spu6). Committado: crash fix (E324), spu6 (E330), provas e medições E313–E331.


## §4cl CORREÇÃO MEDIDA: sem spu6 o game passa o wid=0 (clear) e mesmo assim só 5 shells (E332)

Teste discriminador (2 corridas SEM spu6 vs 3 COM, mesmo env): **SEM spu6** o boot alcança `ReadyCountStore(wid=0)`
FIÁVEL (2/2) e toma o caminho "sem imagem → clear (readyCount=0) → waiters veem idle" (o comentário do código:
"Clear so waiters see idle" evita o livelock do GetWorkloadInfo). Ou seja **o game PROCEDE para além da espera do wid=0
sem o spu6** — e mesmo assim só constrói **5 shells** (registry 5 entradas não resolvidas, 0 arenas WAD_R_*).

**Consequência (corrige a hipótese de §4cj/§4ck):** o relógio de áudio do SCREAM (spu6) **NÃO está provado como o gate
único** de [D]. Sem ele, o game passa o wid=0 via clear e continua — logo o load completo do R_PermA (27 shells + 13
arenas) NÃO é bloqueado só pela ausência do mixer. SOBREVIVE: o spu6 está liftado+registado (peça válida, §4cj); o AddWorkload
(wid=0)=SCREAM PM confirmado. CAI: "spu6 desbloqueia [D]" como afirmação — não medido; o game já prosseguia sem ele.

**Observação lateral:** COM spu6 registado, 2/2 corridas alcançaram os 5 shells mas NÃO o `ReadyCountStore(wid=0)` (vs
2/2 SEM spu6 que o alcançaram) — sinal de que a wiring do spu6 pode alterar o timing/fluxo (a investigar; pode ser
não-determinismo com n pequeno). Gate PS3_SPU6 fica OFF por default, sem risco no baseline.

**O gate REAL de [D] (reafinado):** o registry só enche no load COMPLETO do R_PermA, que só acontece com o game DENTRO de
um nível. O boot chega ao menu/logos mas a transição front-end→gameplay não completa — e (medido aqui) NÃO é só o relógio
de áudio (o game passa o wid=0 via clear). Falta identificar o que, no front-end, impede a entrada no nível (input é
injectado; o wid=0 é limpo; e mesmo assim fica no menu/logos). É a parede [G] no sentido lato — a máquina de estados do
front-end do jogo — ainda por caracterizar ao nível do estado que decide menu vs nível.

**Estado:** [D]/[E] não alcançado. Ganhos sólidos e committados: crash fix (E324), spu6 liftado+registado (E330, peça
válida mesmo com a hipótese de gate corrigida), cadeia medida. Retracção honesta: o spu6 não é o gate provado de [D].


## §4cm REFUTA "phased/needs-level": o loader QUER os 20MB (rem=0x133C280 em state 2) e estagna aos 256KB (E333)

Trace `WADLD-SM` (gated PS3_TRACE_TYMAP) do state-machine do loader (`func_002BA76C`, ts=0x00869570):
- `#1 state=1 rem=0x0`
- `#2 state=2 rem=0x133C280` — **rem = 0x133C280 = 20169344 = os 20MB do R_PermA**.

Ou seja o loader entra em **state 2 com rem=20MB** — está a tentar carregar o R_PermA COMPLETO, NÃO um preload de 256KB.
Processa 256KB (5 shells) e estagna com 20MB por ler. O `func_002BA76C` só é chamado 2x (state 1→2) e depois pára de ser
tickado. O `F2B-STREAM-EOF-DONE=1` era do **R_LglScA** (3072B, file_pos>=sz correcto), NÃO do R_PermA (o EOF-try só marca
com `file_pos>=sz`, e o R_PermA está a 256KB/20MB).

**RETRACTA §4cf/§4cg/§4cl na parte "o loader só precisa de 256KB / [D] precisa de load de nível separado":** o loader
QUER os 20MB (rem=20MB em state 2). Logo **[D] É alcançável neste boot** — não é uma fase de preload parcial; é o load
completo a estagnar aos 256KB. SOBREVIVE: `F2B-STREAM-EMPTY=0` (o reader do ring não famintou) — mas isso agora significa
que o loader **não é re-tickado / não submete o próximo refill de chunk**, não que "só precisa de 256KB".

**O ponto de estagnação (a caracterizar):** o state-machine tem uma **WAD-wait** (`func_002BA808/BA824`, o comentário do
`f2b_stream_eof_try_complete` chama-lhe "WAD-wait ... may complete via timeout path"). Hipótese: o loader processa o 1.º
ring (256KB), submete/espera a async-read do próximo chunk (WAD-wait), e essa não completa → o state-machine não avança e
o game pára de o tickar. `rem` fica 20MB. Próximo: instrumentar `func_002BA808` (entra? sai? via timeout?) e ver porque o
próximo chunk não é lido — é a mesma família do §22 spin do E110 (parede [G] loader-side), mas agora com a certeza medida
de que o loader QUER os 20MB.

**LIGAÇÃO AO FIX PRÓPRIO DA ARENA (a peça acionável para [D]):** o loader estagna aos 256KB = **aos 5 shells**; o 6.º
shell é onde para. Com `PS3_ARENA_ROOT2BP` o 6.º **não crasha** mas também **não constrói** (o redirect Root→BP evita o
deref do handle tagged, mas o objeto continua alocado da arena errada por causa do índice de scope 127 vs 126 de §4cb).
Logo o loader fica em state 2 (rem=20MB) à espera que o 6.º construa, e não avança. **O fix próprio (§4cb/§4cc: pôr o
índice de scope de arena em 126/BP como a consola, tirando o push extra de `func_002BB884`) é o que faz o 6.º+ construir**
→ o loader consome mais records → mais chunks → os 20MB → 27 shells + 13 arenas → [D]. O ROOT2BP é só diagnóstico (tira o
crash, não constrói). **É a via concreta e medida para [D], no NOSSO código (scope de arena), sem depender de front-end
nem de áudio.**

**Estado:** [D]/[E] não alcançado, MAS a direcção corrigiu-se: NÃO é preciso "entrar num nível" separado — o loader já
está a tentar o load completo (rem=20MB) e estagna aos 256KB no refill/WAD-wait do próximo chunk. Isso é acionável no
mecanismo F2B/FIOS async do nosso runtime (nosso código), não numa transição de front-end inatingível. Retracta a
sub-conclusão de que [D] precisava do menu→gameplay.


## §4cn LOCALIZAÇÃO FINAL DO STALL (E334): a máquina de estados EXTERNA de load (func_00041Dxx) pára de tickar o loader interno = §22 spin (E110)

> **CORREÇÃO (E335):** o "= §22 spin (E110) / func_00041D5C" é INFERÊNCIA NÃO CONFIRMADA. Medido depois: com
> `PS3_TRACE_REG=1` os probes `[LDRSTATE]` de `func_00041D5C` **NÃO disparam** — logo `func_00041D5C` pode não ser o
> driver nestas corridas (ou os probes têm gate/linking a rever). CAI a atribuição ao §22 spin de `func_00041D5C`.
> SOBREVIVE (medido): o loader INTERNO `func_002BA76C` corre 2x (state 1→2, `rem=0x133C280`=20MB) e estagna; `func_002BA76C`
> tem 3 callers (0x3F2C0=func_0003F2B0, 0x41FE8, 0x2BAE18) e NÃO está confirmado qual o tickou nem porque parou. O degrau
> seguinte é instrumentar os 3 callers de `func_002BA76C` para ver qual driva e porque pára ao 2.º tick.

Cadeia de driving medida: a máquina EXTERNA de load (`func_00041Dxx`/`func_00042xxx`, 7+ call sites a `func_0003F2B0`)
chama `func_0003F2B0` → `func_002BA76C` (o loader interno do R_PermA). O `WADLD-SM` (cap 80) mostra `func_002BA76C`
chamado **só 2x** (state 1→2, rem=20MB), e depois a EXTERNA **pára de o tickar**. Os call sites da externa (0x4265C,
0x428DC, 0x42C00, 0x43160, 0x43360, 0x435A0, 0x43A00) estão em `func_00041D5C`/`func_00042xxx` — a mesma família do
**§22 spin do E110** (a máquina externa fica em ~state 22/25 e não avança). O console tickaria o loader interno muitas
vezes (20MB / 256KB por chunk ≈ 80 chunks); nós paramos ao 1.º.

**Síntese medida da via para [D] (corrigida ao longo de E333–E334):**
- O loader interno QUER os 20MB (rem=0x133C280, state 2) — [D] é alcançável neste boot (§4cm; refuta needs-level).
- Constrói 5 shells (chunk 1) e a máquina EXTERNA pára de o tickar (§22 spin, E110) — não avança para o chunk 2.
- Logo o registry fica com 5 entradas não resolvidas, 0 arenas WAD_R_*.

**O gate REAL de [D] é o §22 spin da máquina externa de load** (`func_00041Dxx`) — porque é que ela não continua a
tickar `func_0003F2B0`/`func_002BA76C` depois do chunk 1. É parede [G] loader-side (documentada E110), no NOSSO código
(a máquina de estados externa), reachable neste boot. NÃO é: front-end/menu (refutado §4cm), relógio de áudio/spu6
(refutado §4cfor E332), stream-depth como bug de refill (o loader nem starva, EMPTY=0). É o §22 spin: a externa fica
presa antes de pedir o chunk 2.

**Próximo (o degrau, estreito):** instrumentar `func_00041D5C`/a máquina externa em ~state 22/25 — porque é que ela não
re-chama `func_0003F2B0` depois do chunk 1 (que flag/condição a prende). Cruzar com o oráculo (o console avança). É o
mesmo §22 spin do E110/próximo-passo-0b, agora com a certeza medida de que é ELE que gateia [D] (e não o áudio nem o
front-end).

**Estado:** [D]/[E] não alcançado. Ganho desta sessão: o blocker de [D] está localizado ao §22 spin da máquina externa
de load (medido, no nosso código), com 3 paredes falsas refutadas (needs-level, áudio, refill-bug). Crash fixo (E324),
spu6 liftado (E330).


## §4co O DRIVER MEDIDO (E336): func_00041D5C loc_00041FE4 corre o setup (states 1→2) e o state-machine LDRSTATE não corre

Medido (WADLD-SM com `lr`): as 2 chamadas a `func_002BA76C` vêm de **`lr=0x00041FE8`** = a call DIRECTA em `loc_00041FE4`
dentro de `func_00041D5C` (NÃO via `func_0003F2B0`/states LDRSTATE). O loop aí:
```
loc_00041FD4: func_002A94C8(); func_000B3850();
loc_00041FE4: func_002BA76C();  if (r3 != 0) goto loc_00041FD4;   // continua enquanto r3!=0
              // r3 == 0 -> sai: func_000415F0() ...
```
Sequência medida: `#1 state=1` → r3!=0 → loop (func_002A94C8/000B3850) → `#2 state=2 rem=0x133C280` → **r3==0 → SAI**.
Ou seja o loader interno em state 2 **devolve 0 ("feito desta passagem")** e o loop de setup sai. Em `func_002BA76C`,
`loc_002BA880` escreve um novo state (ts+0x1CC=r0) e faz `r3=0; return` — o state avança e devolve 0.

**Medido também:** os probes `[LDRSTATE]` (states 0-26, o state-machine de BULK-load dentro de `func_00041D5C`) **NÃO
disparam** com `PS3_TRACE_REG`. INFERÊNCIA (a confirmar): o `loc_00041FE4` corre só o SETUP (states 1→2 do loader
interno, os 5 shells do 1.º chunk); o BULK-load (que consumiria os 20MB, rem=0x133C280) seria o state-machine LDRSTATE,
que não corre depois do setup. Logo o registry fica com os 5 do setup e o bulk (27 shells + 13 arenas) não acontece.

**SOBREVIVE (medido):** driver = `func_00041D5C` loc_00041FE4; `func_002BA76C` 2x (state 1→2, state 2 devolve 0 e avança
state via loc_002BA880); LDRSTATE não dispara; `f2b_stream_eof_try_complete` NÃO marca EOF p/ o R_PermA (file_pos<sz).
**INFERÊNCIA (não confirmada):** "setup vs bulk-load" e que o LDRSTATE é o bulk. **Próximo:** ver o novo state que
loc_002BA880 escreve (r0) e porque o func_00041D5C não entra no state-machine LDRSTATE depois do setup — instrumentar o
fluxo de func_00041D5C após `loc_00041FE4` (func_000415F0/func_00041014/func_002B273C…) e ver se transita para o bulk-load
ou sai da função. Cruzar com o oráculo (o console faz o bulk).

**loc_002BA880 = state 0 (medido):** > **RETRACTADO E337:** o probe `PS3_TRACE_DONE880` em loc_002BA880 NÃO disparou — o loader NÃO passa por loc_002BA880. A conclusão em state 2 vem de um branch mais cedo (ver §4cp). O texto abaixo fica como registo do erro.

 `loc_002BA880` faz `func_002B3F78(ts+0x64)`, depois `vm_write32(ts+0x1CC, 0)` e
`r3=0; return`. Ou seja em state 2 o loader **põe-se em state 0 (idle/"feito")** e devolve 0 — com `rem=20MB`. É uma
**conclusão PREMATURA** após o 1.º chunk: o loader marca-se done, o loop do driver sai, e o bulk (20MB) nunca corre. O
trigger está na lógica de flags de `loc_002BA808/854` (ts+0x1C8, ts+0x1C4, ts+0x1CC) — a WAD-wait completa e vai a
loc_002BA880. INFERÊNCIA a confirmar: se state=0 aqui é "load todo feito" (bug, rem=20MB) ou "sub-recurso feito" (e o
driver devia re-conduzir para o próximo, mas não o faz). Próximo: instrumentar as flags de loc_002BA808/854 que routeiam
para loc_002BA880 vs continuar; e ver, no oráculo, o que o console faz em state 2 (fica ou avança para bulk).

**Estado:** [D]/[E] não alcançado. Progresso medido: o driver e o ponto de saída do setup estão identificados
(`func_00041D5C` loc_00041FE4, state 2 devolve 0 e escreve state=0 em loc_002BA880, prematuro com rem=20MB); falta o
trigger exacto das flags e ligar ao bulk-load. Crash fixo (E324), spu6 liftado (E330),
11 retracções/correcções honestas ao longo de E313–E336.


## §4cp O BRANCH REAL DE CONCLUSÃO (E337): state 2 com ts+0x1BC==0 vai a func_002BA94C→func_002BA960 (processa 1 record, devolve done)

Medido (WADLD-SM com flags): em `#2 state=2 rem=0x133C280(20MB) f1C8=0 f1BC=0 f1C4=0 body1D4=0`. As flags routeiam:
`ts+0x1C8==0` → NÃO vai a func_002BA914; `ts+0x1BC==0` → **vai a `func_002BA94C`**. (O probe DONE880 confirmou que
loc_002BA880 NÃO é o caminho — retractado.) `func_002BA94C`: lê `r11=*(WADmgr+0x58)` (o stream obj corrente); se !=0 →
`func_002BA960` que chama o **método vtable+0x48** do stream obj (o processador de record) e guarda o status; ambos vão a
`func_002BA7F0`. Ou seja em state 2 o loader chama o processador de record; o resultado leva à conclusão (r3=0, o loop do
driver sai).

**Interpretação (HONESTAMENTE em aberto):** com `body1D4=0` o loader está numa fronteira de record; chama o processador,
que devolve "sem mais records nesta lista". Mas `rem=20MB`. Duas leituras, não distinguidas:
(a) **phased dentro do R_PermA:** a lista de records do HEADER do contentor acaba (os 5 shells referenciados), e os dados
   dos recursos (ShellA data + os outros 149) carregam noutra fase/on-demand (talvez precise mesmo de entrar no nível) —
   nesse caso a consola tem 27 porque está IN-LEVEL a usar os recursos;
(b) **conclusão prematura:** o processador devia continuar a ler a lista (há 20MB), mas o `vtable+0x48` devolve done cedo
   por framing errado do nosso F2B / dos dados entregues.
Não tenho medição que distinga (a) de (b) — e as minhas tentativas anteriores de afirmar uma direcção (needs-level;
áudio; §22 spin; loc_002BA880) foram todas refutadas por medição.

**Honestidade metodológica:** foram **12 retracções** em E313–E337, várias de hipóteses minhas nesta sessão. O loader é
um state-machine profundo (func_002BA76C→…→func_002BA7F0→…) e segui-lo função-a-função não convergiu. O que está SÓLIDO:
o loader interno quer 20MB (rem=0x133C280), processa a lista de records do header (5 shells) em state 2 via
func_002BA94C→func_002BA960 (vtable+0x48), e conclui (r3=0). O que FALTA: distinguir phased vs premature, o que exige o
ORÁCULO — ver, na consola, se em state 2 (mesmo ts, mesma lista) o processador continua ou conclui, e o que carrega os
recursos internos. Sem isso, qualquer fix é adivinhação.

**Estado:** [D]/[E] não alcançado. O blocker está reduzido a UMA pergunta binária e medível **no oráculo** (phased vs
premature em state 2), não a mais tracing cego do nosso lado. Crash fixo (E324), spu6 liftado (E330).


## §4cq ORÁCULO DECIDE: PREMATURE, não phased

> **CORREÇÃO PARCIAL E339:** a parte "nós ficamos presos em state 2, nunca alcançamos state 3" está ERRADA. Medido com
> probes ST23: o nosso loader ALCANÇA `func_002BA990` (resource-start) e `func_002BA9BC` (o setter de state=3) — este
> disparou ≥10x. Logo ENTRAMOS em state 3 e processamos ≥10 records; o `vtable+0x48` devolveu 0x869570 (ptr válido, não 0).
> SOBREVIVE (medido, sólido): a CONSOLA consome os 20MB (state 3, rem 0xAFC280→0x65C280) e nós PARAMOS cedo (5 shells,
> rem fica 20MB). O que MUDA: não é "stuck em state 2 / vtable devolve done" (errado) — é "processamos ~N records em
> state 3 e paramos", N por medir. É a 13.ª retracção da sessão; a análise função-a-função do meu lado NÃO é fiável neste
> loader — só o oráculo dá factos sólidos. — a consola está em state 3 (bulk, rem a descer); nós concluímos em state 2 (E338)

Medição decisiva no oráculo (bp `func_002BA76C` 0x2BA76C, ler ts+0x1CC/0x1AC/0x1D4):
- **Consola:** `func_002BA76C` chamado MUITAS vezes, quase todas em **state=3**, com **rem a DESCER** monotonicamente
  (0xAFC280 → 0xADC280 → … → 0x0065C280 na janela) e `body1D4` != 0 a variar (0x1E720, 0x94B0, 0x22180, …). É o
  **bulk-load a consumir os 20MB, record-body a record-body**. state=2 apareceu 1x (transitório).
- **Nós:** o loader fica em **state=2** com `body1D4=0` (sem corpo de record) e **conclui** (r3=0) — o driver sai.

**RESOLVE a pergunta binária de §4cp: é PREMATURE (bug NOSSO), NÃO phased.** [D] é alcançável neste boot corrigindo o
loader para **avançar state 2 → state 3** (o bulk-load). A transição para state 3 é `func_002BA9BC` (linha 46358:
`state=3`). Em state 2 o nosso loader, com `ts+0x1C8=0`, `ts+0x1BC=0`, `body1D4=0`, routeia para `func_002BA94C →
func_002BA960` (chama o processador de record vtable+0x48) e conclui em vez de continuar para o bulk. **Sinal-chave:**
`body1D4=0` (ts+0x1D4) em nós vs != 0 na consola — o nosso loader não tem CORPO de record para ler, logo "acaba"; a
consola tem os corpos (os dados dos recursos) e consome-os em state 3.

**Direcção do fix (agora certa e medida):** porque é que os nossos records em state 2 têm `body1D4=0` (sem corpo) quando
a consola os tem != 0? É o parsing do header de record / o framing do R_PermA que entregamos (F2B) — o header devia dar
um body size != 0 (o corpo do recurso), e dá 0. Isso trava o avanço para state 3. **Próximo:** instrumentar onde
`ts+0x1D4` (body1D4) é escrito a partir do header de record em state 2, e comparar o header lido (nós vs consola) no
início do R_PermA — o campo que devia ser o body size. Refuta em definitivo: needs-level (§4cm), áudio (E332), §22
spin/loc_002BA880 (retractados). É um bug de framing/parsing do loader, no NOSSO lado, com [D] alcançável.

**PRECISÃO (E338b):** `body1D4` (ts+0x1D4) é escrito DENTRO de `func_002BA9BC` (o setter de state=3, linha 46176), logo
`body1D4=0` é SINTOMA (nunca entramos em state 3), não a causa. A CAUSA está a montante: em state 2 o nosso fluxo toma o
caminho `ts+0x1BC==0 → func_002BA94C → func_002BA960 (vtable+0x48) → …` e CONCLUI, em vez de continuar até
`func_002BA9BC` (state 3). O ponto exacto de divergência está algures em `func_002BA94C → func_002BA960 → …` (o método
vtable+0x48 do stream obj devolve done onde a consola devolveria more). Seguir função-a-função não converge; o próximo
passo produtivo é **comparar a consola vs nós no MESMO ponto de state 2** (bp em func_002BA76C, forçar/apanhar state==2
na consola cedo no R_PermA) e ver o retorno do vtable+0x48 — não mais tracing cego do nosso lado.

**Estado:** [D]/[E] não alcançado, MAS o oráculo PROVOU que é premature (bug do loader), não phased — o blocker está
reduzido a "body1D4=0 em state 2" (record sem corpo) que trava o bulk-load. Crash fixo (E324), spu6 liftado (E330),
12 retracções honestas (a última, loc_002BA880, corrigida pelo oráculo).


## §4cr LOCALIZAÇÃO SÓLIDA (E340): o loader espera em state 3 porque os CORPOS de record não chegam ao ring (func_002B9F00 = AREAD-HLE)

> **REFUTADO E343 (oráculo):** `body1D4=0` NÃO é a causa — na consola, no MESMO ponto (R_PermA start, rem~18.9MB) `body1D4=0` TAMBÉM. Ver §4cu.

Medido (probe ST23 limpo em func_002BA9BC): o nosso loader ENTRA em state 3 e chama `func_002BA9BC` **50+ vezes**, mas
`rem(ts+0x1AC)=0x133C280` (20MB) e `ts+0x74=0x133C280` FICAM CONSTANTES, e `body(r29)=0` sempre. Lida a lógica de
`func_002BA9BC`: chama `f2b_stream_ensure` (enche só 0x20 = 1 header, porque body1D4=0 -> need=0x20), depois
`func_002B9F00` (o reader AREAD-HLE, consome o header), depois `avail=*(stream+0x10)`; se `avail==0` -> `r29=0` ->
`func_002BA824` (WAD-wait). **Medido: avail==0 -> r29=0.** Ou seja o loader lê o HEADER mas o **CORPO do record não está
no ring**; espera pelo corpo, que nunca chega; loop com rem preso.

**Contraste oráculo (sólido):** na consola `func_002BA9BC` dispara **500+ vezes**, `rem` DESCE 0xAFC280→0x38280 (consome
os 20MB), body1D4 != 0 por record. Os corpos CHEGAM.

**RAIZ (medida, no nosso código):** `func_002B9F00` (AREAD-HLE / "chunks FUN_002b9f00→AREAD-HLE" do CLAUDE.md) NÃO entrega
os corpos dos records ao ring — só headers (ou 0-size). O `f2b_stream_ensure` também só enche 0x20 (porque body1D4=0, um
ciclo: sem corpo -> body1D4 fica 0 -> ensure enche só header -> sem corpo). É a mecânica F2B/AREAD-HLE de entrega de
CORPO de record, do nosso lado.

**Próximo (o fix, no nosso código):** ver porque `func_002B9F00`/o F2B não põem o CORPO do record no ring (só o header).
Provavelmente: (a) o `f2b_stream_ensure` devia encher `body_size` (não 0x20) mas `body1D4=0` cria o ciclo; ou (b)
`func_002B9F00` (AREAD-HLE) devia ler o próximo chunk de corpo do R_PermA e não o faz. Comparar com a consola: o que
`func_002B9F00` devolve e o `avail` depois. É oracle-comparável (não análise cega).

**SÓLIDO (sobrevive):** o loader entra em state 3 (§4cq-corrigido), o corpo dos records não chega ao ring (avail==0 pós
func_002B9F00), rem preso a 20MB; a consola consome os 20MB. É um bug de entrega de CORPO no F2B/AREAD-HLE. Refuta
needs-level/áudio/§22-spin/state-2-stall (todos retractados). Crash fixo (E324), spu6 liftado (E330). 13 retracções.


## §4cs FIX FALHADO (E341): encher mais no f2b_stream_ensure NÃO destrava — avail==0 pós func_002B9F00 é mais fundo

Testei o fix óbvio de §4cr: `PS3_F2B_ENSURE_BODY` faz `f2b_stream_ensure` encher um chunk grande (cap do ring, não 0x20)
quando body1D4==0. **Medido: NÃO funciona** — `rem` fica preso a 0x133C280, `body(r29)=0`, `file_pos` fica 256KB. Logo o
problema NÃO é a quantidade que o ensure enche; é que **`avail=*(stream+0x10)` fica 0 DEPOIS de `func_002B9F00`** — ou
`func_002B9F00` (AREAD-HLE) devolve/deixa 0 bytes, ou o ring que a `func_002BA9BC` lê (`*(ts+0x1A8)+0x10`) não é o que o
F2B enche. É a 14.ª retracção/tentativa falhada da sessão.

**Estado honesto:** ao fim de ~55 rondas e 14 retracções, as minhas hipóteses de fix estão a falhar por adivinhação — o
loader/F2B é demasiado entrelaçado para o resolver por tentativa. O que está SÓLIDO e medido: a consola consome os 20MB
(func_002BA9BC 500+x, rem desce); nós entramos em state 3 mas `avail==0` pós `func_002B9F00` -> `r29=0` -> WAD-wait, rem
preso; o corpo dos records não chega ao ring que a func_002BA9BC lê. **O próximo passo TEM de ser oracle-driven e
cuidadoso:** (1) confirmar QUAL stream/ring a func_002BA9BC lê (`*(ts+0x1A8)`) e se é o mesmo que o F2B enche
(`g_f2b_fill_stream`); (2) ver na consola o que `func_002B9F00` faz ao ring (enche? consome?) e o `avail` antes/depois;
(3) só então um fix medido. Sem isso, continuo a falhar.

**Ganhos sólidos da sessão (não retractados):** crash 0x4BFFFE34 eliminado + raiz provada (E324); spu6 (SCREAM PM)
liftado+registado (E330); E316-REARM; e a PROVA por oráculo de que [D] é um bug de loader alcançável (consola consome
20MB; nós não), com needs-level/áudio/§22-spin/state-2-stall todos refutados por medição. O blocker está medido
(corpo de record não chega ao ring da func_002BA9BC), falta o fix correcto — oracle-driven.


## §4ct ESTADO MEDIDO + 2 FIXES FALHADOS + HONESTIDADE (E342)

Medido (probe ring-identity em func_002BA9BC): o ring que a func_002BA9BC lê (`*(ts+0x1A8)=0x43857340`) É o mesmo que o
F2B enche (`g_f2b_fill_stream=0x43857340`) — ring correcto. `avail(+10)=0x40000` (256KB) e **DESCE devagar**
(0x40000→0x3F680 em 50 calls ≈ 48 bytes/call), `body(r29)=0`, `rem=0x133C280` constante, `F2Bfp=0`. CORRIGE §4cr/§4cs
(que diziam "avail==0"): o avail é 256KB a descer, não 0.

Leitura: o loader CONSOME o ring (avail desce) mas só ~48 bytes/call e ~1 call/s (LENTO), com `body=0` — está a mastigar
a **TOC do contentor** (records de header, body=0) que não fazem `rem` descer. Nunca chega aos records de RECURSO
(body!=0, que a consola consome em state 3). O F2B `file_pos=0` (do E316-REARM) — quando o avail esgotar, re-lê a TOC.

**2 fixes FALHADOS (gated OFF, inócuos):** `PS3_F2B_ENSURE_BODY` (encher body em vez de 0x20) e `PS3_F2B_PUMP_ADVANCE`
(file_pos=total pós-pump) — nenhum mexeu (`F2Bfp` fica 0, `rem` preso). O file_pos=0 vem do REARM, não do pump; e a
lentidão (48 bytes/call, 1 call/s) sugere que o gargalo é o PACE do driver (WAD-wait entre records), não só a entrega.

**HONESTIDADE (fim de ~59 rondas):** fiz **~16 retracções** e **2 fixes falhados** nesta sessão. As minhas hipóteses de
loader/F2B estão a falhar por adivinhação; a análise acumulada NÃO é fiável. O que está SÓLIDO e medido: [D] é um bug de
loader alcançável (consola consome 20MB; nós mastigamos a TOC devagar, body=0, rem preso), com needs-level/áudio/§22-spin
todos refutados. O que FALTA é o fix correcto — e a lição operacional desta sessão é que ele precisa de uma abordagem
ORACLE-FIRST fresca (medir na consola o PACE e a transição TOC→recurso em state 3, e o que difere no nosso record header),
NÃO mais tentativa-e-erro do meu lado.

**Ganhos sólidos intactos:** crash 0x4BFFFE34 eliminado + raiz provada (E324); spu6 (SCREAM PM) liftado+registado (E330);
E316-REARM; a prova por oráculo de que [D] é bug de loader alcançável. Blocker medido (TOC mastigada devagar, body=0, rem
preso, F2B file_pos=0), fix por achar.


## §4cu ORÁCULO LIMPO: body1D4=0 é NORMAL; a diferença é rem a descer + refill (E343)

Medição limpa no oráculo (bp func_002BA9BC, filtrar rem>16MB = R_PermA start): na consola, ring=0x3007FCD0,
**rem DESCE** (0x121C280→0x1148280), **avail OSCILA** (0x15000↔0x3E7D0 — REFILL enquanto consome), e **body1D4=0** em
TODOS. Ou seja `body1D4=0` é NORMAL (refuta §4cr/§4cq: não é "corpo em falta"/framing errado). A diferença REAL:
| | rem (ts+0x1AC) | avail (ring+0x10) | refill |
|---|---|---|---|
| consola | DESCE (lê o ficheiro p/ a frente) | oscila 0x15k↔0x3E7k | SIM (avail sobe e desce) |
| **nós** | **preso a 0x133C280** | só DESCE (drena) | NÃO |

**RAIZ (medida, limpa):** o nosso F2B **não avança a leitura do ficheiro** — `rem`/`ts+0x74` nunca decrementam e o ring
nunca refila para a frente além dos 256KB iniciais (do pump). Na consola a leitura natural avança `rem` (ts+0x74) e
refila o ring com os chunks seguintes; em nós o F2B enche 256KB uma vez (pump, file_pos=0) e não avança. É a mesma família
do E285/E316 (profundidade do stream), agora com a prova LIMPA de que o gargalo é o **avanço da leitura (rem/ts+0x74 +
refill)**, NÃO o body dos records (body=0 é igual nos dois).

**Fixes que FALHARAM (gated OFF):** ensure-body (§4cs), pump-advance (§4ct) — ambos porque o file_pos=0 vem do E316-REARM
e/ou o rem/ts+0x74 do guest não é tocado pelo F2B. O fix correcto tem de **decrementar `ts+0x74`/`rem` do guest à medida
que o F2B entrega** e **avançar file_pos** (refill forward), coordenado com o REARM. Isto exige perceber quem decrementa
ts+0x74 na consola (func_002B9F00? a completação da async read?) — oracle-driven, com cuidado.

**Estado honesto:** [D]/[E] não alcançado; ~17 correcções na sessão. SÓLIDO e medido: [D] é bug de loader alcançável; a
raiz é o avanço da leitura (rem/ts+0x74 + refill), body=0 é normal (refutado como causa). Crash fixo (E324), spu6 liftado
(E330), E316-REARM. O fix (rem/ts+0x74 advance no F2B) fica por fazer — precisa de saber quem decrementa ts+0x74 na
consola.


## §4cv CAMINHO NATURAL CONFIRMADO QUEBRADO (E344): sem F2B, rem=0/avail=0 (0 bytes); o F2B é necessário mas incompleto

Testei o caminho NATURAL (sem `PS3_WAD_HOST_READ`/F2B, só crash-fix ROOT2BP): `WADLD-SM state=1 rem=0`, `BA9BC rem=0
avail=0 F2Bfill=0` — o R_PermA natural entrega **0 bytes** (o bloqueio documentado E245: open sucede, leitura=0, via
inflate/dearchiver). Logo o F2B É NECESSÁRIO (o natural não alimenta o loader), mas o nosso F2B é INCOMPLETO (não avança
a leitura forward, §4cu). Ambos os caminhos de alimentar o loader estão bloqueados:
- **Natural:** precisa de corrigir o inflate/dearchiver (E138, o "0 bytes") — a meta real ("mesmo fluxo do RPCS3"), mas fundo.
- **F2B:** precisa de refill forward + decremento de ts+0x74/rem + wake do WAD-wait — 3 fixes meus falharam.

**Sinal adicional medido (E344):** o loader avança ~1 record/s e func_002BA76C só é chamado 2x em 45s — há uma ESPERA
longa (~22s) entre batches, provável WAD-wait a fazer timeout (o F2B não sinaliza a completação da async read; família do
wakeup-race E203–E244). Isto sugere que, além do refill forward, o F2B precisa de **acordar o WAD-wait** a cada entrega.

**CONCLUSÃO HONESTA DA SESSÃO (E313–E344):** [D]/[E] NÃO alcançado. Removi o crash (E324), liftei o spu6 (E330), provei
por oráculo que [D] é bug de loader alcançável e refutei ~5 direções falsas (várias minhas), e cravei a raiz (avanço da
leitura do R_PermA). MAS: 3 fixes falharam, ~18 correcções, e a minha abordagem análise-e-fix NÃO converge neste
loader/F2B. Os DOIS caminhos (natural inflate; F2B forward+wake) estão medidos e escritos. O próximo trabalho tem de ser
oracle-first e cirúrgico — não mais hipótese-e-teste do meu lado, que aqui falhou repetidamente. Ganhos sólidos intactos.


## §4cw PORQUE OS FIXES FALHARAM: o F2B é um acumulado de hacks (pump+rearm+ALIGN hardcoded), não um stream forward (E345)

Achado concreto que explica os 3 fixes falhados: `g_f2b_fill_file_pos` é escrito em **7 sítios**, incluindo DENTRO de
`func_002BA9BC` (o loop quente de state 3) — o código **E131 "F2B-STREAM-ALIGN"** de uma sessão anterior:
- linha 46252: `g_f2b_fill_file_pos = _to` onde **`_to = 20038352` (0x131C290 ≈ 19.1MB, HARDCODED = "MDL_PUMeterDrain1_0")**;
- linha 46266/46328: `g_f2b_fill_file_pos = g_f2b_fill_sz` (= EOF, 20MB);
- linha 46304: `= _o`.

Ou seja o nosso F2B **não é um stream forward** — é um acumulado de HACKS forçados (pump 256KB + rearm file_pos=0 +
ALIGN que crava offsets específicos de recursos do R_PermA como 20038352). É a linhagem da parede [F] ("o preenchimento
host do ring do WAD — F2B fill/pump/ALIGN — rebobinava o R_PermA", CLAUDE.md). Os meus 3 fixes (ensure-body, pump-advance,
F2B-forward) FALHARAM porque lutavam contra estes multi-resetters/hacks, dominados pelo ALIGN no loop quente.

**CONCLUSÃO REAL (a raiz de porque [D] não fecha facilmente):** o F2B é frágil por construção — hacks de offset
hardcoded, não streaming. Para [D] há DUAS vias limpas, ambas fundas:
1. **Reescrever o F2B como um stream forward de verdade** (ler o R_PermA sequencialmente, rem/ts+0x74 a decrementar,
   ring a refilar, wake do WAD-wait) — SUBSTITUINDO os hacks pump/rearm/ALIGN, não remendando-os. É o que a consola faz.
2. **OU corrigir o caminho NATURAL** (o inflate/dearchiver que entrega 0 bytes, E138/E245) — a meta "mesmo fluxo do
   RPCS3", que dispensa o F2B inteiro.

Remendar o F2B ponto-a-ponto (o que tentei) NÃO converge — é preciso substituir o mecanismo. Esta é a lição concreta e
o handoff: NÃO mais fixes pontuais no F2B; ou reescrita forward-stream limpa, ou o inflate natural.

**Estado da sessão (E313–E345):** [D]/[E] não alcançado. Ganhos sólidos: crash fixo (E324), spu6 liftado (E330),
E316-REARM, prova por oráculo de que [D] é bug de loader alcançável, ~5 direções falsas refutadas, e AGORA a raiz do
porquê-não-fecha: o F2B é hacks de offset, não stream. 3 fixes falharam por isso mesmo. As duas vias limpas estão escritas.


## §4cx REDIRECIONAMENTO

> **RETRACTADO E349:** as "mudanças invariantes" eram um ARTEFACTO DE ERRO DE COMPILAÇÃO. O fix pump-advance (E341) meteu `total` fora de escopo (ppu_recomp_001.cpp:38613, 'use of undeclared identifier total'), e o `build_macos.sh` **linkou silenciosamente o .o ANTIGO** — logo TODOS os fixes desde E341 (forward, wake, remdec) NUNCA compilaram. Corrigido o erro, os fixes CORREM e FUNCIONAM (ver §4da).: os 4 fixes do F2B são INVARIANTES no resultado -> o return-0 está na LÓGICA do loader/dados, não no fill (E346)

4.º fix falhado (F2B-WAKE, set ts+0x1B0=1 para acordar a WAD-wait): resultado **IDÊNTICO** aos outros 3 — 50 records,
`rem=0x133C280` preso, avail 0x40000→0x3F680, F2Bfp=0. **Observação-chave:** os 4 fixes que fiz ao FILL do F2B (ensure-body,
pump-advance, F2B-forward, F2B-wake) **NÃO mudam NADA no resultado**. O loader processa ~50 records e `func_002BA76C`
devolve 0 **com 253KB AINDA no ring** (avail=0x3F680) — logo NÃO é falta de dados no ring, nem o fill, nem o wake.

**REDIRECIONAMENTO (a lição das 4 falhas):** o return-0 é determinado pela **LÓGICA do loader (func_002BA76C) / pelos
DADOS dos records**, NÃO pelo mecanismo de fill do F2B que andei a remendar. Estive a corrigir a camada errada. O loader
devolve 0 (correctamente, pela sua lógica de guest) dado o estado/dados que tem — e com 253KB no ring, a causa é os
RECORDS que ele lê (framing/conteúdo) ou o estado, não a entrega.

**Consequência para as vias de [D]:** a via "reescrever o fill do F2B" (§4cw) é provavelmente INSUFICIENTE — os fixes ao
fill são invariantes. As vias reais são: (1) **os DADOS/framing dos records que entregamos** (o loader lê o que está no
ring e conclui cedo — comparar byte-a-byte os records nós vs consola no MESMO offset do R_PermA); (2) o **inflate NATURAL**
(que entregaria os bytes certos). NÃO é o fill/pace/wake do F2B (4x medido invariante).

**Estado honesto (E313–E346):** [D]/[E] não alcançado; **4 fixes falhados, ~18 correcções**. A minha abordagem de remendar
o F2B fill é comprovadamente a camada ERRADA (invariante). Ganhos sólidos: crash fixo (E324), spu6 (E330), oráculo prova
[D] alcançável. O próximo trabalho: comparar os RECORDS entregues (nós vs consola) byte-a-byte, ou o inflate natural —
NÃO mais fixes ao fill do F2B.


## §4cy DADOS CORRECTOS: descarta o framing; é rem/file_pos preso por resetters emaranhados (E347)

Medido: o loader despacha **type=21 primeiro** (o header do contentor R_PermA = `15 00 00 00` = `r_perma.wad_ps3[0]`),
logo o ring TEM os dados SEQUENCIAIS desde o offset 0 (correcto). O ALIGN hardcoded (offset 20038352 = "MDL_PUMeterDrain1_0",
confirmado no ficheiro) NÃO disparou neste run (F2Bfp=0). **Descarta a via "record data/framing errado" (§4cx-1):** os
dados são CORRECTOS.

**Narrowing final (E313–E347), tudo medido:**
- Os dados no ring são correctos (sequencial de 0). NÃO é framing.
- Os 4 fixes ao FILL são invariantes. NÃO é o fill/pace/wake.
- `file_pos` é reposto a 0 por um resetter emaranhado (rearm/pump/E131 — a FILL chegou a pôr 262144, algo repõe 0).
- `rem` fica preso a 0x133C280 (não decrementa).
- O loader processa ~50 records correctos e `func_002BA76C` devolve 0 (done) com 253KB no ring e rem=20MB.

**Conclusão (o que sobra, medido):** a construção não popula o registry porque o loader **conclui a fase após ~50 records
com rem preso** — e a única diferença medida vs a consola é **rem a decrementar (consola) vs preso (nós)**, governada pelo
avanço de `file_pos`, que os resetters emaranhados do F2B (rearm/pump/ALIGN) mantêm a 0 apesar dos meus fixes. É a linhagem
[F] "F2B rebobinava o R_PermA". Fechar [D] = **desemaranhar os resetters de `file_pos` E fazer `rem`/`ts+0x74` decrementar**
(uma reescrita coordenada), OU o inflate natural. NÃO é: framing (dados correctos), fill (invariante), needs-level, áudio.

**Estado honesto:** [D]/[E] não alcançado. 4 fixes falhados, ~19 medições/correcções. Ganhos sólidos: crash fixo (E324),
spu6 (E330), oráculo prova [D] alcançável, e o funil apertado até "rem/file_pos preso por resetters emaranhados" — com
framing, fill, needs-level e áudio todos DESCARTADOS por medição. É a caracterização mais apertada que o projeto tem para
[D]; fechá-lo é a reescrita coordenada dos resetters ou o inflate natural — para uma sessão fresca oracle-first.


## §4cz PROVA DEFINITIVA

> **RETRACTADO E349:** as "mudanças invariantes" eram um ARTEFACTO DE ERRO DE COMPILAÇÃO. O fix pump-advance (E341) meteu `total` fora de escopo (ppu_recomp_001.cpp:38613, 'use of undeclared identifier total'), e o `build_macos.sh` **linkou silenciosamente o .o ANTIGO** — logo TODOS os fixes desde E341 (forward, wake, remdec) NUNCA compilaram. Corrigido o erro, os fixes CORREM e FUNCIONAM (ver §4da).: 5 mudanças ao F2B = resultado byte-idêntico -> o return-0 é lógica de guest, intocável pelo F2B (E348)

5.º fix (F2B-REMDEC, decrementar ts+0x74/rem pela consumo) **NÃO disparou sequer** (REMDEC=0) e o resultado foi
IDÊNTICO aos outros 4. Balanço: **5 mudanças distintas ao F2B (ensure-body, pump-advance, forward, wake, remdec) dão um
resultado BYTE-IDÊNTICO** — 50 records, rem=0x133C280 preso, func_002BA76C devolve 0, 253KB no ring. E os DADOS no ring
são correctos (sequencial de 0, §4cy).

**PROVA DEFINITIVA:** o return-0 do loader é **totalmente independente de tudo o que toco no F2B** — é a **lógica da
máquina de estados do GUEST** (`func_002BA76C` recompilado) que conclui a fase, dados os dados (correctos) e o estado que
vê. Não é alcançável remendando o F2B (5x medido invariante). Para [D] via loader, o que difere vs a consola é o ESTADO
que o loader lê (medido: rem preso vs a descer), mas a causa desse estado NÃO é o F2B fill (invariante) — é onde/como o
guest decrementa a sua contagem de leitura (`ts+0x74`), fora da região do loader, que não consegui isolar/mover.

**Honestidade final absoluta (E313–E348):** [D]/[E] não alcançado. **5 fixes falhados/invariantes, ~19 correcções**, e no
fim a própria instrumentação a falhar (REMDEC=0) — sinal claro de que ultrapassei o ponto produtivo. NÃO vou gerar mais
tentativas: provei, por 5 medições invariantes, que a minha abordagem (remendar o F2B) é a camada ERRADA. Os ganhos
sólidos ficam: crash fixo (E324), spu6 liftado (E330), E316-REARM, e o funil apertado com framing/fill/needs-level/áudio
TODOS descartados por medição. Fechar [D] exige compreender a lógica de return-0 do guest e onde `ts+0x74` é decrementado
na consola — trabalho oracle-first fresco, NÃO mais remendos meus ao F2B.


## §4da CORREÇÃO CRÍTICA: os fixes nunca compilaram (erro em E341); corrigidos, FUNCIONAM parcialmente (E349)

**Descoberta:** as probes novas (mfd, REMDEC, RINGDUMP, F2B-forward) não disparavam. Compilei `ppu_recomp_001.cpp`
standalone: **erro em E341** — o fix pump-advance escreveu `g_f2b_fill_file_pos = _pa ? total : 0` com `total` FORA DE
ESCOPO (linha 38613). O `build_macos.sh` **ignorou o erro e linkou o .o ANTIGO** (sem os meus fixes). Logo os "5 fixes
invariantes" de §4cx/§4cz **NUNCA CORRERAM** — a conclusão "o F2B é a camada errada" está RETRACTADA.

**Corrigido o erro (revert da linha `total`), os fixes COMPILAM e CORREM.** Medido (combo REMDEC+FORWARD+WAKE):
- **REMDEC:** `rem` agora DESCE (0x133C280→0x133B900) — antes preso. FUNCIONA.
- **FORWARD:** `F2Bfp` agora AVANÇA (208→288→2432) — antes 0. O ring refila para a frente. FUNCIONA.
- `mfd=0x4D560002` (R_PermA, F2B activo), natural=0.

**Ainda aberto (agora com fixes a correr):** o loader ainda processa só ~50 records e pára. Como os fixes AGORA correm e o
loader ainda pára, isto verifica que o return-0 é lógica do guest (func_002BA76C devolve 0 após ~25 records, o driver
func_00041D5C sai) — mas SÓ agora é uma conclusão válida (antes era artefacto de compilação). Bug secundário: o 1.º
F2B-FWD faz delta=262144 (conta os 256KB do pump duas vezes, ts74->0). Próximo: (a) corrigir o double-count do 1.º
forward; (b) achar porque o driver só faz 1 pass de ~50 records — é o return-0 do guest, AGORA testável com fixes reais.

**LIÇÃO CRÍTICA:** o `build_macos.sh` linka o .o ANTIGO se um chunk falha a compilar (silenciosamente). SEMPRE compilar
o chunk standalone após editar, ou verificar que a probe nova dispara, antes de concluir "invariante". Isto invalidou
~5 conclusões desta sessão (§4cx-§4cz). Ganhos sólidos (crash E324, spu6 E330) intactos; e AGORA os fixes do F2B correm e
avançam file_pos/rem — o funil reabre.


## §4db RECONCILIAÇÃO (E350): os fixes do F2B FUNCIONAM; o jogo faz UMA pass de TOC (~50 records) e vai ao menu -> [D] precisa do nível

Com os fixes a compilar (§4da), medido em 90s (combo REMDEC+FORWARD+WAKE):
- `func_002BA76C` chamado **1x** (não em loop), processa ~50 records DENTRO dessa chamada, devolve 0, driver sai.
- `F2Bfp` avança 0->2432 e **PLATEIA** (2.4KB); `rem` desce e para; o boot vai ao menu (guest/menu).

Ou seja: os fixes do F2B FUNCIONAM (file_pos avança, rem desce, ring refila forward — a raiz de §4cu/§4cr RESOLVIDA no
mecanismo), MAS o jogo faz **UMA pass de load (a TOC do contentor, ~50 records, 2.4KB)** durante o intro e depois
**ABANDONA para o menu** — não faz o load dos CORPOS dos recursos (os 20MB).

**RECONCILIAÇÃO das minhas conclusões contraditórias:**
- E326 ("o loader QUER os 20MB, rem=20MB") — CERTO: a INTENÇÃO do loader é o load completo.
- §4cf/E344 ("[D] precisa do load de nível") — TAMBÉM CERTO: o JOGO só faz uma pass de TOC no intro; o load dos corpos
  (os 20MB -> 27 shells + 13 arenas) só acontece quando o jogo faz MUITAS passes = ao entrar num nível ([G]).
- Os dois não eram contraditórios: rem=20MB é a intenção; uma-pass-de-TOC é o que o jogo dá no intro.

**Estado REAL (pós-correção de compilação):** [D] precisa de DUAS coisas, ambas agora claras:
1. **Os fixes do F2B** (REMDEC + FORWARD) — que FIZ e FUNCIONAM (file_pos avança, rem desce) — para o stream forward ser
   correcto quando o load acontecer. (Falta limpar o double-count do 1.º FORWARD e promover de gated.)
2. **[G] (menu->gameplay / entrar num nível)** — para o jogo fazer as passes de load dos corpos. É o gate final.

**Lição de compilação (§4da) crítica:** ~5 conclusões desta sessão foram invalidadas por um erro de compilação silencioso
(build linka .o antigo). SEMPRE compilar o chunk standalone após editar. Ganhos: crash (E324), spu6 (E330), E316-REARM, e
AGORA os fixes reais do F2B forward (REMDEC/FORWARD) que resolvem o "rem preso / file_pos a 0" no mecanismo. [D] fica no
gate [G] (nível) com o F2B já preparado.

## §5 Ferramentas que ficaram

- `patch_e128_csave_check.py` aceita nomes de função por argumento (o verificador anda de nível em nível).
- Sondas gated por `PS3_TRACE_REG`: `patch_e149_voice_probe.py`, `patch_e150_fios_ops_probe.py`,
  `patch_e151_fios_done_probe.py`, `patch_e153_stream_update_probe.py`.
- `PS3_USLEEP_PRIO` (default on) e `PS3_SPU_TASK_TICK_MS` (default 2): melhorias de escalonamento medidas em §1;
  não são o fix e não substituem a paridade com o console — a manter só se o baseline não regredir.
- Auditoria de jump tables truncadas: reproduzível com o `discover_jump_tables` antigo vs novo sobre o ELF.

---

## §4dc — E351: KEEPGOING refutado (frame errado) — o gate de [D] confirmado como [G]

**Hipótese (E351):** o body-load do R_PermA (→ 27 CGOWShader shells + 13 arenas `WAD_R_*`)
não dispara porque o *driver externo* `func_00041D5C` (loop `loc_00041FE4`: chama
`func_002BA76C`; `if(r3!=0) goto`) **sai** quando `func_002BA76C` devolve 0 depois da
passagem de TOC, apesar de a reserva do loader `rem` (`ts+0x1AC`) ainda ser ~20 MB.
Teste decisivo: forçar o loop do driver enquanto `rem` grande (`PS3_F2B_KEEPGOING`, gated OFF).

**MEDIDO (sobrevive):**
- Com a recipe E275 completa (`PS3_WAD_HOST_READ/FIOS_STREAM_PUMP/F2B_HOST_FILL/FIOS_F2B_MOVIEIO`
  + `F2B_REMDEC/FORWARD/WAKE/ENSURE_BODY` + `ARENA_ROOT2BP` + `MOVIE_EOS/VDEC_FORCE_SEQDONE_MS=8000/MOVIE_VT_REPLAY`),
  o F2B engata (83 hits): `F2B-FWD` avança `fp` **262192→264144 / 20169344 (1,3%)** em 40 fills;
  `F2B-REMDEC` decrementa `ts+0x1AC` (rem = 0x133BE80 ≈ 20 MB, ≫ 0x1000).
- **`KG-PROBE` = 0**: o check de `func_00041D5C` (`loc_00041FE4`) **nunca é alcançado** nesta
  fase. `g_f2b_ts` está setado e `rem>0x1000` — logo as condições do force estavam satisfeitas;
  o force não disparou porque **o driver não corre aqui**.
- O stream do WAD é bombeado pelos OUTROS chamadores de `f2b_stream_ensure`
  (`func_002BA7F0` @002:286453/286568, `func_002BA76C` @001:45950/46140/46417), **não** por
  `func_00041D5C`.
- Guest chega a `guest/menu`; único shader compilado = demo `FlashUI.ps3fx`
  ("Unknown combination string, disable USE_PRECALCULATED_SHADER_CRCS"); **0** `CGOWShader`
  shells, **0** arenas `WAD_R_*`.

**CAI (inferido, RETRACTADO):** "forçar o loop de `func_00041D5C` enquanto `rem>0` faria o
body-load". Frame errado — `func_00041D5C` não é o driver ativo do stream do WAD nesta fase
(KG-PROBE=0). A mecânica KEEPGOING não pode conduzir o body-load. (Lição
`walking-a-corruption-upstream`: instrumentei o frame errado; o valor mau/plateau está a
montante.)

**MEDIDO (sobrevive):** no NOSSO boot, o front-end faz um preload **parcial** do R_PermA
(TOC + ~264 KB = 1,3 %) e transita para o menu; o plateau em `fp=264144` + `KG-PROBE=0`
mostram que nada está "à espera do loop do driver" — o guest segue para o menu depois do TOC.

**CAI / NARROW (E352, ver §4dd):** a frase original — "o body-load acontece na entrada de
nível ... [D] gated por [G]/[A], confirmado por duas medições ortogonais" — **over-reach**.
As duas medições (§4db + §4dc) são AMBAS do NOSSO lado e só provam que o NOSSO front-end pára
na TOC. **Não** provam que a consola também só carrega in-level. Toda a medição-oráculo dos
20 MB usou `EBOOT_skipintro.BIN` = **já dentro do nível** (linhas 2436/2474) — a consola no
regime de **front-end** nunca foi medida. Logo "needs-level" NÃO está estabelecido (nem
refutado) pelo oráculo; ver §4dd para o experimento que decide.

**Código:** `PS3_F2B_KEEPGOING` (force no driver) + `PS3_F2B_KGPROBE` (sonda) + captura
`g_f2b_ts` vivem só no lift gitignored (`recomp_macos_e162/ppu_recomp_000/001.cpp`), gated OFF,
no-op no baseline; **não** promovidos a `patch_*.py` por serem refutados. Somem no próximo re-lift.

---

## §4dd — E352: o nó do fio needs-level (E338↔E347↔E350) — o experimento decisor nunca correu

Ao reler o fio inteiro (E313–E351) sob a disciplina `retracting-your-own-findings`, o
contraditório é este e resolve-se sem escolher um lado:

| Achado | Afirma | Regime medido |
|--------|--------|---------------|
| §4cm (E333) | "loader QUER 20MB (rem=0x133C280 em state 2)" → refuta needs-level | **nosso boot**, front-end |
| E338/E339 | consola em state 3 consome 20MB "no mesmo ponto" → refuta needs-level | **oráculo `EBOOT_skipintro.BIN` = in-level** |
| E343/E347 | needs-level DESCARTADO; a raiz é rem/file_pos preso por resetters | oráculo **skip-intro = in-level** |
| §4db/§4dc (E350/E351) | jogo faz 1 pass de TOC → menu; body-load só na entrada de nível → **needs-level** | **nosso boot**, front-end |

**O erro comum, agora visível:** TODA a medição-oráculo dos 20 MB usou `EBOOT_skipintro.BIN`,
que salta a intro e entra **direto no 1.º nível** (o próprio ledger di-lo em 2436/2474). Logo
"a consola carrega os 20 MB no mesmo ponto" é uma observação **in-level** — não pode refutar
needs-level, porque needs-level é precisamente a hipótese de que o body-load só acontece
in-level. E do nosso lado, "rem=20MB" é o tamanho do container (a reserva), não uma prova de
que o guest exige lê-lo todo no front-end. Nem `refuta needs-level` (E338/E347) nem
`needs-level` (E350/E351) estão estabelecidos — **ambos assentam no regime errado ou em
inferência de um só lado**.

**Disciplina aplicada:** "a refutation resting on an absence needs a known positive in the same
regime." O positivo conhecido (consola a carregar 20 MB) foi medido num regime (in-level) que
**não é** o regime da pergunta (front-end). Retracta-se a força de §4cm/E338/E347 ("needs-level
refutado") E de §4db/§4dc ("needs-level confirmado") ao mesmo tempo: a questão está **em aberto**.

**O experimento que decide (nunca correu):** oráculo com **`EBOOT_intro.BIN`** (não skip-intro),
levado até ao **front-end** (menu/logos, ANTES de entrar num nível), com watch de `rem`
(`type_sys+0x1AC`, ts=0x869570) e do driver do R_PermA. Duas saídas:
- **consola front-end também só faz a TOC** (rem fica ~20 MB, sem body-load) → needs-level é
  REAL; [D] gated por [G]/[A]; a nossa paridade de front-end está correcta.
- **consola front-end faz o body-load** (rem desce, avail oscila/refill como E343) → é **bug
  NOSSO** de front-end (o resetter emaranhado de file_pos / rem preso de E347), e [D] é
  alcançável sem [G].

Este oráculo em regime de front-end precisa da GUI do RPCS3 conduzida até ao menu (pedido do
utilizador: "o utilizador é que roda o launcher"). É o **próximo passo cravado** e o único que
desempata o fio needs-level de 40 sub-secções. Ganhos sólidos do fio ficam intactos: crash
fixo (E324), spu6 liftado (E330), oráculo prova que o loader é o local (in-level), F2B REMDEC/
FORWARD compilam e funcionam (E349), lição do `.o` velho do build (E349).

---

## §4de — E353: DECISIVO (oráculo front-end) — needs-level REFUTADO; a consola faz body-load do R_PermA no front-end

O experimento que §4dd cravou como "pendente, precisa GUI" **correu — e é AUTÓNOMO** (headless):
RPCS3 suporta `--headless`, o config já tem PPU Interpreter (static) + GDB Server 127.0.0.1:2345,
e `probe.launch_rpcs3(path, headless=True)` arranca `EBOOT_intro.BIN` (regime de **front-end**,
não skip-intro). A afirmação repetida "precisa da GUI do utilizador" estava **ERRADA** — o oráculo
é headless; a regra "o utilizador roda o launcher" é para a janela Metal (ver pixels), não para o oráculo.

**Ferramenta:** `tools/rpcs3_probe/oracle_frontend_rem.py` — bp em `func_002BA76C` (0x002BA76C,
o passo interno do loader), lê `type_sys` do global fixo `0x0053FF4C` (= TOC 0x00541178 − 0x122C;
confirmado por `oracle_dump_ring.py`/`oracle_scopes.py`), depois `rem` (ts+0x1AC), `state`
(ts+0x1CC), `body1D4` (ts+0x1D4). `type_sys` = **0x00869570** (bss fixo).

**MEDIDO (sobrevive, reproduzido 2×), consola em `EBOOT_intro.BIN`, t≈21–25 s (durante a intro):**
- `#4`: `rem=0x0133C280` (20 MB), state 2 — container R_PermA aberto, tamanho cheio.
- `#6–#87…`: **state 3**, `rem` DESCE continuamente `0x0131C280 → … → 0x00FFC280` em passos de
  128 KB (0x20000); `body1D4` a acompanhar com valores **não-nulos** (0xFC830, 0xDC830, …, 0x362D0).
  **338 hits em state 3**; `rem` chega a valores baixos e depois há um segundo container
  (`rem≈0x016D4C40 > 20 MB`) — o loader continua para outros WADs. É o **body-load real**.

**Comparação com o NOSSO runtime (E338/E339, in-boot):** paramos em **state 2 com body1D4=0** e
concluímos. A consola, no MESMO ponto de front-end, entra em **state 3 com body1D4≠0** e consome o
corpo. A divergência é EXATAMENTE a transição **state 2 → state 3**.

**RESOLVE o fio needs-level (40+ sub-secções):**
- **REFUTADO**: needs-level (§4db/§4dc/§4351 E350/E351) — a consola faz body-load no front-end, não só in-level.
- **REFUTADO**: a força de §4dd "em aberto, precisa GUI" — o experimento correu, é autónomo, e decide.
- **CONFIRMADO**: a tese original de E338 — "o header de record devia dar body size ≠0 e dá 0"; é
  um **bug de parsing/framing do NOSSO loader** na transição state 2→3. **[D] é alcançável SEM [G].**

**Próximo passo (cirúrgico, oracle-first):** `body1D4` (ts+0x1D4) é escrito em `func_002BA9BC`
(setter de state=3, ppu_recomp_001.cpp:46176). A consola escreve 0xFC830 aí; nós ficamos em state 2
e nunca lá chegamos. Descobrir a origem de `body1D4` no header do record (que campo/offset o parse
lê) e porque o nosso dá 0 no MESMO ponto — bp na consola em `func_002BA9BC` e no ramo de state 2
que nos faz concluir (`func_002BA94C`→`func_002BA960` vtable+0x48, E337), comparar o header lido.
Isto é o funil de [D], agora **sem [G] no caminho** e com o oráculo a dar o valor-alvo (0xFC830).

**Precisão (E353b):** na consola `body1D4` acompanha `rem` com offset CONSTANTE
(0x0131C280−0xFC830 = 0x012FC280−0xDC830 = **0x0121FA50**), até `body1D4≈0` (record acabado) e
depois salta (novo record: 0x590→0xB470…). Logo `body1D4` (ts+0x1D4) é um **remanescente derivado
do record corrente**, NÃO um campo independente do header — não o perseguir como campo. O
discriminador real de [D] é a **CONDIÇÃO da transição state 2→3**: o que faz a consola começar a
consumir (entrar em `func_002BA9BC`, state=3) e a nós concluir em state 2 (`func_002BA94C`→
`func_002BA960` vtable+0x48, ts+0x1BC==0, E337). Alvo do próximo probe: bp consola no ramo de
decisão de state 2 dentro de `func_002BA76C` e ler o campo que decide (candidatos: ts+0x1BC,
ts+0x1C4, avail do ring ts+0x1A8+0x10) nós vs consola no MESMO ponto.

## §4df — E353c: o discriminador NÃO é campo/state-machine — é a ALIMENTAÇÃO do ring (dearchiver)

Estendi `oracle_frontend_rem.py` para ler os campos de decisão de state 2 (ts+0x1BC/0x1C4/0x1C8 e
o avail do ring ts+0x1A8+0x10) e comparar com o que sabemos do nosso runtime (E337/E340/E344).

**MEDIDO (consola, `EBOOT_intro.BIN`, front-end):**
- No **container-open** (`#4/#5`, state 2, `rem=0x0133C280`): `1BC=0 1C4=0 1C8=0 avail=0` — **IDÊNTICO**
  ao nosso runtime no mesmo ponto. A consola mesmo assim **avança para state 3** e consome (o ring
  passa a ser alimentado; rem desce 128 KB/passo).
- Numa **pausa de state 2 posterior** (`#22–#30`, `rem=0x0121C280`): `1C4=0x3385C580` (ponteiro de
  heap da consola), `1C8=1`, `avail=0x37D0` (ring com dados).
- Nós (E340/E344): presos em state 2 com **avail=0** e, no caminho natural, **0 bytes** entregues.

**CORRIGE o alvo de §4de/§4353 (E353):** o discriminador de [D] **não** é a condição da transição
state 2→3 nem um campo `ts` (são idênticos, todos 0 no container-open) — é a **ENTREGA DE DADOS ao
ring**. A consola alimenta o ring (o dearchiver/FIOS entrega o corpo) e o loader consome os 20 MB; o
nosso caminho natural entrega **0 bytes** (avail fica 0) e o loader fica preso em state 2. A máquina
de estados está **correcta**; o que falta é o dado.

**Reconecta a E255/E245/E344 (agora oracle-grounded e SEM needs-level/[G] no caminho):** o dearchiver
natural do R_PermA infla ~14 blocos e pára num wait FIOS (`lr=0x30D94C`) porque o 2.º filme da ponte
de replay compete pelo FIOS (E255); e sem F2B o caminho natural dá 0 bytes (E344, bloqueio de inflate
E245). **Raiz de [D] = alimentar o ring pelo caminho natural** (fixar a competição FIOS da ponte /
o inflate natural), NÃO o state-machine e NÃO [G]. O F2B forçado entrega no framing errado (E283).

**Próximo (cirúrgico):** (1) parar o filme/FIOS da ponte de replay ao armar o EOS (E255), medir se o
dearchiver natural passa dos ~14 blocos e enche o ring; (2) em paralelo, confirmar na consola que o
ring é alimentado pelo FIOS async durante state 3 (bp no produtor do ring). O oráculo dá o
comportamento-alvo: ring alimentado → state 3 → 20 MB → registry (27 shells + 13 arenas).

## §4dg — E353d: caminho natural medido — inflate OK (28 blocos), depois pool corrompe ANTES do R_PermA abrir

Medido o NOSSO runtime no caminho **natural** (sem F2B; `PS3_MOVIE_VT_REPLAY=1` completa a intro;
`PS3_HOST_INFLATE=1` + `PS3_TRACE_TYMAP=1`):

**SEQUÊNCIA MEDIDA:**
1. O dearchiver natural **INFLA 28 blocos** (`[HOSTINFL]`, último ~linha 4993, `evf set rc=0`). O
   inflate **funciona** — não é 0 (refuta a leitura "inflate 0 bytes" como estado actual) nem pára aos ~14 (E255).
2. **Depois** (linha 7913, DEPOIS do inflate parar): `[FREELIST-TAG-GUARD] 263040 entry head=0x0 → abort` (8×) —
   a arena de chunks (pool de handles) **corrompe**.
3. O loader `func_002BA76C` só é alcançado **1×**: `state=1 rem=0x0` (`lr=0x00041FE8`). O **container
   R_PermA NUNCA abre** (rem fica 0, não 0x133C280).

**CORRIGE §4df/§4353c (E353c):** o próximo passo que eu apontei — "parar a competição FIOS da ponte
de replay (E255)" — está **refutado como causa actual**: o inflate corre (28 blocos), não está preso
num wait FIOS aos 14. O blocker é a **corrupção do pool** (FREELIST-TAG-GUARD) que acontece DEPOIS do
inflate e ANTES de o R_PermA abrir. É a **doença do megaprojecto** de [[parede-g-gow2-cadeia-de-paliativos]]
(string-como-ponteiro a corromper a arena), no caminho natural do WAD.

**Cadeia de [D] agora completa e oracle-grounded (sem [G], sem needs-level):**
- Consola (oráculo E353): R_PermA abre (rem=20MB) → state 3 → body-load → registry (27 shells + 13 arenas), no front-end.
- Nós (natural, E353d): inflate 28 blocos → **pool corrompe** → R_PermA **não abre** (state 1, rem=0) → registry vazio.
- A máquina de estados e o inflate funcionam parcialmente; o que falha é **a montante do open do R_PermA**: a arena de chunks corrompe.

**Próximo (walking-a-corruption-upstream, metodologia do megaprojecto que caiu em 6 rondas):** a
`FREELIST-TAG-GUARD` em `func_00263040` aborta com `head=0`/`0x61`. Instrumentar UM call-site do
alocador da arena de chunks, capturar input+output vivos, contar a população (1-em-N), e subir até
quem escreve o `head` tagged/ASCII. O valor com estrutura (0x61='a', tags 0x84xxxxxx) nomeia a origem.
Não remendar o guard (é band-aid) — subir até quem corrompe. A consola dá o comportamento-alvo.

## §4dh — E353e: o blocker natural É a corrupção pool-de-handles conhecida (§4a/E312), agora ligada ao oráculo

O contexto da corrupção de §4dg (natural.log ~linha 7905) nomeia a origem:
```
[vm] UNCOMMITTED read32  0x84000017 ra=func_002ACBE8+0x174   (lookup por chave, E263)
[vm] UNCOMMITTED write32 0x84000017 ra=func_002ACD38+0x114
[vm] UNCOMMITTED write32 0x84000013 ra=func_002AB0E4+0x270
[vm] UNCOMMITTED read32  0x84000013 ra=func_002B2AB0+0x24C
```
`0x84000017` = flag|size 0x17; `0x84000013` = flag|size 0x13 (=19) — os **ponteiros TAGGED** do
"pool de handles". É **exactamente** a corrupção §4a/E312 já documentada em CLAUDE.md [C]/[D]:
a arena de chunks (`func_00263040`) deref'a handles tagged porque a **free-list da arena Root
(0x40004020) está corrompida em nós** (chunks sobrepostos), **limpa na consola** — E312,
oracle-confirmado (decisão/threshold/SWAR strcmp/arena/overlap TODOS batem; só a free-list difere).

**Fecho do arco desta sessão (E351–E353e):** o oráculo de front-end (autónomo) provou que [D] é
alcançável SEM [G] (needs-level refutado); a máquina de estados e o inflate funcionam; e o blocker
natural é a corrupção pool-de-handles CONHECIDA (§4a/E312), a montante do open do R_PermA. Ou seja:
esta sessão **religa** o breakthrough do oráculo à raiz accionável que E312 já isolou. Não é um
blocker novo — é o megaprojecto (string/tag-como-ponteiro na free-list da arena Root).

**Próximo (ciclo dedicado, fresh): o walk da free-list da arena Root vs consola** (E312), com o
oráculo agora provado headless: bp na consola em `func_00263040`/`func_00263178`, comparar a
free-list (head/next/tags) com a nossa em 0x40004020, e subir até quem escreve o nó tagged
(`0x84000013`=flag|19) que sobrepõe a tabela de objectos 0x40004480. Metodologia: a do megaprojecto
que caiu em 6 rondas (walking-a-corruption-upstream). O oráculo dá a free-list-alvo limpa.

## §4di — E353f: CONSOLIDAÇÃO GRANDE — o oráculo front-end (E353) fecha o arco com o root arena↔tabela (§4bh–§4bm)

Reli o frontier arena/tabela (E298–E305, §4bf–§4bm) e ele CONVERGE com o oráculo desta sessão. A
história de [D], única e coerente, ponta a ponta:

**Consola (oráculo E300/E301/E303 + E353 desta sessão):** no front-end, processa a entrega natural
COMPLETA do R_PermA (20 MB — E353: rem desce 0x133C280→0x00FFC280, state 3, 338 hits). Isso popula o
heap (~8 MB alocados ANTES da arena) → a arena (`func_00263178`) cai LONGE (`0x307D9D64`), distinta da
tabela de objetos (`0x30004480`) → o grow da tabela (`func_002AB070`) não a toca → o índice fica
estável (down-counter a 14) → todos os objetos registados → o 6.º shell lê um objeto válido
(`0x30636900`) → registry popula (27 shells + 13 arenas) → shaders do nível compilam.

**Nós:** a entrega do R_PermA é INCOMPLETA — (a) host-fill parcial (256 KB, E275) chega ao 6.º shell mas
sub-popula o heap; (b) natural puro (E353d) corrompe ainda antes de abrir o container (28 blocos de
inflate → FREELIST-TAG-GUARD, rem=0). Heap sub-populado → a arena cai ADJACENTE (`0x40004050`) à tabela
(`0x40004480`) → o grow da tabela realoca via o alocador da arena e SOBREPÕE (§4bm: slots 10–15 a ZERO,
16–17 tagged) → objetos perdidos → o 6.º shell lê slot tagged (`0x84000013`) → `func_002ACBE8` percorre
árvore tagged → 0 → dispatch lixo → crash `0x4BFFFE34`.

**O que E353 ACRESCENTA ao frontier (decisivo):** o §4bi/§4bl atribuíram a sub-população à "entrega
parcial" mas deixavam aberto se a entrega COMPLETA acontece no front-end ou só no nível ([G]/needs-level).
**E353 fecha isso: a consola faz a entrega completa dos 20 MB NO FRONT-END (autónomo, headless).** Logo
o fix — completar a entrega natural do R_PermA — é alcançável SEM [G]. needs-level está fora.

**O nó a desatar (o fix, agora inequívoco):** a entrega natural do R_PermA corrompe cedo (E353d: pool
aos 28 blocos). É a MESMA corrupção arena↔tabela, só que numa fase anterior. Como a consola entrega os
20 MB sem corromper, a pergunta cirúrgica é: **na consola, o que popula o heap ANTES da arena (os ~8 MB
até 0x307D9D64) — é o corpo do R_PermA ou alocações do front-end anteriores?** Oráculo (headless, já
provado): bp na criação da arena (`func_00263178` de `lr=0x263074`) e ver o estado do heap/entrega nesse
instante, consola vs nós. Se forem alocações anteriores, o nosso défice é saltarmos essa fase do
front-end; se for o corpo do R_PermA, o fix é destravar o inflate natural além dos 28 blocos.

**Estado (a caracterização mais completa e verificada do projeto):** [D]/[E] não alcançado; o registry
continua vazio (0 shells, 0 arenas). MAS o root está totalmente mapeado e oracle-grounded, [G] está
FORA do caminho, e o fix (entrega natural completa do R_PermA → heap populado → arena longe da tabela)
tem a consola como referência-alvo medível headless. Arco desta sessão: E351–E353f.

## §4dj — E353g: CAPSTONE (⚠ ELO DO FUNDO RETRACTADO por §4dk/E353h)

> **RETRACTAÇÃO PARCIAL (E353h, medido):** o elo do fundo — "func_002B42B4 cancela a leitura do
> R_PermA cedo" — está **ERRADO**. A op que func_002B42B4 cancela é a do **SmLogo** (o filme de
> logo do boot), NÃO o R_PermA: `[CANCEL-OP] op=0x00869E04 +0=0x005173A0 +44=0x536D4C6F="SmLo"`.
> Sobrevive: a espinha do TOPO (registry→arena↔tabela→heap→entrega incompleta) e E353 (consola faz
> 20MB no front-end, [G] fora). Cai: a atribuição do inflate-stop a um cancel FIOS do R_PermA. Ver §4dk.

## §4dj — E353g: CAPSTONE — a espinha causal completa de [D], do registry vazio ao cancel FIOS

Medindo o caminho natural (E353d) até ao fundo, a espinha causal de [D] fica COMPLETA e conectada,
unindo o oráculo de front-end desta sessão (topo) com a raiz de entrega [C]/E263 (fundo):

```
registry VAZIO (0 shells, 0 arenas)                                    [sintoma, medido]
  ← o 6.º shell CGOWShader lê slot tagged 0x84000013 → dispatch lixo → crash 0x4BFFFE34   [§4bm]
  ← a tabela de objetos (0x40004480) e a arena (func_00263178) SOBREPÕEM-SE               [E305]
  ← heap SUB-POPULADO → a arena cai adjacente (0x40004050) e não longe (consola 0x307D9D64) [E301/E303]
  ← a entrega do R_PermA é INCOMPLETA (nós); a consola entrega 20 MB no front-end          [E353, oráculo]
  ← o inflate natural PÁRA aos 28 blocos (HOSTINFL)                                        [E353d]
  ← func_002B42B4 CANCELA a leitura do R_PermA cedo (chama func_0030AE58, limpa op+8)      [medido, = [C]/E263]
  ← a decisão vem de func_002B42EC (bit16 em op+0xC) / func_002B4330 (bit17) → func_002B42B4 [medido agora]
  ← o DONE-poll marca a op FIOS "done/erro" prematuramente, com o movie HLE AINDA ativo    [E244/E263, fundo]
```

**Evidência-chave (natural.log):** logo após o 28.º `[HOSTINFL]`: `[CONTENT] hold ... paused (movie HLE
active)` + `[FIOSOPEN] 42B4-CANCEL-YIELD after DONE early cancel` (2×). O movie HLE ainda corre → o
DONE-poll cancela a op do R_PermA → o inflate pára aos 28 blocos → heap sub-populado → arena↔tabela →
6.º shell tagged → registry vazio.

**O que esta sessão estabeleceu de novo e definitivo:**
- **[G] FORA do caminho / needs-level REFUTADO** (E353, oráculo front-end autónomo): a consola faz os
  20 MB no front-end. O fix não precisa de entrar num nível.
- **A espinha inteira conectada**: pela 1.ª vez, o registry vazio liga-se, nível a nível medido, ao
  cancel FIOS (func_002B42B4) e ao DONE-poll (func_002B42EC/4330). O root de [D] (arena↔tabela) e o
  root de [C] (entrega/DONE-cancel) são o MESMO fio.
- **Oráculo headless provado** (`oracle_frontend_rem.py`): a consola é referência-alvo medível sem GUI.

**O fix frontier (dedicado, deep, E244/E263):** porque func_002B42EC/func_002B4330 marcam a op do
R_PermA como done/erro (bit16/bit17 em op+0xC) aos 28 blocos, quando a consola só o faz após os 20 MB.
É a corrida de wakeup do FIOS com o movie HLE ativo (E244: op completa mas o submetora dorme em lwcond
opWait; a limpeza do scheduler cancela a op sem dono). Alvo-consola: bp em func_002B42EC/4330 na
consola e ver que só disparam no fim do R_PermA; comparar op+0xC e o estado do movie HLE. Fixar o
early-cancel → o inflate completa os 20 MB → heap populado → arena longe → registry popula → [D].

**Estado:** [D]/[E] não alcançado (registry vazio). MAS a espinha causal está COMPLETA, conectada e
oracle-grounded, do sintoma ao mecanismo mais fundo, com [G] eliminado. É a caracterização terminal
de [D] deste ramo — o próximo ciclo é o fix da corrida FIOS DONE-cancel (E244/E263), não mais diagnóstico.

## §4dk — E353h: RETRACTAÇÃO medida — o cancel é do SmLogo, não do R_PermA; o natural está preso na fase de LOGOS

Sonda `PS3_DUMP_CANCEL_OP` em `func_002B42B4` (dump da op ANTES do cancel), caminho natural:
```
[CANCEL-OP] #1 op=0x00869E04 +0=0x005173A0 +4=0x43018528 +8=0x430094C0 +C=0x00010020 +40=0 +44=0x536D4C6F
[CANCEL-OP] #2 op=0x00869E04 +0=0x005173A0 +4=0x43018580 +8=0x430095A0 +C=0x00010020 +40=0 +44=0x536D4C6F
```
`op+0x44 = 0x536D4C6F = "SmLo"` = **SmLogo** (o filme de logo do boot). A op cancelada é a do
**SmLogo**, NÃO o R_PermA.

**CAI (retractação do elo do fundo de §4dj/E353g):** "func_002B42B4 cancela a leitura do R_PermA cedo"
— ERRADO. Os 2 cancels são de ops de **filme de logo** (SmLogo), normais quando um logo acaba. Foi um
erro de frame (correlação inflate-stop ↔ cancel ≠ o cancel ser do R_PermA) — a MESMA classe do KEEPGOING
(E351). A sonda apanhou-o (disciplina `walking-a-corruption-upstream`: instrumentar a op, não assumir).

**SOBREVIVE (medido):** E353 (consola faz os 20 MB no front-end; [G]/needs-level fora) e a espinha do
TOPO (registry → 6.º shell tagged → arena↔tabela → heap sub-populado). O que cai é só a atribuição do
inflate-stop a um cancel FIOS do R_PermA.

**Quadro CORRIGIDO do caminho natural (medido, conservador):**
- A op cancelada é SmLogo; o loader do R_PermA `func_002BA76C` corre só 1× em `state=1 rem=0` — o
  **container R_PermA NUNCA abre** no natural. Os 28 blocos `HOSTINFL` NÃO estão provados serem R_PermA
  (podem ser assets de logo/front-end).
- Ou seja, o natural está preso **na fase de LOGOS/front-end** (SmLogo→scep_e→bluepoint), **antes** de
  o loader do R_PermA sequer arrancar. É upstream de TUDO o que §4dg–§4dj analisaram como "entrega do R_PermA".

**Open (o real blocker do natural, honesto):** porque é que o nosso front-end não progride dos logos
(SmLogo) para o loader do R_PermA (`rem` fica 0), quando a consola progride (E353, alcança o R_PermA no
front-end). NÃO é o cancel FIOS do R_PermA (refutado). Próximo: medir a FSM do front-end/movie no ponto
do SmLogo — porque não avança para o próximo logo / para o loader — consola vs nós (oráculo
`EBOOT_intro.BIN`, já provado headless). O F2B-forçado salta esta fase (força o R_PermA) e chega ao 6.º
shell; o natural nem lá chega.

**Estado:** [D]/[E] não alcançado; registry vazio. A espinha do topo e E353 sobrevivem; o elo do fundo
(cancel=R_PermA) foi refutado por medição. O blocker natural re-situado: progressão do front-end nos
LOGOS, upstream do open do R_PermA. 6 retractações medidas nesta sessão (needs-GUI, needs-level,
body1D4-campo, E255-FIOS, alvo-E353c, cancel=R_PermA) — todas apanhadas por medição, não por releitura.

## §4dl — E353i: ROOT2BP + F2B NÃO popula o registry de nível (resultado NEGATIVO, honesto)

Testei se o redirect da arena (`PS3_ARENA_ROOT2BP`, E324, que tira a colisão arena↔tabela) + F2B
faz o registry de nível crescer além dos 5 shells. **Não faz.**

**MEDIDO (root2bp.log vs natural.log):**
- `[SHADERSRC] N>0` dispara 18× — **MAS são shaders de engine/UI**: `FlashUI.ps3fx`, `Default.ps3fx`
  (HOSTRES `flashui.cfx`; "Unknown combination string, disable USE_PRECALCULATED_SHADER_CRCS" = o
  caminho demo/CRC-bypass que o projeto EXCLUI como aceite). **Idêntico (18) com E sem ROOT2BP** →
  ROOT2BP não muda nada aqui.
- `[LDRSH] = 0` (nenhum shader de LOADER/nível). `WADLD-SM #1 state=1 rem=0` — o loader do R_PermA
  não progride. 24 marcadores FREELIST-TAG-GUARD/ICALL (corrupção do pool na fase de front-end).
- O `0x4BFFFE34` não aparece (0×) — mas porque o boot nem chega ao 6.º shell; trava antes, na
  corrupção do front-end.

**Quase over-claim, evitado por verificação:** ao ver `SHADERSRC N>0` (o sinal que o meta chama de
meta) quase o reportei como sucesso. Verifiquei: são os shaders de UI/engine (FlashUI/Default), NÃO a
construção de nível (27 shells CGOWShader + 13 arenas WAD_R_*). O registry de NÍVEL continua a 0.

**Conclusão (honesta, negativa):** ROOT2BP só importaria AO 6.º shell (colisão arena↔tabela na
construção de nível); estes runs nem lá chegam — travam antes, na corrupção do pool do front-end
(E353d/E353e: FREELIST head=0x61='a', string-como-ponteiro). O blocker é UPSTREAM da construção de
nível. Sobrevive: E353 (consola faz 20MB no front-end, [G] fora) e a espinha do topo. O experimento
ROOT2BP não avança o registry de nível porque o loader não lá chega.

**Estado:** [D]/[E] não alcançado; registry de nível vazio (os 18 SHADERSRC são UI/engine, não nível).
O blocker medido é a corrupção do pool no front-end (E312/string-como-ponteiro), antes do loader do
R_PermA. 7.ª verificação da sessão que evita um falso-positivo por medição.

## §4dm — E353j (⚠ "RACE/flaky" RETRACTADO por §4dn/E353k: é OVERFLOW determinístico do scope stack) a corrupção do pool está no SCOPE STACK do provider da arena

> **RETRACTAÇÃO (E353k):** a caracterização "RACE flaky / string-byte 0x61" está ERRADA. Medido: o
> índice do scope stack **transborda o limite** (idx=128,129,130 > lim=128) — é um OVERFLOW
> DETERMINÍSTICO, não uma race. A "flakiness" era só se o boot alcançava o overflow na janela de
> tempo. O 0x61 foi 1 hit; o dominante é idx>lim → provider devolve 0. Ver §4dn.

Continuando a walk (user: "Continue"), instrumentei o provider da arena `func_00262808` e o entry de
`func_00263040`:

**MEDIDO:**
- `func_00262808` (provider) lê um **SCOPE STACK** em `r2-0x20D8`: `base=*(stk+0)`, `idx=*(stk+4)`,
  `lim=*(stk+8)`; devolve `arena = base[idx]` (se `idx<lim`), senão 0 (OOB→NULL). Medido:
  `stk=0x008370A8 base=0x008370BC idx=127 lim=128 arena=0x40004020`.
- **Run LIMPO (arenastk.log):** idx fica em 127 (<128), arena=0x40004020 válida, **0 crashes**
  (FREELIST/ICALL=0). O boot NÃO corrompe.
- **Run CORROMPIDO (natural3.log):** o guard de `func_00263040` dispara com o arena/head=**0 e 0x61**
  (a entrada do scope stack `base[idx]` ficou 0 ou string-byte 'a'). FREELIST-TAG-GUARD + ICALL crash.
- **A minha sonda de entry BAD (`263040 ENTRY BAD`) NÃO disparou no run limpo** — confirma: quando o
  scope stack está intacto, a arena é válida e não há crash.

**CONCLUSÃO (nova, medida):** a corrupção pool-de-handles é **FLAKY — uma RACE** que escreve 0/`0x61`
sobre a entrada do scope stack do provider da arena (`base[idx]`, `idx=127` → EA≈`0x008372B8`, bss fixo).
NÃO é determinística; runs idênticos divergem (limpo vs corrompido). Isto é consistente com a doença do
megaprojecto (string-como-ponteiro, `0x61`='a') MAS agora localizada ao **scope stack do provider da
arena** e caracterizada como **race**, não corrupção estática. Liga a parede-F (escopo dos gestores).

**Próximo (walk, o writer da race):** watch de stores à EA `0x008372B8` (a entrada do scope stack em
idx=127) para apanhar o thread/função que lá escreve o string-byte — `PS3_WATCH_STORE` na região do
scope stack `0x008370BC..0x008372BC`, correndo várias vezes até apanhar o run corrompido (é flaky).
A natureza de RACE aponta para um escritor concorrente (FIOS/movie HLE) a pisar o bss do scope stack.

**Estado:** [D]/[E] não alcançado; registry de nível vazio. Progresso desta continuação: (1) ROOT2BP
NÃO popula o registry (negativo, §4dl); (2) a corrupção do pool está agora localizada ao **scope stack
do provider da arena** e é uma **RACE flaky** (§4dm), não estática — refina §4dh (que a punha na
free-list da arena) para o nível acima: a ENTRADA do scope stack que dá a arena.

## §4dn — E353k: ROOT do crash natural = OVERFLOW do scope stack dos gestores (idx > lim=128) — MECANISMO claro, OCORRÊNCIA intermitente (ver §4do)

Cadeia do alocador (medida): `func_00263040` (arena alloc) ← wrappers `func_00263B70/BA8/BE0` ←
`func_00262808` (provider) devolve `arena = base[idx]` do SCOPE STACK (`stk=0x008370A8`,
`base=0x008370BC`, `idx=*(stk+4)`, `lim=*(stk+8)=128`).

**MEDIDO (sonda `[ARENASTK-BAD]`):**
```
[ARENASTK-BAD] idx=128 lim=128 base=0x008370BC base[idx]=0xDEADBEEF
[ARENASTK-BAD] idx=129 lim=128 ...
[ARENASTK-BAD] idx=130 lim=128 ...
```
O **índice do scope stack TRANSBORDA o limite**: idx corre 128→129→130 com lim=128 (slots 0..127).
Quando `idx>=lim`, o provider devolve **0** → arena NULL → `func_00263040` (guard) aborta → falha de
alocação → FREELIST/ICALL crash. Determinístico (idx claramente excede lim), NÃO uma race.

**RETRACTA §4dm (E353j):** "RACE flaky / string-byte 0x61" estava errado. É OVERFLOW do scope stack.
A "flakiness" (run limpo vs corrompido) era só se o boot alcançava o ponto de overflow na janela de
90 s — não uma race de escrita. O `0x61` (1 hit) e o `0` (dominante) são ambos `base[idx]` fora do
range (lixo/DEADBEEF) por idx>lim.

**ROOT (determinístico, medido):** o **scope stack dos gestores transborda** — empurramos ~2-3
escopos a mais do que desempilhamos (idx passa de 127→128→129→130 sem voltar). Na consola idx fica
≤127. É a parede-F ("escopo dos gestores") vista por outro ângulo: um **push/pop desequilibrado** do
scope stack (um pop em falta, provavelmente por control-flow/early-return do lift que salta o pop —
a MESMA classe do fallthrough cross-fragment 2550C8 e do if-goto E129).

**Próximo (o fix, pointável):** achar quem incrementa `idx` (`*(0x008370AC)`) — o push do scope — e
quem o decrementa — o pop — e porque há mais pushes que pops (um pop saltado por early-return/goto no
lift). Comparar a contagem push/pop com a consola (oráculo headless, bp no push e no pop do scope).
Corrigido o balanço, idx fica ≤127, o provider devolve a arena válida (0x40004020), as alocações do
front-end sucedem, o boot passa dos logos, o R_PermA carrega e o registry popula.

**Estado:** [D]/[E] não alcançado; registry vazio. MAS o root do crash natural está agora
DETERMINÍSTICO e localizado: **overflow do scope stack dos gestores** (idx>lim=128), um
push/pop desequilibrado — a mesma classe de bug de control-flow do lift que o projeto já venceu 2×.
Esta continuação: ROOT2BP negativo (§4dl) → scope stack (§4dm, "race" retractado) → **overflow
determinístico do scope stack (§4dn)**. 9 correcções medidas na sessão.

## §4do — E353l: push/pop do scope stack localizados; o overflow é INTERMITENTE (corrige "determinístico" de E353k)

Localizei o par push/pop do scope stack:
- **PUSH (enter scope):** `func_00262C8C` — `idx++` (`*(stk+4) += 1`).
- **POP (leave scope):** `func_00262C64` — `idx--` (`*(stk+4) -= 1`).

**MEDIDO (sonda `[SCOPE-PUSH]`/`[SCOPE-POP]` para idx≥118):** num run LIMPO o idx oscila 126↔127 com
pares push/pop **BALANCEADOS**, cada um de callers diferentes (POP lr=0x24271C ↔ PUSH lr=0x242748;
POP 0x2B4AF0 ↔ PUSH 0x2B4D7C; POP 0x24A80C ↔ PUSH 0x24AD84; POP 0x2BA4DC ↔ PUSH 0x2BA50C).
`ARENASTK-BAD=0` — **sem overflow neste run**.

**CORRIGE E353k ("determinístico"):** o overflow **NÃO** é determinístico — é **intermitente**. O
run de E353k (arenastk3) transbordou (idx 128,129,130); este balanceou (idx≤127). O MECANISMO
(idx>lim → provider devolve lixo → crash) está certo e medido; a OCORRÊNCIA varia entre runs
idênticos. Em condições normais o push/pop balanceia; num subconjunto de runs, **um push a idx~127
escapa ao seu pop** → idx passa a 128+ → overflow. (A "flakiness" de §4dm afinal era real, mas o
mecanismo é o overflow, não uma race de string-byte.)

**Próximo (o fix, pointável mas precisa apanhar o run que transborda — flaky):** correr com
`PS3_TRACE_SCOPESTK` até apanhar um run com `SCOPE-PUSH idx 127->128` SEM o `SCOPE-POP` correspondente,
e ler o `caller-lr` desse push — é a função que entra num scope e não sai (early-return/goto do lift
que salta o `func_00262C64`, ou uma condição de erro que aborta antes do pop). Comparar o balanço
push/pop dessa função com a consola. Corrigido o pop em falta, idx fica ≤127, a arena é sempre válida,
o boot passa dos logos → R_PermA → registry.

**Estado:** [D]/[E] não alcançado; registry vazio. Root do crash natural localizado ao par push/pop do
scope stack (`func_00262C8C`/`func_00262C64`), overflow **intermitente** (um push sem pop em alguns
runs). Esta continuação (medida, 10 refinamentos): ROOT2BP negativo → scope stack → overflow (mecanismo)
→ push/pop localizados + ocorrência intermitente. O funil está no par push/pop; falta apanhar o run
que transborda para nomear o caller do push órfão.

## §4dp — E353m: APANHADO o push órfão — `func_0042A0C8` (lr=0x42A1A4) empurra sem pop → overflow

Run que transbordou (scope2.log), sequência push/pop no fim:
```
[SCOPE-PUSH] idx 127->128 lr=0x0042A1A4     (func_0042A0C8)
[SCOPE-POP]  idx 128->127 lr=0x00041214
[SCOPE-PUSH] idx 127->128 lr=0x00041288
[SCOPE-POP]  idx 128->127 lr=0x00041214
[SCOPE-PUSH] idx 127->128 lr=0x00041288
[SCOPE-PUSH] idx 128->129 lr=0x0042A1A4     ← RUNAWAY
[SCOPE-PUSH] idx 129->130 lr=0x0042A1A4     ← RUNAWAY (mesmo caller, sem pop)
```
**O push órfão é `func_0042A0C8`** (o `bl func_00262C8C` em 0x0042A1A0, lr=0x42A1A4): empurra o scope
(idx++) e RETORNA, e em runs de overflow é chamado repetidamente (idx 128→129→130) **sem o pop
correspondente**.

**Estrutura de `func_0042A0C8` (medida):** anda uma lista ligada (`r30=r28+0x38`, `r31=*(r30)`) e tem
**DUAS saídas**: (1) tail-call cedo a `func_0042A1D4` (`if cr&8`, linha ~0x42A100) — que **não empurra**;
(2) saída normal em `loc_0042A198`: `func_002AAB84` → **`func_00262C8C` (PUSH)** → return. A saída
normal empurra sempre; a cedo não. O desequilíbrio (push sem pop) é a classe clássica de **duas saídas
com push/pop assimétrico** — a mesma família do fallthrough cross-fragment 2550C8 e do if-goto E129 que
o projeto já venceu 2×.

**Próximo (o fix, agora com o alvo exacto):** comparar `func_0042A0C8` na consola (oráculo headless,
bp em 0x42A0C8 e nas duas saídas + no push/pop do scope) — ver se a consola empurra na saída normal
E se o pop está no caller (`func_0042A1D4`? ou o chamador de `func_0042A0C8`?), e porque no nosso lift
o pop é saltado num subconjunto de runs (a lista ligada que `func_0042A0C8` anda leva-o à saída de push
mais vezes que a consola, OU o pop no caller é saltado por control-flow). Corrigido o balanço, idx≤127,
arena sempre válida, boot passa dos logos → R_PermA → registry.

**Estado (o mais fundo do crash natural):** [D]/[E] não alcançado; registry vazio. Mas o crash natural
está localizado ao PUSH ÓRFÃO **`func_0042A0C8`** (0x42A1A4) que empurra o scope stack sem pop → overflow
(idx>128) → provider devolve arena inválida → crash, ANTES do loader do R_PermA. Cadeia completa medida:
crash ← arena NULL ← provider idx>lim ← push órfão func_0042A0C8. Classe de bug conhecida (duas saídas,
push/pop assimétrico), com o alvo exacto isolado. Esta continuação: 11 medições, do "registry vazio" até
à função exacta do push órfão.

## §4dq — E353n: ORÁCULO CONFIRMA — a consola NUNCA transborda o scope stack; o overflow é bug NOSSO

Probe headless `oracle_scope.py`: bp na consola em `func_0042A0C8` (0x0042A0C8), lê o scope idx
(`*(*(0x0053F0A0)+4)`, lim `+8`).

**MEDIDO (consola, `EBOOT_intro.BIN`, front-end t≈22–26 s):**
- `func_0042A0C8` é chamado com scope **idx=126 → 125 → 124** (max=126, lim=128) ao longo de ~30 hits.
  O idx **DECRESCE** (a consola está a DRENAR scopes nesta fase) e **NUNCA excede 126** — nunca transborda.
- Nós (E353m, scope2.log): o idx TRANSBORDA para **128→129→130** via pushes de `func_0042A0C8` sem pop.

**CONFIRMADO (consola vs nós, decisivo):** o overflow do scope stack é um **bug NOSSO** (do lift), NÃO
o design do jogo. A consola equilibra (até net-drena, idx 126→124); nós acumulamos pushes → overflow →
provider devolve arena inválida → crash. A consola prova que `func_0042A0C8` e o seu protocolo de scope
são benignos quando o pop acontece.

**REFINA §4dp (walking-a-corruption-upstream):** `func_0042A0C8` é onde o overflow SURGE (pusha a
idx≥128), mas a raiz é a montante — na consola o idx entra em `func_0042A0C8` já a 124–126 e DECRESCE
(pops entre chamadas); em nós entra a 127+ e CRESCE (pops em falta ENTRE as chamadas de `func_0042A0C8`).
Faltam-nos ~2–3 pops sistemáticos nesta fase do front-end. Candidatos: (a) a saída cedo de
`func_0042A0C8` (`func_0042A1D4`) que devia popar e não popa no nosso lift; (b) um pop no caller do
loop que anda a lista; (c) `func_0042A0C8` a tomar a saída de PUSH onde a consola toma a de não-push.

**Próximo (o fix, direcção CONFIRMADA):** bp na consola em `func_0042A1D4` (saída cedo) e no pop
`func_00262C64` (0x00262C64) à volta de `func_0042A0C8`, para ver o pop que a consola faz e nós saltamos
— é um pop cross-fragment/early-exit do lift (classe 2550C8/E129). Corrigido, o idx fica ≤126 como a
consola, a arena é válida, o boot passa dos logos → R_PermA → registry.

**Estado:** [D]/[E] não alcançado; registry vazio. MAS o crash do caminho natural está CONFIRMADO como
bug NOSSO do lift (overflow do scope stack, a consola nunca transborda), localizado a `func_0042A0C8` e
aos ~2–3 pops em falta à volta dele. Cadeia inteira medida e verificada contra a consola: registry ←
crash ← arena NULL ← scope overflow (idx>128) ← pops em falta no lift (consola idx≤126). 12 medições
na continuação, do "registry vazio" até "bug de lift confirmado vs consola, alvo isolado".

## §4dr — E353o: CORRIGE §4dp — o push de `func_0042A0C8` está CERTO (consola pusha 100%); o pop em falta é NOUTRO sítio

Probe `oracle_a0c8_branch.py` (consola, bp em entry 0x42A0C8 / push-site 0x42A1A0 / early-exit 0x42A1D4):
```
[a0c8] DONE hits=400 counts={'entry': 200, 'PUSH-site': 200, 'early-exit(A1D4)': 0}
```
A consola **SEMPRE** alcança o push (entry=push=200, early-exit=0) — pusha a 100%, **igual a nós**.

**CAI (retracta a hipótese implícita de §4dp/E353m):** "`func_0042A0C8` toma a saída de push onde a
consola toma a cedo" — ERRADO. A consola também pusha 100% via `func_0042A0C8`. Logo o **push de
`func_0042A0C8` está CORRECTO** e a função é **VÍTIMA**, não a causa (walking-a-corruption-upstream:
o sítio onde o overflow surge não é onde o valor mau é produzido).

**SOBREVIVE (medido):** o overflow é bug NOSSO (consola idx≤126, nós >128, §4dq). A consola equilibra
~200 push com ~200 pop; nós fazemos ~200 push e ~197 pop → +3 acumulados → overflow. O **pop em falta
(~2–3 de ~200) está NOUTRA função**, e é intermitente (por isso alguns runs balanceiam).

**Próximo (o fix, alvo refinado — needle-hunt honesto):** o defeito é ~2–3 pops (`func_00262C64`)
saltados de ~200, intermitentes — NÃO no push de `func_0042A0C8`. Para o apanhar: logar TODO push/pop
com o ARENA empurrado (`base[idx]`) e o caller-lr, e casar push↔pop por arena para achar o push cujo
pop nunca vem — ou comparar a distribuição de caller-lr do pop `func_00262C64` consola vs nós (um
pop-caller que a consola tem e nós não). É um pop cross-fragment/early-return do lift (classe
2550C8/E129), mas NÃO em `func_0042A0C8`.

**Estado:** [D]/[E] não alcançado; registry vazio. Bug do crash natural CONFIRMADO nosso (scope
overflow, consola nunca transborda), refinado: `func_0042A0C8` é vítima (push correcto); a raiz é ~2–3
pops intermitentes em falta noutra função. 13 medições na continuação; o funil está em "qual pop de
~200 é saltado", um needle-hunt que precisa de casar push↔pop por arena (ciclo focado).

## §4ds — E353p: a máquina push/pop mapeada; o overflow é acumulação SISTÉMICA (~3), needle-hunt por arena

Segui o pop `0x41214` (que faltava após o push `0x41288` no run de overflow) até `func_000411EC`:
**pop `func_00262C64` (0x41210) ao INÍCIO + push `func_00262C8C` (0x41284) ao FIM** — "sai do scope
antigo, entra num novo", BALANCEADO por chamada (como `func_0042A0C8` mas com pop). Ou seja, cada
função da máquina de scope (`func_0042A0C8`, `func_000411EC`, e os pares 0x422Fxx/0x24xxxx/0x2Bxxxx do
trace) parece localmente balanceada; o overflow é uma **acumulação SISTÉMICA de ~3 scopes**, não um
único pop obviamente saltado.

**Disciplina (`walking-a-corruption-upstream`, "stopping at the first coherent story"):** já tenho
DUAS histórias coerentes (`func_0042A0C8` empurra; `func_000411EC` pop+push) e nenhuma é o defeito
isolado. Parar de ler função-a-função. O passo certo é a **análise de população**: logar TODO
push/pop com `(idx, arena=base[idx], caller-lr)`, casar push↔pop por arena (é LIFO), e achar o push
cujo pop nunca vem — OU contar pops por caller-lr consola vs nós e ver o défice de ~3. Precisa de
apanhar o run que transborda (intermitente) e de output completo (não só idx≥118).

**Estado (terminal desta continuação, honesto):** [D]/[E] não alcançado; registry vazio. O crash do
caminho natural está CONFIRMADO nosso (scope overflow, consola idx≤126) e a máquina push/pop está
mapeada (`func_00262C8C` push / `func_00262C64` pop; players `func_0042A0C8`, `func_000411EC`). O
defeito é uma acumulação sistémica de ~3 scopes, intermitente, cujo pop exacto exige o casamento
push↔pop por arena (ciclo analítico focado, não mais leituras isoladas). 14 medições na continuação;
o funil está na análise de população da máquina de scope.

## §4dt — E353q: os pop-sites da consola EXISTEM no nosso lift → divergência DINÂMICA (não lift-missing), ~3 no front-end

Histograma de callers do pop `func_00262C64` na consola (`oracle_pop_callers.py`, 800 hits): dominantes
`0x002C0C34 ×289`, `0x002C09CC ×289`, `0x002E0BF4 ×150`, depois caudas pequenas.

**Verificação estática (nosso lift):** TODOS esses pop-sites EXISTEM (`ctx->lr = 0xXXXX; func_00262C64`
em ppu_recomp_001/003). **Não é um pop apagado pelo lifter** (call-site presente).

**Logo o defeito é DINÂMICO:** um pop-site é ALCANÇADO na consola e não (ou menos vezes) em nós, por
**divergência de control-flow**, ~3 vezes, intermitente. NÃO é estático.

**Nota importante (retraction discipline — não fixar o frame cedo):** o net "idx ~3 alto" é **ambíguo**
entre (a) saltamos ~3 POPS e (b) fazemos ~3 PUSHES a mais. Ambos dão o mesmo overflow; o sítio do fix
difere. Distinguir exige contar o push de `func_0042A0C8` (e dos outros) nós vs consola — não só os pops.

**Fase:** os pops dominantes da consola (`0x2C0C34/0x2C09CC`, o par do level-loader) acontecem TARDE
(a consola progride 800+ pops); o nosso boot CRASHA cedo, no front-end. Logo o ~3 imbalance é no
**front-end** (logos/intro), antes do level-loader — não nos pops dominantes.

**Próximo (o fix, honesto sobre o esforço):** comparação temporal fina push+pop `func_00262C8C`/`C64`
com `(idx, arena, caller-lr)` consola-vs-nós NO MESMO ponto do front-end, para ver se é um push a mais
ou um pop a menos e em que função. É um ciclo analítico cuidadoso (os dois lados progridem diferente),
não uma leitura rápida.

**Estado (terminal, honesto):** [D]/[E] não alcançado; registry vazio. Confirmado: crash natural = bug
NOSSO (scope overflow, consola idx≤126); pop-sites presentes (dinâmico, não lift-missing); ~3 imbalance
no front-end (ambíguo push-extra vs pop-saltado). 15 medições na continuação. O funil está numa
comparação temporal fina da máquina de scope no front-end — ciclo focado, não mais leituras isoladas.

## §4du — E353r (⚠ a hipótese "load-completion" RETRACTADA por §4dw/E353t: é nesting tight, bug de lift) NÃO são pushes extra; as ~3 pops que deviam seguir NÃO disparam

> **RETRACTAÇÃO (E353t):** a hipótese "as pops são condicionais a load-completion → religa à stall de
> entrega" está provavelmente ERRADA. Medido: a scope de `func_0042A0C8` é popada em **3–13 eventos**
> por funções variadas — nesting TIGHT, não deferido por load. Logo o overflow é um **bug de lift
> (pop saltada)**, SEPARADO da stall de entrega. Ver §4dw. Sobrevive: não são pushes extra.

Histograma de push do NOSSO lado (run de overflow, sonda `SCOPE-PUSHALL`): `func_0042A0C8` (0x42A1A4)
pusha **x3** — **IGUAL à consola (x3, E353q-push)**. Os outros pushers também batem. **Não fazemos
pushes a mais.**

**Descoberta importante (o scope stack opera PROFUNDO):** só ~12 pushes no window logado, mas idx vai
118→130 — logo o scope stack opera normalmente a **idx ~118–127** (não perto de 0), com só ~8 slots de
folga até lim=128. Os runs limpos oscilam 124–127. Qualquer imbalance de ~3 transborda.

**A sequência do overflow (medida):**
```
push 0x41288 →128 ; pop 0x41214 →127 ; push 0x41288 →128
push 0x42A1A4 →129  (func_0042A0C8, SEM pop a seguir)
push 0x42A1A4 →130  (func_0042A0C8, SEM pop a seguir)
```
As 2 últimas pushes de `func_0042A0C8` **não têm pop a seguir** → overflow. Como o push count bate com
a consola, o defeito é que **as ~3 pops que deviam seguir as pushes de `func_0042A0C8` NÃO disparam**.

**Reframe (hipótese forte, a verificar):** essas pops são CONDICIONAIS — um scope popado na
**conclusão** de um load. Se `func_0042A0C8` empurra "início de load de recurso" e a pop é "load
concluído", então as ~3 pops em falta são ~3 **loads que não completam** — o que **religa o scope
overflow à STALL de entrega do R_PermA** (§4dg–§4dk). Ou seja, o scope overflow pode ser **SINTOMA da
mesma stall de loading**, não um bug de lift independente. A consola: os loads completam → pops disparam
→ idx≤126. Nós: loads stall → pops não disparam → overflow → crash (ANTES de o próprio R_PermA abrir).

**Alternativa (não excluída):** uma pop condicional saltada por control-flow do lift (não por load-stall).
Distinguir: ver o que a pop a seguir a `func_0042A0C8` na consola espera (bp na consola logo após o push
de `func_0042A0C8`, ver que pop dispara e sob que condição) vs nós.

**Estado:** [D]/[E] não alcançado; registry vazio. Refinamento decisivo: o scope overflow **não** é
pushes extra (func_0042A0C8 ours=3=consola); são ~3 pops (conclusões de load) que não disparam,
provavelmente **religando à stall de entrega do R_PermA** — o scope overflow e a stall de [C]/[D] podem
ser o MESMO root (loads não completam). 16 medições na continuação. Próximo: confirmar se a pop de
`func_0042A0C8` é condicional a load-completion (consola).

## §4dv — E353s: o scope stack corre a idx=126-127 POR DESIGN (margem de 1 slot); a consola gere-o exacto

Probe `oracle_a0c8_pop.py` (bp push 0x42A1A0 + pop 0x262C64): num window de 40 eventos, TODOS foram
**pops de `0x2C0C34`/`0x2C09CC`** (o par do level-loader), **todos a idx=127**. A consola oscila
**126↔127** via essa máquina de push/pop de alta frequência — a correr **encostada ao limite** (lim=128).

**Achado (medido):** o scope stack corre a **idx 126-127 POR DESIGN** (o jogo aninha scopes fundo); a
consola mantém-no com **~1 slot de margem** (max idx=127). Nós vamos a **130** (+3). A margem é tão fina
que qualquer imbalance transiente de ~3 transborda. **Corrige §4du/§4dq** ("consola idx≤126"): a consola
alcança 127 (durante loading); o max é 127, não 126.

**Limitação honesta deste probe:** não capturou a pop específica da scope de `func_0042A0C8` (o window
caiu na fase do level-loader `0x2C0Cxx`, que nós nunca alcançamos — crashamos antes, no front-end). Logo
a hipótese de §4du ("a pop de func_0042A0C8 é condicional a load-completion") **continua NÃO confirmada
nem refutada** — precisa de apanhar a fase de front-end da consola no MESMO ponto que nós, o que é
difícil (a consola passa depressa; nós crashamos cedo).

**Estado (terminal honesto desta continuação):** [D]/[E] não alcançado; registry vazio. O que está
SÓLIDO: o crash natural é overflow do scope stack (bug/sintoma NOSSO; consola nunca transborda, gere a
margem de 1 slot exacto); `func_0042A0C8` pusha x3=consola (não é push extra). O que fica EM ABERTO: se
os ~3 pops em falta são (a) load-completion não disparada (religa à stall de entrega) ou (b) uma pop
condicional saltada pelo lift no front-end. Distinguir precisa da consola no MESMO ponto de front-end —
um ciclo focado. 17 medições na continuação; margem de scope medida como razor-thin by design.

## §4dw — E353t: RESOLVE o fork (a)vs(b) — a scope de func_0042A0C8 é nesting TIGHT (3–13 eventos); é bug de LIFT, não load-completion

Probe `oracle_a0c8_life.py` (consola): após `func_0042A0C8` empurrar, seguir push/pop até a scope ser
popada:
```
PUSH idx=126 → popada após 13 eventos (lr=0x002E0BF4)
PUSH idx=125 → popada após  6 eventos (lr=0x00228708)
PUSH idx=125 → popada após  3 eventos (lr=0x0027B818)
```
A scope de `func_0042A0C8` vive **3–13 eventos** e é popada por funções VARIADAS (`0x2E0BF4`, `0x228708`,
`0x27B818`) — um **nesting de propósito geral TIGHT** (push/pop entrelaçados a idx 125–127), **NÃO**
deferido por um load.

**RESOLVE o fork de §4du (a)load-completion vs (b)lift:** é **(b)**. As pops NÃO são condicionais a
load-completion (acontecem em 3–13 eventos, não através de um load). Logo o scope overflow é um **bug de
control-flow do LIFT** (saltamos ~3 pops), **SEPARADO** da stall de entrega do R_PermA. **RETRACTA a
unificação de §4du** ("scope overflow = sintoma da stall").

**Consequência (boa notícia):** o crash do front-end é um **bug de lift directamente fixável** (pop(s)
saltada(s) por control-flow), da classe 2550C8/E129 — NÃO precisa de resolver a stall de entrega primeiro.
Fixá-lo pode **destravar o crash** e levar-nos ao loader do R_PermA (onde a stall de entrega seria o
próximo alvo, mas separado).

**Próximo (o fix do lift):** achar as ~3 pops que o nosso lift salta vs a consola. As pops da consola no
front-end vêm de `0x2E0BF4`, `0x228708`, `0x27B818`, `0x42AF50`, `0x2BA490`, `0x2BC258`; comparar quais
o nosso runtime alcança no MESMO ponto (uma delas tem um ramo/early-return no nosso lift que salta o
`bl func_00262C64`). É a auditoria de fallthrough/if-goto cross-fragment (classe 2550C8/E129) NAS funções
que popam scopes no front-end.

**Estado:** [D]/[E] não alcançado; registry vazio. Fork resolvido: o crash natural é **bug de lift
(pop saltada), separado da entrega, directamente fixável**. 18 medições; a unificação de §4du retractada.
Próximo concreto: auditar as funções popadoras do front-end (`0x2E0BF4`/`0x228708`/`0x42AF50`/…) por um
ramo que salta o pop.

## §4dx — E353u: lead concreto (func_0042AF18 salta o pop 0x42AF50) MAS ambíguo — modelo do scope stack incompleto

POPALL do nosso lado (histograma completo de `func_00262C64`): os poppers do front-end da consola
mostram **0 hits em nós**: `0x2E0BF4`=0, `0x228708`=0, **`0x42AF50`=0** (consola x20), `0x2BA490`=0,
`0x2BC258`=0; só `0x27B818`=2.

**Lead concreto:** `0x42AF50` está em `func_0042AF18`, que tem um **early-exit `if(r4==0)`** →
tail-call `func_0042B030` (SEM pop); o pop `func_00262C64` só corre no ramo `r4!=0`. A consola toma
`r4!=0` (x20 pops); nós 0. **Shape exacto de um pop saltado por ramo** (classe 2550C8/E129).

**MAS — anomalia que bloqueia a conclusão (retracting-findings):** o nosso run faz só **~12 pushes / 9
pops** antes de crashar, com idx JÁ a ~127. 12 pushes não explicam idx=127 → o scope stack é
**pré-inicializado a uma profundidade alta (~120+)** por um mecanismo que NÃO modelei (init/base scopes,
ou escrita directa a stk+4). Logo os "0 hits" nos poppers da consola são **AMBÍGUOS**: (a) saltamos
mesmo esses pops (bug), OU (b) crashamos cedo demais (após ~12 ops) e nunca alcançamos a fase da consola.
NÃO posso afirmar "saltamos o pop 0x42AF50" com esta data.

**O que falta (o modelo, não mais histogramas):** entender COMO idx chega a ~127 no início (init do
scope stack) e porque o nosso runtime só faz ~12 ops antes de crashar. Com PS3_TRACE_SCOPESTK a
profundidade inicial e a fase têm de ser medidas ANTES do window de overflow. Só então o "0 hits em
0x42AF50" fica interpretável (skip vs não-alcançado).

**Estado (terminal honesto):** [D]/[E] não alcançado; registry vazio. SÓLIDO: crash = overflow de scope
(bug de lift, nesting tight, não load-completion — E353t). LEAD: `func_0042AF18` salta o pop 0x42AF50 no
ramo r4==0; 0 hits em nós vs x20 consola. BLOQUEIO honesto: modelo do scope stack incompleto (idx
pré-inicializado a ~127; crash após ~12 ops) → o lead é ambíguo. 19 medições na continuação; o próximo
passo é MODELAR a init/profundidade do scope stack, não mais histogramas.

## §4dy — E353v: CORREÇÃO MAIOR — o modelo push/pop estava INVERTIDO; é UNDERFLOW (enters saltados), não overflow

Achado o 3.º manipulador do idx: **`func_00262CC4` (INIT)** — aloca 0x4000 e escreve:
`idx=0x80=128`, `lim=0x80=128`, `base=stk+0x14`. Ou seja **idx=128 = VAZIO** (topo).

**A semântica estava INVERTIDA (retracta E353k–E353u):**
- `func_00262C64` (idx--) = **ENTER** (entra num scope; idx desce de 128 para 127…), NÃO "pop".
- `func_00262C8C` (idx++) = **LEAVE** (sai; idx sobe para 128=vazio), NÃO "push".
- O "overflow para 130" é **UNDERFLOW** — idx passa de 128 (vazio): **fazemos mais LEAVES que ENTERS**,
  esvaziando a pilha. O provider `func_00262808` devolve base[idx] só se idx<lim(128); com idx≥128
  (vazio) → OOB → NULL → crash.

**RE-FRAME do lead (agora CORRECTO):** `0x42AF50` (em `func_0042AF18`) chama `func_00262C64` = um
**ENTER**. Dispara **x20 na consola, 0 em nós** (tomamos o ramo `r4==0` → tail-call `func_0042B030`,
SEM enter). Logo **saltamos ENTERS** que a consola faz → a pilha fica sub-populada (idx perto de
128=vazio) → os LEAVES seguintes (ex.: `func_0042A0C8`→`func_00262C8C`) empurram idx > 128 → underflow
→ NULL → crash. **`func_0042A0C8` NÃO é o culpado** (o seu LEAVE é correcto; a consola também o faz x3);
o culpado é o ENTER em falta.

**Consequência (retracta E353r/E353t/E353u no detalhe):** "missing pops", "func_0042A0C8 pusha x3",
"nesting tight" — a DIREÇÃO estava trocada. O correcto: **ENTERS em falta** (não pops). O overflow é
underflow. O lead `func_0042AF18` sobrevive mas invertido: é um ENTER saltado (`r4==0`), não um pop.

**Root (corrigido, medível):** `func_0042AF18` toma `r4==0` (salta o enter) em nós vs `r4!=0` (enter x20)
na consola. **Porque é `r4==0` em nós?** `r4` vem do caller de `func_0042AF18`. Próximo: walk upstream de
`r4` — quem chama `func_0042AF18` e com que `r4` (consola r4!=0, nós r4==0). É uma divergência de
DADOS/control-flow a montante do enter.

**Estado:** [D]/[E] não alcançado; registry vazio. Modelo do scope stack CORRIGIDO (idx=128=vazio;
C64=enter, C8C=leave; crash=underflow por enters em falta). Lead re-enquadrado: enter saltado em
`func_0042AF18` (`r4==0`). 20 medições na continuação; a correção do modelo inverte E353k–E353u mas
mantém a cadeia (crash = scope stack esvaziado por enters em falta). Próximo: origem de `r4==0`.

## §4dz — E353w: func_0042AF18 é RED HERRING (nunca chamado em nós); parar de perseguir funções individuais

Instrumentei `func_0042AF18` (`PS3_TRACE_AF18`): num run completo (8931 linhas, **sem crash de scope**,
ARENASTK-BAD=0), `func_0042AF18` foi **chamado 0 vezes**. Logo o "0 hits em 0x42AF50" NÃO é "tomamos
r4==0 dentro de `func_0042AF18`" — é que **nunca chamamos `func_0042AF18`** (não alcançamos esse caminho).

**CAI (retracta o lead de §4dx/§4dy):** `func_0042AF18` é **red herring** — uma função que a consola
chama (x20 enters) e nós não, mas NÃO é o sítio do fix; é vítima de algo a montante (o seu caller não é
alcançado). E o crash de scope é **intermitente** (este run não crashou).

**SOBREVIVE (sólido, medido):** (1) modelo do scope stack corrigido (idx=128=vazio; `func_00262C64`=ENTER,
`func_00262C8C`=LEAVE; crash=UNDERFLOW por enters em falta — §4dy); (2) o crash é NOSSO (a consola nunca
faz underflow); (3) é intermitente.

**Meta-lição (walking-a-corruption-upstream + retracting-findings):** persegui DOIS "sítios" (func_0042A0C8,
func_0042AF18) e ambos eram vítimas/red-herrings; o modelo estava invertido; o crash é intermitente. Isto
é o sinal de PARAR de instrumentar funções individuais no tail de uma sessão longa. O root (enters em
falta, sistémico/intermitente) precisa de uma abordagem limpa com o modelo corrigido — não mais um probe
por função.

**Estado (terminal honesto desta continuação):** [D]/[E] não alcançado; registry vazio. Entregue: modelo
do scope stack CORRIGIDO (underflow por enters em falta, não overflow por pushes), crash confirmado NOSSO
e intermitente, e DOIS leads de sítio refutados como red-herrings (func_0042A0C8, func_0042AF18). 21
medições na continuação; ~12 auto-correcções. O próximo passo é uma análise limpa dos enters em falta com
o modelo corrigido (que caller/fase a consola executa e nós não), num ciclo fresco — não mais probes por
função no tail.

## §4ea — E353x (⚠ "RACE" RETRACTADO por §4eb/E353y: thread única) a INTERMITÊNCIA recategoriza o bug

> **RETRACTAÇÃO (E353y):** "RACE entre threads" está ERRADO. Medido: TODAS as ops de scope no run que
> crashou vêm de UMA thread (tid=8420302208). Não é race de threads. A intermitência é single-thread
> mas TIMING-dependente (o caminho varia com o timing do intro/movie). Ver §4eb.

Síntese dos factos desta continuação:
- O crash de scope é **INTERMITENTE** (uns runs crasham no underflow, outros chegam ao menu — E353w).
- Os dois leads de sítio (`func_0042A0C8`, `func_0042AF18`) eram **red-herrings** (E353t/E353w).
- O modelo é **underflow por enters em falta** (E353v).

**Recategorização (a intermitência é o discriminador):** um bug ESTÁTICO de lift (ramo errado a saltar
um enter) falharia em TODOS os runs igual. Falhar só em ALGUNS runs ⇒ é uma **RACE**, não control-flow
estático. O scope stack é um global (`0x008370A8`); se múltiplas threads (front-end + FIOS + movie HLE)
o manipulam, um interleaving mau faz underflow (uma thread faz LEAVE de um scope que outra ainda não
fez ENTER, ou o idx++/idx-- corre sem exclusão mútua). **Isto explica os red-herrings:** não há UM
sítio — é um interleaving concorrente.

**Reconecta ao contexto de threading da sessão:** giant lock, preempção temporal (E229), FIOS scheduler,
task-lib SPU. O scope stack partilhado sem exclusão mútua (ou com a preempção a interromper um par
enter/leave) é a classe do problema. NÃO é um pop/enter saltado por lift.

**Próximo (recategorizado):** (1) confirmar se o scope stack é tocado por >1 thread (instrumentar o
thread-id em `func_00262C8C`/`func_00262C64`); (2) se sim, o fix é exclusão mútua no par enter/leave OU
não preemptar entre eles (relaciona-se com a preempção do giant lock E229). É um problema de
CONCORRÊNCIA, não de lift — o que explica porque 21 medições de control-flow não o pinaram.

**Estado (terminal honesto):** [D]/[E] não alcançado; registry vazio. Diagnóstico corrigido e
recategorizado: o crash do front-end é um **underflow do scope stack por RACE** (intermitente, threads
concorrentes no global `0x008370A8`), NÃO um bug estático de lift nem load-completion. 21 medições, ~12
auto-correcções, 2 red-herrings, 1 correcção de modelo (overflow→underflow), 1 recategorização
(lift→race). O próximo passo é a análise de concorrência (thread-id no enter/leave), não mais control-flow.

## §4eb — E353y: REFUTA a race — thread ÚNICA toca o scope stack; a intermitência é TIMING-dependente (intro)

Instrumentei o thread-id (`pthread_self`) em `func_00262C8C`/`func_00262C64`. Num run que CRASHOU
(ARENASTK-BAD=8), **as 21 ops de scope vêm de UMA thread** (`tid=8420302208`). **Não é race entre threads.**

**CAI (retracta §4ea/E353x):** "RACE no scope stack partilhado" — ERRADO. Uma só thread manipula o scope.

**SOBREVIVE + reconcilia:** o crash é single-thread MAS **intermitente** ⇒ **timing/data-dependente**: a
mesma thread toma sequências de enter/leave DIFERENTES em runs diferentes, conforme um input que varia.
O que varia entre runs: o **timing do intro/movie** (VideoToolbox async, `PS3_VDEC_FORCE_SEQDONE_MS`, a
ponte `PS3_MOVIE_VT_REPLAY`). Logo o underflow do scope depende de QUANDO/COMO a intro completa —
**reconecta (de novo) ao problema de completação da intro** (o contexto profundo da sessão, [A]/[G]).

**Convergência final do diagnóstico:** o crash do front-end (underflow do scope stack) é single-thread,
timing-dependente, e o timing é governado pela completação da intro/movie. NÃO é: race de threads (E353y),
bug estático de lift num sítio (red-herrings E353t/E353w), load-completion do R_PermA (E353t), nem overflow
por pushes (E353v). É um **sintoma downstream do timing de completação da intro**, que empurra o front-end
por um caminho de enter/leave que esvazia o scope stack.

**Estado (terminal, honesto e convergido):** [D]/[E] não alcançado; registry vazio. O crash natural =
underflow do scope stack single-thread, TIMING-dependente da completação da intro. Reconecta a [A]/[G]
(completação natural do intro / relógio de áudio do SCREAM). 22 medições, ~13 auto-correcções (incl.
overflow→underflow, lift→race→timing). O root accionável de TUDO (scope crash + entrega R_PermA + registry)
converge no MESMO ponto: a **completação natural da intro/front-end**, o problema mais profundo do projeto.

## §4ec — E353z: DESTRAVADO — o underflow do scope gateava a construção de nível; shells 5 → **18**

**Cadeia montada (medida, reproduzida 2×):**
1. `PS3_MOVIE_DONE_MS=9000` faz a intro **completar deterministicamente** (`st620 3→11→0`,
   `overlay_done=1`, `eos_ea=0x00869F1C` armado) — antes ficava presa em `st620 3→3`.
2. Com a intro completa, o **F2B entrega** (103–104 fills do R_PermA).
3. Sem mais nada, o boot batia no **underflow do scope** (24 marcadores FREELIST/ICALL) e a
   construção parava aos **5 shells** (baseline E292).
4. Com o **diagnóstico gated `PS3_SCOPE_CLAMP=1`** (impede o LEAVE de empurrar idx≥lim; disparou
   **1× só**: `idx=128 >= lim=128 -> clamp 127`): **shells 5 → 18**, **crash markers = 0**.

**RESULTADO MEDIDO:** `[TYPE15SHELL] post#` chega a **18** (era 5). **13 shells CGOWShader type-15 a
mais** construídos, sem crash, reproduzido em 2 runs. É o ponto mais avançado que o projeto alcançou na
construção de nível.

**O que isto PROVA:** o **underflow do scope stack era o gate da construção de nível**. Uma única
correcção do desequilíbrio enter/leave (aqui mascarada por um clamp) liberta a construção a continuar
(+13 shells). Confirma toda a cadeia diagnosticada: intro→entrega→scope→construção.

**Honestidade (regra 5 do projeto):** `PS3_SCOPE_CLAMP` é **DIAGNÓSTICO, gated OFF por defeito**,
declarado — **mascara** o desequilíbrio enter/leave, **não** o corrige. NÃO é aceite de conteúdo. O fix
real é o desequilíbrio (porque fazemos um LEAVE a mais). O clamp serve para **provar o gate** e medir o
payoff — e o payoff está medido: **+13 shells**.

**Ainda em falta para [D] completo:** 18/27 shells; arenas `WAD_R_*` = **1** (`WAD_R_Perm`) vs 13 na
consola; `LDRSH` = 0 (loader de shaders de nível ainda não corre). Ou seja: a construção avançou muito
mas ainda não completa o registry.

**Próximo:** (a) corrigir o desequilíbrio enter/leave a sério (substituir o clamp) — o gate está provado
e vale +13 shells; (b) ver o que trava dos 18 para os 27 e porque só 1 arena de 13 (provavelmente mais
entrega do R_PermA / mais records). Com o gate removido, a construção já progride — é a primeira vez.

**Estado:** [D] AVANÇOU de forma medida: **18 shells construídos (era 5), 0 crashes**, com a intro a
completar e o F2B a entregar. Registry ainda incompleto (falta 27 shells + 13 arenas + compiles), mas a
construção de nível **já não está travada** pelo scope. 24 medições na continuação.

## §4ed — E354: estado estável medido — 18 shells, R_PermA COMPLETO (20 MB), 0 crashes, plateau em st620=0

Corrida longa da cadeia de §4ec (intro forçada + F2B + ROOT2BP + SCOPE-CLAMP diagnóstico):

| Métrica | Antes (baseline E292) | AGORA | Consola |
|---|---|---|---|
| Shells CGOWShader (`post#`) | 5 | **18** | 27 |
| R_PermA entregue | 256 KB (parcial) | **20 169 344 / 20 169 344 (COMPLETO)** | completo |
| Crash markers | crash ao 6.º shell | **0** | 0 |
| Arenas `WAD_R_*` | 0 | 1 (`WAD_R_Perm`) | 13 |
| `LDRSH` (compiles de nível) | 0 | 0 | >0 |

**MEDIDO:** `[GATE-FORCE] R_PermA full flagged (bytes_read=20171600 size=20169344 preads=3091)` — o
R_PermA é **entregue por inteiro** pela 1.ª vez com a construção a correr. 18 shells construídos, **zero
crashes**, boot estável. Depois estabiliza: `MOVIEFSM st620 0→0` (front-end idle pós-intro), shells
param nos 18.

**O que está PROVADO:** a cadeia intro→entrega→scope→construção funciona ponta-a-ponta; o underflow do
scope era o gate (+13 shells ao removê-lo); a entrega completa dos 20 MB é alcançável.

**O que FALTA (honesto):** 18→27 shells, 1→13 arenas, e `LDRSH`=0 (os compiles de shaders de nível ainda
NÃO disparam). O front-end fica idle em `st620=0` depois dos 18 shells — falta o jogo progredir daí.
E o `SCOPE_CLAMP` continua **diagnóstico gated OFF** (mascara o desequilíbrio enter/leave; o fix real é
o desequilíbrio).

**Próximo:** (a) fix real do enter/leave (substituir o clamp — payoff já medido: +13 shells);
(b) porque o front-end fica idle em `st620=0` com 18 shells (o que espera para continuar a construção);
(c) as 12 arenas em falta.

**Estado:** [D] avançou de forma medida e reproduzida — **18 shells (era 5), R_PermA completo, 0 crashes**
— mas o registry ainda não está completo (27 shells + 13 arenas + compiles). É o ponto mais avançado do
projeto na construção de nível.

## §4ee — E355: o gap 18→27 é REGISTO DE NOMES — 80/80 lookups MISS (tabela vazia)

Com a cadeia de §4ec/§4ed a correr (intro completa + R_PermA completo + scope destravado, 18 shells),
medi os lookups de nome do loader (`func_002B2BA0` → `func_002AB23C`, sonda `PS3_TRACE_NAMELOOKUP`):

**MEDIDO — 80 lookups, 80 MISSES, ZERO hits:**
```
[WADLD-T1R] hit=0  x80        (hit=1: ZERO)
#1 Master_Server  #2 WadServer  #3 ProServer ... #25 EpiServer   (os SERVERS)
#28 WAD_R_Perm #29 GFXX_R_Perm #30 TXRX_R_Perm ... #38 MDLX_R_Perm  (os RECURSOS R_Perm)
TXR_speck2Chest, TXR_nsGlowTexture2Ch, ...                        (as TEXTURAS)
```
```
[NAMELOOKUP] name='Master_Server' AB23C_result=0x00000000 mgr(*r2-0x14A0)=0x00868D48
             mgr+0x58=0x4389BE50  mgr+0x5C=0x00000000
```

**Diagnóstico:** o **manager existe** (`0x00868D48`) e o seu contentor `+0x58` é povoado
(`0x4389BE50` a partir do 3.º lookup) — mas `func_002AB23C` (o lookup na tabela) devolve **0 sempre**.
A **tabela de nomes está VAZIA de entradas**: nem os SERVERS nem os recursos `*_R_Perm` nem as texturas
alguma vez são **REGISTADOS**.

**É este o gap para o registry completo:** a construção de nível procura os servers para lhes ATTACHAR
recursos; como nenhum nome resolve, não há attach → **arenas ficam em 1/13**, os shells param nos 18/27,
e `LDRSH`=0 (nenhum compile de nível dispara). Bate certo com E263 ("`func_002ACBE8` é LOOKUP por chave;
devolve 0 = chave NÃO registada") — agora quantificado: **80/80**.

**Próximo (cirúrgico):** achar o **REGISTO** (a contraparte de `func_002AB23C` que insere na tabela em
`mgr+0x58`) e porque nunca corre — os servers deviam ser criados/registados antes da passagem de attach.
Comparar com a consola (oráculo headless: bp no register, ver quantas entradas tem a tabela quando o
loader faz o 1.º lookup).

**Estado:** [D] com avanço real (18 shells, R_PermA completo, 0 crashes — §4ec/§4ed) e o gap restante
**localizado com precisão**: o registo de nomes nunca acontece (80/80 misses). Não é entrega, não é
scope, não é crash — é **registo**.

## §4ef — E356 (⚠ "3 inserts / registo não corre" RETRACTADO por §4eg/E357: `func_002ACD38` é o ramo de ÁRVORE VAZIA, não um insert) 4600 lookups medidos

> **RETRACTAÇÃO (E357):** `func_002ACD38` NÃO é o insert. Em `func_002ACBE8`: `r31=*(r3+0)` (raiz da
> árvore) e o salto para `func_002ACD38` só acontece se **`r31==0` (árvore VAZIA)**. Logo os "3 inserts"
> são 3 lookups com árvore vazia; os outros **4597 percorreram uma árvore POVOADA e mesmo assim
> falharam**. A tabela NÃO está vazia — as CHAVES é que não batem. Ver §4eg.

Sondas `PS3_TRACE_KEYREG` no par da tabela de chaves (lookup `func_002ACBE8` / insert `func_002ACD38`),
com a cadeia de §4ec a correr (intro completa, R_PermA completo, scope destravado, 18 shells):

| Operação | Contagem |
|---|---|
| **LOOKUP** (`func_002ACBE8`) | **4600** |
| **INSERT** (`func_002ACD38`) | **3** |
| Lookups de NOME (`WADLD-T1R`) | 80 — **hits: 0** |

**Diagnóstico definitivo:** o jogo faz **4600 lookups** contra uma tabela que recebeu **3 inserts**. O
**REGISTO praticamente não corre**. Por isso:
- os 80 lookups de nome (servers `Master_Server`/`WadServer`/…, recursos `*_R_Perm`, texturas `TXR_*`)
  dão **todos MISS**;
- sem resolver nomes não há **attach** → arenas ficam em **1/13**;
- a construção pára nos **18/27 shells**;
- **`LDRSH`=0** — nenhum compile de shader de nível dispara.

**Cadeia do lookup (mapeada):** `func_002B2BA0` (lookup por nome) → `func_002AB23C` → `func_002AB1AC`
→ **`func_002ACBE8`** (lookup por chave, E263). Insert correspondente: **`func_002ACD38`** (E353e).

**Próximo (o alvo, agora quantificado):** achar QUEM devia chamar `func_002ACD38` centenas de vezes
(o registo dos servers/recursos) e porque só corre 3×. Comparar com a consola (oráculo headless: bp em
`func_002ACD38`, contar inserts até ao 1.º lookup de nome — a consola deve ter centenas). É o **último
elo** entre a construção (já a correr, 18 shells) e o registry completo + compiles.

**Estado:** [D] com a construção destravada e a correr (18 shells, R_PermA completo, 0 crashes) e o
bloqueio final **quantificado com precisão**: **3 inserts vs 4600 lookups** — o registo não corre.
Não é entrega, não é scope, não é crash: é **REGISTO**.

## §4eg — E357 (⚠ hipótese "hash errado" REFUTADA por §4eh/E358: hash é 8/8 correcto) a tabela está POVOADA; falham as CHAVES

> **REFUTAÇÃO (E358):** a hipótese "a chave/hash não bate" está ERRADA. O algoritmo liftado
> (`func_002AAAF0` uppercase + `func_002AAA6C` hash) reproduz **8/8** as chaves da consola. A
> derivação da chave está CORRECTA. Ver §4eh.

**Leitura correcta de `func_002ACBE8`** (o lookup por chave):
```
r31 = *(r3 + 0)            ; raiz da árvore/tabela
if (r31 == 0) -> func_002ACD38   ; ramo de ÁRVORE VAZIA (NÃO é insert)
else            ; percorre a árvore com a chave r4
```
Logo, das **4600** chamadas, só **3** encontraram a árvore vazia; **4597 percorreram uma árvore
POVOADA** e devolveram 0 → **a tabela TEM entradas; as CHAVES é que não correspondem**.

**RETRACTA §4ef/E356:** "3 inserts, o registo não corre" está ERRADO (rotulei o ramo de árvore-vazia
como insert). O registo ACONTECE; o que falha é a **correspondência de chave**.

**Nova hipótese (forte, testável):** a **chave/hash derivada do nome** não bate com a que foi registada.
Cadeia: `func_002AB1AC` chama `func_002AAA6C` (deriva a chave do nome) e passa-a em `r4` a
`func_002ACBE8`. Medido nas sondas: `r4` é por vezes um hash (`0xB2E534BE`, `0xD5122DE0`). Se o nosso
hash de string estiver mal liftado (ou a derivação da chave diferir), **TODOS** os lookups falham numa
tabela povoada — exactamente o que se observa (80/80 nomes MISS, 4597 misses em árvore povoada).

**Próximo (decisivo):** registar, do nosso lado, `nome → chave(r4)` para os nomes conhecidos
(`Master_Server`, `WAD_R_Perm`, …) e comparar com a consola (oráculo headless: bp em `func_002ACBE8`,
capturar `r4` na mesma sequência de lookups). Se as chaves divergirem para o mesmo nome → **bug de
hash/derivação de chave**, e é esse o último elo para o registry + compiles.

**Estado:** construção destravada e a correr (18 shells, R_PermA completo, 0 crashes); o último elo
re-caracterizado: **não é o registo que falta — é a CHAVE que não bate** numa tabela povoada.

## §4eh — E358: o HASH está CORRECTO (8/8 vs consola) — refuta o "hash errado"; os nomes é que não estão NAQUELA tabela

Decodifiquei o algoritmo do lift e testei-o **offline** contra chaves medidas na consola
(oráculo `oracle_keyhash.py`, bp em `func_002AB1AC` entry + no call a `func_002ACBE8`):

```
func_002AAAF0(dst,src): uppercase ASCII, máx 0x18 (24) chars
func_002AAA6C(str,h=0): h = ((ROTL32(h,7) & 0xFFFFFF80) - h) + c   por char
```

| nome | consola | modelo do nosso lift | |
|---|---|---|---|
| WAD_R_LglScA | 0x6042C212 | 0x6042C212 | ✅ |
| GFXX_R_LglScA | 0x6FECA1ED | 0x6FECA1ED | ✅ |
| MATX_R_LglScA | 0xE0473674 | 0xE0473674 | ✅ |
| MDLX_R_LglScA | 0xB5A42EE9 | 0xB5A42EE9 | ✅ |
| … | | | **8/8** |

Auto-verificação com as chaves do NOSSO runtime (`Master_Server`=0xD5122DE0, `WadServer`=0xF25A08DD,
`ProServer`=0xD1D93F50, `GOServer`=0xDFC4188B): **4/4** iguais ao modelo.

**REFUTA §4eg/E357:** a derivação da chave está **CORRECTA**. Não é bug de hash.

**Estado do funil (o que sobra, por eliminação medida):**
- entrega do R_PermA: ✅ completa (20 MB);
- crash/scope: ✅ destravado (18 shells, 0 crashes);
- registo/tabela: **POVOADA** (4597 lookups percorrem árvore não-vazia);
- chave/hash: ✅ **CORRECTA** (8/8 vs consola);
- ⇒ **os NOMES que procuramos (`Master_Server`, `WAD_R_Perm`, …) não estão NAQUELA tabela**:
  ou nunca foram registados, ou foram registados noutra INSTÂNCIA de tabela (a nossa procura em
  `tbl+4=0x43407794`).

**Próximo (cirúrgico, agora muito estreito):** descobrir em que tabela/instância os servers são
registados vs a que o loader consulta (`0x43407794`) — comparar o endereço da tabela na consola no
mesmo lookup. Se divergirem, é uma **colisão/duplicação de instância** (a mesma classe do
arena↔tabela de §4bh–§4bm).

**Estado:** [D] com construção destravada (18 shells, R_PermA completo, 0 crashes) e o último elo
reduzido por eliminação a **"os nomes não estão na tabela consultada"** — hash correcto, tabela
povoada, entrega completa.

## §4ei — E359 (⚠ CONCLUSÃO REFUTADA por §4ej/E360: a consola TAMBÉM falha os lookups) os nomes vêm de uma tabela estática

> **REFUTAÇÃO (E360):** a conclusão "os servers nunca são registados ⇒ é esse o bloqueio" está ERRADA.
> Medido no oráculo, no MESMO sítio (`func_002AB1AC`/retorno de `func_002ACBE8`): a **consola também
> devolve `node=0` em 20/20** lookups. **Os MISSES são NORMAIS** — o lookup é um "isto já existe?"
> que legitimamente falha para recursos novos. Sobrevive: a tabela estática de nomes e o caminho
> uppercase→hash→lookup. Ver §4ej.

- `Master_Server` está em **vaddr 0x004CB980** (PT_LOAD off=0 → vaddr=0x10000; file 0x4BB980).
- **Nenhuma referência directa** a esse endereço no lift: os nomes são alcançados por uma
  **tabela/array de ponteiros** que o loader **percorre** — daí a sequência determinística
  `Master_Server, WadServer, ProServer, GOServer, AnimServer, …` (25 servers) seguida dos
  recursos `*_R_Perm` e das texturas `TXR_*`.
- No lookup, o nome é **copiado/uppercased para um buffer de stack** (`namep=0x0FEFF408`), o que
  confirma o caminho `func_002AAAF0` (uppercase) → `func_002AAA6C` (hash) → `func_002ACBE8`.

**Conclusão (por eliminação medida):** o loader percorre a lista de servers ESPERADOS e procura-os;
**todos falham** (80/80). Como a tabela está povoada e o hash está correcto (§4eh, 8/8), a única
leitura consistente é: **os servers nunca foram criados/registados** nesta tabela antes da passagem
de attach. Sem servers, não há a quem attachar recursos → arenas 1/13, shells 18/27, `LDRSH`=0.

**Funil final (tudo o resto eliminado por medição):**
| elo | estado |
|---|---|
| entrega R_PermA | ✅ completa (20 MB) |
| crash / scope underflow | ✅ destravado (18 shells, 0 crashes) |
| tabela de nomes | ✅ povoada (4597 lookups em árvore não-vazia) |
| hash/chave | ✅ correcta (8/8 vs consola) |
| **criação/registo dos SERVERS** | ❌ **não acontece** ← último elo |

**Próximo:** achar a fase que **cria e regista os servers** (`Master_Server`…) — na consola corre antes
do attach; em nós não. Oráculo: bp no lookup e ver que a consola já tem os servers registados quando
percorre a mesma lista (ou bp na criação do primeiro server).

**Estado:** [D] com construção destravada (18 shells, R_PermA completo, 0 crashes) e o último elo
isolado por eliminação: **os servers nunca são registados**. Não é entrega, não é scope, não é hash,
não é tabela vazia.

## §4ej — E360: a CONSOLA também falha os lookups (0 hits / 20 misses) — os misses são NORMAIS

Probe `oracle_lookup_hit.py` (bp em `func_002AB1AC` entry para o nome + no retorno de `func_002ACBE8`
em `0x002AB1F4` para o resultado), consola `EBOOT_intro.BIN`:
```
#1  WAD_R_LglScA   node=0x00000000 MISS
#11 MDLX_R_LglScA  node=0x00000000 MISS
#20 GFX_SCREEN_00  node=0x00000000 MISS
DONE hits=0 misses=20
```

**A consola tem exactamente o mesmo padrão que nós: 0 hits.** Logo **o MISS é o comportamento normal**
deste lookup (é um "já existe?" antes de criar), **não** um sintoma de registo em falta.

**REFUTA §4ei/E359** ("os servers nunca são registados ⇒ bloqueio") e, com ele, toda a linha de
raciocínio que tratava os 80/80 misses como o defeito (§4ee/§4ef iniciais). **Os 80 misses não são bug.**

**SOBREVIVE (medido e útil):**
- o **avanço real**: 18 shells (era 5), **R_PermA completo (20 MB)**, **0 crashes** (§4ec/§4ed);
- o **hash está correcto** (8/8 vs consola, §4eh) — verificação offline reutilizável;
- o scope underflow era o gate da construção (§4ec) — payoff medido +13 shells;
- a tabela de nomes e o caminho uppercase→hash→lookup estão mapeados e correctos.

**CAI:** "registo/lookup é o bloqueio" (E355–E359). O plateau nos 18 shells e `LDRSH`=0 têm **outra**
causa, ainda por determinar.

**Próximo (honesto, re-aberto):** voltar ao plateau com o oráculo no MESMO ponto: a consola, depois de
entregar o R_PermA, constrói mais shells (27) — comparar o que ela faz a seguir ao 18.º shell que nós
não fazemos (bp no construct type-15 e ver a sequência), em vez de assumir o lookup.

**Estado:** [D] com avanço real e reproduzido (18/27 shells, R_PermA completo, 0 crashes); a causa do
plateau **volta a estar em aberto** — os misses de nome estão ILIBADOS pela consola. 16.ª correcção
medida da sessão.

## §4ek — E361: a consola faz **397+** constructs type-15 (contínuo); nós fazemos 18 e paramos

Probe `oracle_type15.py` (bp `func_0039E794` = handler type-15/CGOWShader, E292; e `func_002B0DB0` =
dispatcher do registry), consola `EBOOT_intro.BIN`, ~65 s:
```
TYPE15 construct #335 (r3=0x300D78D0) ... #395 (r3=0x300D78D0)
DONE TYPE15=397  DISPATCH=3        (nós: 18 shells, depois plateau)
```

**MEDIDO:** a consola chama o handler type-15 **397+ vezes** e continua (capei em 400 hits), reciclando
um punhado de objectos (`0x300D78D0`, `0x300C6210`, `0x30300E80`, `0x300C5048`). O dispatcher do registry
(`func_002B0DB0`) corre só **3×**.

**Leitura:** o trabalho type-15 na consola é uma **actividade CONTÍNUA e viva** (provavelmente bind/uso
de shaders por frame), não uma construção one-shot de 27. Nós fazemos uma **rajada de 18 e paramos**
(front-end fica idle em `st620=0`, §4ed).

**Consequência para o alvo:** a métrica "27 shells" (da consola in-level) não é directamente comparável
com este contador; o que a comparação mostra sem ambiguidade é que **a consola CONTINUA a trabalhar e
nós paramos**. O plateau é real e é o alvo: o nosso front-end não entra no estado "vivo" onde a consola
constrói/usa shaders continuamente.

**Próximo:** perceber porque o nosso front-end fica idle em `st620=0` depois da rajada de 18 (o que a
consola executa a seguir que nós não) — é o mesmo plateau de §4ed, agora com um contador de referência
da consola (397+ vs 18) para medir qualquer progresso.

**Estado:** [D] com avanço real e reproduzido (18 shells, R_PermA completo, 0 crashes) e o plateau
caracterizado por comparação directa: **consola 397+ contínuo vs nós 18 e idle**. Os misses de nome
estão ilibados (§4ej); a causa do plateau é o front-end não entrar no estado vivo.

## §4el — E362: divergência de SEQUÊNCIA — abrimos `r_lglsca.wad_ps3` mas NUNCA processamos recursos `_R_LglScA`

**MEDIDO (nosso run, `clamp3.log`):**
```
open '/wad/r_lglsca.wad_ps3'     <- ABERTO
open '/wad/r_perma.wad_ps3'      <- ABERTO
lookups por sufixo de WAD:  57x '_R_Perm'   |   0x '_R_LglScA'
```
**Consola (oráculo, mesma janela):** 19+ lookups `_R_LglScA` (`WAD_R_LglScA`, `GFXX_R_LglScA`,
`MDLX_R_LglScA`, … `WYPX_R_LglScA`) — **é a fase do ECRÃ LEGAL**.

**Divergência:** a consola processa os recursos do **R_LglScA** (ecrã legal) nesta fase; nós **abrimos
o ficheiro e nunca processamos nenhum** — saltamos directamente para os recursos do **R_Perm** (57
lookups). O F2B prende-se a um ficheiro de cada vez (`g_f2b_fill_mfd`, com o "rearm" de E316 quando o
FO é reutilizado para o R_PermA) e no nosso run acaba no **R_PermA** (file_pos=20169344/20169344).

**Hipótese (LEAD, não provada):** ao forçar a intro (`PS3_MOVIE_DONE_MS`) e ao entregar o R_PermA por
F2B, **saltamos a fase do ecrã legal**; a FSM do front-end esperaria legal→título→menu e fica idle em
`st620=0` depois da rajada de 18 shells. Isto explicaria o plateau — mas **não está provado**; é preciso
medir se processar o R_LglScA faz o front-end avançar (a lição desta sessão: não assumir a causa).

**Como testar:** (a) ver se o loader chega a PEDIR recursos `_R_LglScA` e o F2B lhe entrega o WAD errado
(entrega R_PermA quando o pedido é LglScA); (b) deixar a sequência natural do LglScA correr antes de
armar o R_PermA, e medir se os shells passam dos 18 e se `LDRSH` dispara.

**Estado:** [D] com avanço real (18 shells, R_PermA completo, 0 crashes); plateau caracterizado
(consola 397+ constructs contínuos vs nós 18 e idle, §4ek) e agora com um **lead concreto e medido**:
saltamos toda a fase de recursos do ecrã legal que a consola executa.

## §4em — E363 (⚠ "TRUNCADO" RETRACTADO por §4en/E364: o psarc tem os MESMOS 3 072 B) `R_LglScA.wad_ps3` tem 3 072 B

> **RETRACTAÇÃO (E364):** o ficheiro **NÃO está truncado**. Extraí `/wad/r_lglsca.wad_ps3` do próprio
> `gow2.psarc`: **3 072 bytes, byte-a-byte idêntico** ao do cache. Logo 3 KB **é** o tamanho real e
> `0x0007A120` em +0x20 **não é** um campo de tamanho de ficheiro. Ver §4en.

Testei o lead de §4el (timing da intro): dar 25 s ao ecrã legal **não muda nada** (`_R_LglScA` lookups
continuam **0**, shells 18, `LDRSH`=0) ⇒ **§4el refutado como causa** (não é timing).

A causa real é **dados**:
```
[movieio] open '/wad/r_lglsca.wad_ps3' -> movie_cache/r_lglsca.wad_ps3  size=3072
[movieio] open '/wad/r_perma.wad_ps3'  -> movie_cache/r_perma.wad_ps3   size=20169344
```
| WAD (cache) | filesize | campo +0x20 |
|---|---|---|
| `R_PermA.wad_ps3` | 20 169 344 | `0xFFFFFFFF` (sentinela de container — correcto) |
| `R_LglScA.wad_ps3` | **3 072** | `0x0007A120` = **500 000** |

O header diz `WAD_R_LglScA` e declara ~**500 000 B**, mas o ficheiro tem **3 072 B** e termina a meio
de uma estrutura (fim exacto em 0xC00). Datas: `R_LglScA`/`R_PermA` são de **20 jul**; os restantes WADs
do cache (r_colsus00 7,3 M, r_hero01 3,4 M, …) são de **2 set** e têm tamanhos plausíveis. Ou seja, o
`R_LglScA` ficou de uma extracção antiga e **incompleta**.

**Consequência:** o F2B entrega os 3 072 B, o loader não encontra recursos para parsear, **0 lookups
`_R_LglScA`**, a fase do ecrã legal não produz nada, e saltamos para o R_Perm — o que bate certo com
§4el (abrimos o ficheiro mas nunca processamos recursos dele).

**Fix (dados, não código):** re-extrair `r_lglsca.wad_ps3` do `gow2.psarc` (7 GB) para o `movie_cache`
com o tamanho real (~500 KB) e repetir a medição: esperado ⇒ aparecem lookups `_R_LglScA`, a fase do
ecrã legal processa, e mede-se se os shells passam dos 18 e se `LDRSH` dispara.

**Estado:** [D] com avanço real (18 shells, R_PermA completo, 0 crashes) e o plateau com uma **causa
concreta e verificável**: o WAD do ecrã legal está truncado no cache. §4el (timing) refutado por teste.

## §4en — E364: o `R_LglScA` NÃO está truncado (psarc = 3 072 B idênticos); nós ENTREGAMOS mas não PARSEAMOS

Extraí o ficheiro do próprio arquivo do jogo:
```
psarc_extract.py gow2.psarc /wad/r_lglsca.wad_ps3 -> 3072 bytes
cmp vs movie_cache/R_LglScA.wad_ps3 -> IDÊNTICOS (byte-a-byte)
```
**REFUTA §4em/E363:** o WAD do ecrã legal tem mesmo **3 072 B**; não há truncagem, o cache está correcto,
e `+0x20=0x0007A120` não é tamanho de ficheiro (é outro campo). 18.ª correcção medida da sessão.

**Pergunta refinada (o que sobra, medido):** o ficheiro de 3 KB **É entregue por completo** pelo F2B
(`file_pos=3072/3072`) e contém registos (`GroupSta…`, `GFX_SCREEN_00` visíveis no dump) — mas nós
fazemos **0 lookups `_R_LglScA`**, enquanto a consola faz **19** a partir do mesmo ficheiro de 3 KB.
Ou seja: **entregamos os dados e não os parseamos**; com o R_Perm entregamos E parseamos (57 lookups).

**Próximo:** perceber porque o loader não parseia o conteúdo do LglScA que lhe entregamos (estado do
loader nessa fase, ou a entrega chega antes de ele estar pronto), comparando com a consola no mesmo
ponto — em vez de assumir (foi assim que caíram §4el timing e §4em truncagem).

**Estado:** [D] com avanço real (18 shells, R_PermA completo, 0 crashes); o plateau continua sem causa
provada — mas o funil está mais limpo: **não é timing (§4el), não é ficheiro truncado (§4en), não é
registo/lookup (§4ej), não é hash (§4eh)**. É o loader não parsear o LglScA que lhe é entregue.

## §4eo — E365: o op cancelado é um SINGLETON REUSADO — `+44="SmLo"` é LIXO ANTIGO; `+4` aponta ao fo do WAD

Sequência medida (`lgl.log` 8060–8092), na transição LglScA→PermA:
```
[movieio] open r_lglsca.wad_ps3 size=3072
[E275]    WAD-HOST-READ fo=0x4301C178 sz=3072
[FIOSOPEN] F2B-STREAM-PUMP  pumped=3072/3072
[FIOSOPEN] F2B-STREAM-FILL  avail=3072 file_pos=3072/3072     <- LglScA ENTREGUE ao ring
[FIOSOPEN] 42B4-CANCEL-YIELD after DONE early cancel           <- CANCEL
[movieio] open r_perma.wad_ps3 size=20169344                   <- MESMO fo 0x4301C178 reusado
[FIOSOPEN] F2B-STREAM-ENSURE was_av=0 ... file_pos=3072/3072   <- avail=0: LglScA DESCARTADO
```
Dump do op cancelado (`PS3_DUMP_CANCEL_OP`):
```
[CANCEL-OP] op=0x00869E04 +0=0x005173A0 +4=0x4301C178 +8=0x4300D110 +C=0x00010020 +44=0x536D4C6F
```
- **`op=0x00869E04` é um endereço FIXO** ⇒ o op é um **singleton REUSADO** por pedidos sucessivos
  (SmLogo → WAD LglScA → WAD PermA).
- **`+4 = 0x4301C178` é exactamente o `fo` do WAD** (`[E275] WAD-HOST-READ fo=0x4301C178`).
- **`+44 = "SmLo"` é LIXO ANTIGO** (tag de um uso anterior do mesmo op), não a identidade do pedido corrente.

**REVISÃO de §4dk/E353h:** ali concluí "o cancel é do SmLogo, não do R_PermA" **com base no `+44`**.
Como o op é um singleton reusado, o `+44` estava **obsoleto**; o campo que identifica o pedido corrente
é o `+4` (o `fo`), e esse aponta ao **WAD**. Ou seja: o DONE early-cancel dispara sobre o op que está a
**servir a leitura do WAD**, logo a seguir à entrega do LglScA.

**Mecanismo (coerente com todos os factos):** LglScA é entregue ao ring (avail=3072) → o DONE-poll
**cancela** o op do WAD → o `fo` é reusado para o R_PermA → `avail` volta a 0 → **os 3 KB do LglScA são
descartados antes de o loader os parsear** ⇒ **0 lookups `_R_LglScA`** (a consola faz 19).

**Próximo:** impedir que o cancel descarte um WAD já entregue mas ainda não parseado (ou não reusar o
`fo` antes do parse) e medir: esperam-se lookups `_R_LglScA`, a fase do ecrã legal a processar, e depois
verificar shells>18 / `LDRSH`>0.

**Estado:** [D] com avanço real (18 shells, R_PermA completo, 0 crashes) e o plateau com **mecanismo
medido**: o early-cancel + reuso do `fo` descartam o LglScA entregue. Corrige a identificação de E353h
(op singleton, `+44` obsoleto).

## §4ep — E366: o F2B-HOLD NÃO disparou — o remap do `fo` não é o ponto de descarte

Implementei o diagnóstico gated `PS3_F2B_HOLD` (adiar o remap `fo`→novo WAD enquanto o ring ainda tem
bytes por consumir) para testar a hipótese de §4eo. Resultado:
```
F2B-HOLD fired: 0     _R_LglScA lookups: 0     shells: 18     LDRSH: 0     crashes: 0
```
**Disparou 0 vezes** ⇒ no instante do remap para o R_PermA, `avail` **já era 0** (ou o `fo` não era o do
fill). Logo **o remap NÃO é o ponto onde os 3 KB se perdem**.

**CORRIGE §4eo/E365:** a formulação "o reuso do `fo` descarta o LglScA entregue" **não se sustenta**.
Entre o `F2B-STREAM-FILL avail=3072` e o remap, o `avail` já desceu a 0 — ou seja, os 3 KB foram
**consumidos** (lidos pelo loader) ou repostos por outra via, **antes** do remap. Sobrevive de §4eo: o op
é um singleton reusado e o `+44="SmLo"` é lixo antigo (isso mantém-se, e corrige E353h).

**Pergunta refinada (a distinguir):** o loader **consumiu** os 3 KB do LglScA mas **não produziu
lookups** (parse que não gera recursos), em vez de os ter perdido. Distinguir "consumido-mas-não-parseado"
de "reposto/descartado" exige seguir quem baixa o `avail` de 3072→0 no intervalo (consumo do loader vs
reset), e comparar com a consola, que do MESMO ficheiro de 3 KB produz 19 lookups.

**Estado:** [D] com avanço real (18 shells, R_PermA completo, 0 crashes). O plateau continua sem causa
provada; eliminados por teste: timing (§4el), ficheiro truncado (§4en), registo/lookup (§4ej), hash
(§4eh), e agora o remap do `fo` (§4ep). 19.ª correcção medida da sessão.

## §4eq — E367: o diferenciador do LglScA é o ESTADO DO LOADER — consola `state=2`, nós `state=1`

Probe `oracle_lgl_state.py` (bp `func_002AB1AC` = lookup por nome; lê o nome em `r4` e o estado do
loader em `type_sys` via `*0x0053FF4C`), consola `EBOOT_intro.BIN`, durante a fase do ecrã legal:
```
#1  WAD_R_LglScA   state=2 rem=0x00000000 body1D4=0x00000000 avail=0xB50
#2  GFXX_R_LglScA  state=2 rem=0x00000000 body1D4=0x00000000 avail=0xAE0
...
#14 EEPR_R_LglScA  state=2 rem=0x00000000 body1D4=0x00000000 avail=0x430
```
**NÓS na mesma fase:** `WADLD-SM state=1 rem=0x0 body1D4=0x0`, **0 lookups `_R_LglScA`**.

**Comparação (o que é igual vs o que difere):**
| campo | consola | nós | conclusão |
|---|---|---|---|
| `rem` (ts+0x1AC) | **0** | **0** | IGUAL — não é o defeito |
| `body1D4` (ts+0x1D4) | **0** | **0** | IGUAL — não é o defeito (re-confirma E343) |
| `avail` do ring | **desce** 0xB50→0x430 (consome record a record) | não consome | difere |
| **`state` (ts+0x1CC)** | **2** | **1** | ← **O DIFERENCIADOR** |

**Achado:** para processar os records do LglScA o loader tem de estar em **state=2**; o nosso fica em
**state=1** e por isso nunca percorre o ring nem gera lookups. `rem=0`/`body1D4=0` são **normais** nesta
fase (a consola tem-nos iguais) — o que reforça que as leituras antigas que os tratavam como defeito
estavam erradas.

**Próximo (cirúrgico e estreito):** achar a transição **state 1 → 2** do loader (quem escreve
`ts+0x1CC=2`) e porque não dispara em nós na fase do LglScA. É o mesmo campo cuja transição 2→3 já foi
estudada (`func_002BA9BC` é o setter de state=3); falta o setter/condição de **state=2**.

**Estado:** [D] com avanço real (18 shells, R_PermA completo, 0 crashes) e o gap do LglScA reduzido a
**um único campo**: `state` 1 (nós) vs 2 (consola). Eliminados por teste: timing, truncagem, registo/
lookup, hash, remap do fo, e agora também rem/body1D4 (iguais na consola).

## §4er — E368: o gate do state=2 PASSA, mas só 1× e para o R_PermA; o LglScA nunca entra no loader

Localizei o setter de `state=2`: **`func_002BA990`**, guardado por
```c
r3 = func_002B4224(ts + 0x64);          // readiness
if ((r3 & 0xFF) == 0) -> func_002BA824  // BAIL (fica em state=1)
rem(ts+0x1AC) = *(ts+0x74);  state(ts+0x1CC) = 2;  *(ts+0x1D0) = *(ts+0x74);
```
Sonda `PS3_TRACE_ST2GATE` no nosso runtime:
```
[ST2GATE] #1 ready(B4224)=0x1 -> SET state=2  ts=0x00869570  ts+0x74=0x133C280  ring_av=0x40000
count=1   SET=1   BAIL=0
```
**O gate PASSA** (não é um bail!) — mas dispara **uma única vez** e com `ts+0x74 = 0x133C280` = **20 MB
= R_PermA**. Ou seja, a nossa única transição para state=2 é a do **R_PermA**.

**Consola (E367), na fase do ecrã legal:** `state=2` com **`rem=0`** e `avail` a descer (0xB50→0x430),
isto é, `ts+0x74 = 0` — o LglScA é processado em **modo "inline"** (sem stream: consome directamente o
que está no ring), enquanto o R_PermA é um **container em stream** (`rem = 20 MB`).

**Conclusão (medida):** não é o gate que nos bloqueia (ele passa). **O loader nunca é invocado para o
LglScA** — `func_002BA990` é alcançado 1× (para o R_PermA) e o WAD do ecrã legal nunca entra no fluxo de
processamento. Daí `0` lookups `_R_LglScA`, 1/13 arenas e o plateau.

**Pergunta seguinte (sobe um nível):** o que faz o loader **começar a processar um WAD** em modo inline
(`ts+0x74=0`, consumir o ring)? Na consola acontece para o LglScA antes do R_PermA; em nós nunca.

**Estado:** [D] com avanço real (18 shells, R_PermA completo, 0 crashes). O gap do LglScA está agora
caracterizado com precisão: **não é o gate do state=2** (passa), é o loader **nunca ser invocado** para
esse WAD — o modo inline (rem=0) que a consola usa nunca é accionado em nós.

## §4es — E369: a consola TICKA o loader pelo `func_00041D5C` (23/25); em nós a cadeia NÃO volta ao loop

Probe `oracle_tick_caller.py` (bp `func_002BA76C`, stack walk), consola:
```
#1 lr=0x002BAE18  stack=[0x002BAE18, 0x00041F6C, 0x000B7388, 0x002B2EC8]
#3 lr=0x00041FE8  stack=[0x00041FE8, 0x000B7388, 0x002B2EC8, 0x0025C894]
DONE hits=25 -> caller-lr=0x00041FE8 x23 ; 0x002BAE18 x2
```
**A consola ticka o loader maioritariamente por `lr=0x00041FE8`** = o loop de `func_00041D5C`
(`loc_00041FE4`), com a cadeia `0x0025C894 → 0x002B2EC8 → 0x000B7388 → func_00041D5C → func_002BA76C`.

**Nós:** o único tick também tem `lr=0x00041FE8` (WADLD-SM #1) — ou seja, **o driver CHAMA o loader** —
mas o `KG-PROBE`, colocado **imediatamente a seguir à chamada** dentro de `func_00041D5C`, dispara
**0 vezes** (medido em DUAS configurações diferentes, E351 e agora com a cadeia funcional).

**Conclusão (medida):** o driver chama o loader, mas **a cadeia de trampolins do loader NUNCA regressa
ao ponto do loop** em `func_00041D5C` — por isso o loop **não itera** e ficamos com **1 tick**, enquanto
a consola itera 23+ vezes pelo mesmo `lr`. Não é "o loader devolve 0 e o loop sai" (esse ramo nem chega a
ser avaliado); é o **retorno da cadeia** que não acontece.

**Classe do defeito:** control-flow do lift no caminho de retorno do loader (trampolim/fallthrough), a
mesma família dos bugs já vencidos (fallthrough cross-fragment 2550C8, if-goto E129) — mas aqui no
**retorno ao driver**, não numa função isolada.

**Consequência:** 1 tick ⇒ só o R_PermA entra no loader ⇒ o LglScA nunca é processado (0 lookups,
1/13 arenas) ⇒ construção pára nos 18 shells e `LDRSH`=0.

**Próximo (cirúrgico):** seguir o retorno da cadeia de `func_002BA76C` no nosso lift (onde acaba o
`g_trampoline_fn`/`DRAIN_TRAMPOLINE` e para onde devolve) e porque não volta a `0x00041FE8`; comparar
com a consola, onde o retorno a esse `lr` acontece 23/25 vezes.

**Estado:** [D] com avanço real (18 shells, R_PermA completo, 0 crashes) e o plateau reduzido a um
**defeito de retorno de cadeia**: o driver ticka 1× em vez de N. É a explicação única para LglScA=0,
arenas 1/13 e o plateau.

## §4et — E370: CONFIRMADO sem ambiguidade — o controlo NUNCA regressa da chamada ao loader

O probe de §4es (`KG-PROBE`) estava **depois do `goto`**, logo era ambíguo (se o ramo do loop fosse
tomado, também não dispararia). Repeti com um probe **imediatamente a seguir à chamada, ANTES do
`cmp`/`goto`**:
```c
ctx->lr = 0x00041FE8; func_002BA76C(ctx); DRAIN_TRAMPOLINE(ctx);
[TICKRET]  <-- aqui, antes de qualquer ramo
```
Resultado:
```
TICKRET count = 0        WADLD-SM count = 1
```
**O loader É entrado (1×) mas o controlo NUNCA regressa ao `call site`.** Sem ambiguidade: o probe está
antes de qualquer ramo, logo se o `DRAIN_TRAMPOLINE` terminasse, ele disparava.

**CONFIRMA §4es/E369:** a cadeia de trampolins de `func_002BA76C` **não regressa** a `func_00041D5C`.
Por isso o loop do driver **nunca itera** (1 tick), contra os **23/25** ticks da consola pelo mesmo
`lr=0x00041FE8`.

**Cadeia causal completa e provada:**
```
cadeia de trampolins do loader não retorna ao driver   (E370, medido)
  -> o loop de func_00041D5C não itera: 1 tick (consola: 23+)
  -> só o R_PermA entra no loader; o LglScA nunca é processado (0 lookups vs 19 na consola)
  -> arenas 1/13, construção pára nos 18 shells, LDRSH=0
```

**Próximo (cirúrgico, agora sem ambiguidade):** descobrir ONDE a cadeia acaba em vez de voltar —
instrumentar o fim do `g_trampoline_fn`/`DRAIN_TRAMPOLINE` a partir de `func_002BA76C` (que função a
termina e com que `lr`), e comparar com a consola, onde o retorno a `0x00041FE8` acontece 23/25 vezes.
É um defeito de control-flow do lift no **retorno**, da família 2550C8/E129.

**Estado:** [D] com avanço real (18 shells, R_PermA completo, 0 crashes) e o plateau com **causa única
provada**: o controlo não regressa da chamada ao loader, logo o driver ticka 1× em vez de N.

## §4eu — E371: o NOSSO diagnóstico `PS3_F2B_WAKE` prendia o loader — o flag `ts+0x1B0` é "PENDENTE", não "pronto"

Li o loop de despacho do loader (`func_002BA7F0`, `loc_002BA808`) **exactamente**:
```c
loc_002BA808:
  state = *(ts+0x1CC);
  if (state==2) -> func_002BA9BC;  if (state==3) -> func_002BAB88;  if (state==1) -> func_002BA990;
  flag = *(ts+0x1B0);
  if (flag != 0) goto loc_002BA808;     // <<< GIRA enquanto o flag for NÃO-ZERO
  yield func_0025C0D4 ...
```
O loop **gira enquanto `*(ts+0x1B0) != 0`** ⇒ o flag significa **"leitura assíncrona PENDENTE"**, e
**"pronto/completo" é ZERO**. O nosso diagnóstico E346 (`PS3_F2B_WAKE`) escrevia **1** ("sinalizar
pronto") — semântica **invertida** ⇒ o loader entrava em **spin infinito**, nunca devolvia ao driver.

**A/B medido (mesma build, só o `PS3_F2B_WAKE` muda):**
| | com `F2B_WAKE` (escreve 1) | **sem `F2B_WAKE`** |
|---|---|---|
| controlo regressa (`TICKRET`) | **0** | **30** (`r3=1` ⇒ volta ao loop) |
| ticks do loader (`WADLD-SM`) | **1** | **80** |
| shells | 18 | 5 |

**Explica §4es/§4et:** o "controlo nunca regressa" e o "1 tick" eram causados por **nós**, não pelo jogo.
Sem o `F2B_WAKE` o driver ticka o loader **80×** e o controlo regressa com `r3=1` (continuar) — o
comportamento da consola (23/25 ticks pelo mesmo `lr=0x00041FE8`).

**Trade-off actual (medido):** o `F2B_WAKE` também é o que faz a entrega do R_PermA chegar (18 shells);
sem ele a entrega cai (5 shells). Ou seja **precisamos das duas coisas**: a entrega **e** o loop de ticks,
sem escrever no flag de pendência.

**Nota honesta:** tentei inverter o `F2B_WAKE` para escrever 0; nesse run o F2B **não entregou nada**
(0 fills, 0 GATE-FORCE) — resultado **inconclusivo** (a entrega é intermitente, como visto toda a sessão),
não uma refutação.

**Próximo:** obter entrega + ticks em simultâneo — sinalizar a conclusão da leitura pelo caminho correcto
(o campo/al mecanismo que o jogo usa para completar a op) em vez de escrever no flag de pendência, e medir
`_R_LglScA`, shells e `LDRSH`.

**Estado:** [D] com avanço real (18 shells, R_PermA completo, 0 crashes) e uma **causa NOSSA identificada
e provada por A/B**: o diagnóstico `PS3_F2B_WAKE` prendia o loader num spin (1 tick em vez de 80).

## §4ev — E372: as duas configurações são COMPLEMENTARES — falta refill do ring sem o spin

Corrida longa **sem** `F2B_WAKE` (comportamento de loader correcto):
```
ticks(WADLD-SM)=80   states: 1x state=1, 68x state=2, 11x state=3   rem a DESCER (0x1090170->0x1019700)
delivery: file_pos=265088/20169344 (1,3%)   GATE-FORCE=0   shells=5   crashes=12
```

**Quadro completo (A/B, mesma build):**
| | `F2B_WAKE` ON (escreve 1) | `F2B_WAKE` OFF |
|---|---|---|
| loader ticks | **1** (spin infinito) | **80** ✅ |
| estados | preso | **1→2→3 a ciclar** ✅ |
| `rem` | preso | **a descer** ✅ (assinatura da consola) |
| entrega R_PermA | **20 MB COMPLETO** ✅ | **pára aos 265 KB** ❌ |
| shells | **18** | 5 |

**Diagnóstico:** as duas metades certas estão em configurações diferentes. O `F2B_WAKE`, ao pôr o loader
em **spin**, fazia com que o `f2b_stream_ensure` fosse chamado repetidamente e **empurrava a entrega até
aos 20 MB** — efeito colateral do bug, não desenho. Sem o spin, o loader comporta-se como a consola
(80 ticks, 1→2→3, `rem` a descer) mas o **ring não é reabastecido**: `file_pos` pára em 265 088
(≈ um enchimento de ring de 256 KB) — o velho problema do refill (E344–E349).

**O que falta (preciso):** **reabastecer o ring à medida que o loader consome**, accionado pelo consumo
do loader (não por um spin). Ou seja, chamar o `ensure`/FORWARD no caminho de consumo do loader, para que
`file_pos` avance dos 265 KB até aos 20 MB **enquanto** o loader ticka normalmente.

**Estado:** [D] com as duas metades provadas separadamente — **entrega completa** (config WAKE) e
**loader correcto tipo consola** (config sem WAKE). Falta uni-las: refill dirigido pelo consumo. O
`PS3_F2B_WAKE` fica marcado como **band-aid nocivo** (semântica invertida, §4eu) — não usar.

## §4ew — E373: refill por CURSOR implementado (gated) — NÃO validado (runs não alcançaram a fase)

Implementei o passo que §4ev identificou: **reflectir o consumo real do loader no `avail`** para o
refill FORWARD poder avançar sem depender do spin.
```c
/* PS3_F2B_CURSOR (DIAGNOSTIC, gated OFF) — em f2b_stream_ensure, antes do bloco FORWARD:
 * lê o cursor do ring (stream+8); se avançou desde a última chamada, subtrai o delta ao
 * avail (stream+0x10), para que f2b_stream_fill volte a encher e file_pos avance. */
```
Compila e está **gated OFF por defeito**.

**NÃO VALIDADO:** nas corridas de teste o boot **não chegou à fase de entrega** (`F2B fills=0`,
`WADLD-SM=0`, `shells=5`) — a mesma **intermitência** observada toda a sessão (há runs que completam a
intro e entregam, outros não). Portanto **não há medição** que confirme ou refute este refill. Fica
registado como implementado-mas-por-medir, para não parecer um resultado.

**O que está PROVADO (mantém-se):**
- `PS3_F2B_WAKE` tem semântica invertida e **prendia o loader** (A/B: 1 tick vs 80) — §4eu;
- as duas metades existem em configs diferentes: **entrega completa** (com WAKE) e **loader correcto
  tipo consola** (sem WAKE) — §4ev;
- avanço reproduzido: **18 shells** (era 5), **R_PermA completo**, **0 crashes**.

**Próximo:** validar o `PS3_F2B_CURSOR` num run que chegue à fase (a entrega é intermitente — repetir
até obter um run com `F2B fills>0` e `WADLD-SM>0`), e medir `file_pos` além dos 265 088, shells>18,
`_R_LglScA`>0 e `LDRSH`.

**Estado:** [D] — `LDRSH`=0, registry 18/27 shells e 1/13 arenas. A condição não está satisfeita.

## §4ex — E374: o cursor `stream+8` NUNCA avança — refuta o refill por cursor; o consumo é via `avail`

Corrida que **alcançou** a fase de entrega (`F2B fills=2`, `WADLD-SM=1`, `file_pos=262144/20169344`):
```
F2B-CURSOR = 0      <- a sonda NUNCA disparou
```
A condição da sonda era `cursor(stream+8) > cursor_anterior`. Nunca se verificou ⇒ **o cursor do ring
não avança**. **REFUTA §4ew/E373** (o refill por cursor não pode funcionar) e **confirma E344/E347**:
o loader **não usa** `stream+8`; o consumo aparece como **decremento do `avail` (stream+0x10)**
(medido antes: `was_av` a descer 260640→258016).

**Consequência sobre §4ev (importante):** sem o spin, a entrega **não está bloqueada — está limitada
pelo ritmo de consumo real do loader**. Na corrida longa sem WAKE: 64 fills, `ENSURE=36`,
`file_pos=265088` — cada fill acrescenta pouco porque o `avail` só desce um pouco de cada vez. Ou seja
o F2B **já acompanha** o loader; é o loader que consome devagar. Com o `F2B_WAKE`, o spin martelava o
`ensure` e empurrava 20 MB **sem** o loader os processar — os 18 shells vinham desse artefacto.

**Leitura honesta do "18 vs 5":** os 18 shells (config WAKE) são **maiores mas menos legítimos** (entrega
forçada por um spin que impedia o loader de tickar); os 5 shells (config sem WAKE) vêm de um loader que
**ticka correctamente como a consola** mas ainda só consumiu 1,3% do WAD nessa janela — e a corrida
terminou com 12 crashes.

**Próximo (redefinido, honesto):** não é "empurrar mais dados" — é **deixar o loader correr mais tempo
sem crashar** na config sem WAKE (80 ticks, estados 1→2→3, `rem` a descer) e ver até onde chega. Os 12
crashes dessa config são agora o alvo, não a entrega.

**Estado:** [D] — `LDRSH`=0, registry 18/27 shells e 1/13 arenas; condição não satisfeita. 21.ª correcção
medida da sessão (o refill por cursor caiu na própria medição que o ia validar).

## §4ey — E375: CORRIGE §4ev/§4ex — a config legítima consome ~13 MB e cai na corrupção parede-G (r3=0x0A050000)

> **NÚMERO CORRIGIDO (E386):** «80 ticks» era o **cap do probe** `[WADLD-SM]` (`n++<80`,
> `ppu_recomp_001.cpp`), não o loader a parar — no tick #80 ele ainda está em state 2 com `rem=0x66E350`
> (6,7 MB por consumir). O número de ticks real está por medir (`PS3_TRACE_TYMAP_N`). SOBREVIVE o resto.

> **RÓTULO CORRIGIDO por §4fi (E385).** SOBREVIVE toda a medição (80 ticks, 1→2→3, 13 MB, o crash em
> `func_002F0AAC` com `r3=0x0A050000`). CAI o rótulo «corrupção parede-G»: `0x0A050000` é o **`CellGcmContextData`**
> e o crash é o **callback `+0xC` a NULL** (E66) — resolvido por `PS3_GCM_CB=1`, A/B em §4fi.

Reli a corrida longa **sem `F2B_WAKE`** (a config legítima) no ponto certo:
```
state=3  rem=0x7EB0A0 -> 0x79E9F0 -> 0x71E9F0 -> 0x69E9F0
F2B-STREAM-ENSURE ... rem1D4=0xDBC90  file_pos=527264/20169344
[TICKRET] #2 returned from func_002BA76C r3=0x00000001 (r3!=0 -> loop again)
[ICALL-BAD] ctr=0x00000001 lr=0x0014F048 r2=0x40004790 r3=0x0A050000 r11=0x489E90B8
  host_ra: func_002F0AAC+0x98 <- func_0014F020+0x170 <- func_001568F4+0x468
```
**CORRIGE a minha leitura de §4ev/§4ex ("pára aos 265 KB"):** o `rem` desce de **0x133C280 (20 MB) até
0x69E9F0 (6,9 MB)** ⇒ o loader **consome ~13 MB** do R_PermA, em **state=3 (body-load real)**, com o
driver a tickar (`TICKRET r3=1 -> loop again`). Eu tinha medido cedo demais.

**O bloqueio real da config legítima:** `ICALL-BAD` com **`r3=0x0A050000`** — exactamente o valor que a
memória da **parede-G** identifica ("uma string de caminho escrita na vtable de um objecto,
r3=0x0A050000"), i.e. a **doença do megaprojecto** (string-como-ponteiro). Cadeia host:
`func_001568F4 -> func_0014F020 -> func_002F0AAC` com `ctr=1` (alvo de chamada indirecta = lixo).

**Quadro honesto e corrigido:**
| config | loader | entrega | fim |
|---|---|---|---|
| `F2B_WAKE` ON | **spin, 1 tick** (artefacto nosso) | 20 MB empurrados sem processar | 18 shells |
| **sem `F2B_WAKE`** | **80 ticks, 1→2→3, correcto** | **~13 MB CONSUMIDOS de verdade** | **crash r3=0x0A050000** |

**Conclusão:** a via legítima **funciona e vai longe** (13 MB processados); o que a mata é a **corrupção
parede-G conhecida**, não a entrega. O alvo passa a ser essa corrupção (`r3=0x0A050000`,
`func_0014F020`/`func_001568F4`), que já tem histórico próprio no projeto.

**Estado:** [D] — `LDRSH`=0, registry 18/27 e 1/13; condição não satisfeita. Mas a via legítima está
identificada e mede-se: consome 13 MB e morre num defeito conhecido e nomeado.

## §4ez — E376: a corrupção da via legítima está num ÚNICO SLOT — `*( *(0x0053D438) + 0 ) = 0x0A050000`

Estrutura de `func_0014F020` (onde a via legítima morre):
```c
r29 = *(r2 - 0x3D40);      // holder global; TOC 0x00541178 - 0x3D40 = 0x0053D438
r3  = *(r29 + 0);          // O OBJECTO  -> 0x0A050000 (corrompido)
func_002F0A44(r3);  func_002F068C(r3);  func_002F0564(r3);  func_002F091C(r3);
func_002F09B0(r3);  func_002F05F8(r3);  func_002F0888(r3);  func_002F02E8(r3);
```
São **8 chamadas em sequência, todas com o MESMO `r3 = *(r29+0)`** — e os `ICALL-BAD` medidos foram
exactamente **8**, um por chamada (`lr=0x14F048/58/68/78/88/98/A8/BC`). Ou seja **não são 8 defeitos**:
é **um único ponteiro corrompido** usado 8×.

**Localização exacta da corrupção:** o slot **`*( *(0x0053D438) + 0 )`** contém **`0x0A050000`** em vez
de um objecto válido. (`0x0053D438` = `TOC(0x00541178) - 0x3D40`, o holder do singleton.)

**Liga à parede-G:** `r3=0x0A050000` é o valor que a memória do projeto já nomeia para a doença
"string/valor escrito por cima de um objecto". Agora com **endereço do slot** e **momento** (durante o
body-load do R_PermA, depois de ~13 MB consumidos).

**Próximo (watch cirúrgico):** vigiar escritas ao slot `*(0x0053D438)+0` (ou ao objecto que lá devia
estar) para apanhar **quem** lá põe `0x0A050000` — com o boot na config legítima (sem `F2B_WAKE`), que
chega a consumir 13 MB antes de cair.

**Estado:** [D] — `LDRSH`=0, registry 18/27 e 1/13; condição não satisfeita. Mas a via legítima está
medida ponta-a-ponta (80 ticks, 1→2→3, 13 MB consumidos) e o que a mata é **um slot identificado**.

## §4fa — E377: o objecto `0x0A050000` é LEGÍTIMO; o que troca é a sua VTABLE (intermitente)

Sonda `PS3_TRACE_OBJ14F` em `func_0014F020` (holder `*(TOC-0x3D40)`), num run **sem crash**:
```
[OBJ14F] holder=0x0086DBDC  obj=*(holder+0)=0x0A050000
         obj[0..5]= 47E00000 48000000 47E00104 00000000 00000000 00000000
         (word2 avança entre chamadas: 0x104 -> 0x51C -> 0x92C -> 0xD3C -> 0x114C ...)
ICALL-BAD = 0
```
**O objecto é legítimo:** `word0` (vtable) = **0x47E00000** (ponteiro de heap plausível) e `word2` é um
**cursor a avançar** (~0x410 por chamada) — comportamento normal. **`0x0A050000` NÃO é lixo**: é o
endereço do objecto, em memória guest commitada.

**CORRIGE a leitura de §4ez:** não é "um slot corrompido com 0x0A050000"; o ponteiro está certo. O que
falha, **intermitentemente**, é a **VTABLE**: no run que crashou o dump do `ICALL-BAD` trazia
`r11=0x489E90B8` (vtable usada) e `ctr=1` (método lido dela), enquanto no run saudável a vtable é
`0x47E00000`. Ou seja **`*(0x0A050000+0)` é por vezes substituído** por `0x489E90B8`, cujo slot de método
dá **1**.

**Natureza:** intermitente (este run: 0 ICALL-BAD; o anterior: 8). Consistente com toda a sessão — a
corrupção aparece só em alguns runs.

**Próximo:** apanhar um run que crashe **com a sonda ligada**, para comparar o `obj[0..5]` saudável
(`47E00000 48000000 …`) com o corrompido, e depois vigiar quem escreve `0x489E90B8` em `*(0x0A050000+0)`.

**Estado:** [D] — `LDRSH`=0, registry 18/27 e 1/13; condição não satisfeita. A via legítima consome
~13 MB e morre por **substituição intermitente da vtable** de um objecto que, em si, está correcto.
22.ª correcção medida da sessão.

## §4fb — E378: o objecto exterior está SÃO no crash; a vtable má (`r11=0x4890F97x`) é de um objecto INTERIOR

Apanhei um run **com crash e a sonda ligada**:
```
[OBJ14F] #1..#10  obj=0x0A050000  obj[0..5]= 47E00000 48000000 47E0xxxx ...   <- SÃO (10/10)
[ICALL-BAD] ctr=0x00000001 lr=0x0014F048 r3=0x0A050000 r11=0x4890F978
[ICALL-BAD] ctr=0x00000001 lr=0x0014F058 r3=0x0A050000 r11=0x4890F980   (+8)
```
- O **objecto exterior** (`0x0A050000`, passado por `func_0014F020`) mantém `word0=0x47E00000` e o
  cursor a avançar — **saudável nas 10 amostras**.
- A vtable efectivamente despachada é **`r11=0x4890F97x`**, e **muda 8 bytes entre chamadas**
  (`…978` → `…980`), o que é típico de um **array/tabela percorrida**, não do `word0` do objecto exterior.
- O `host_ra` do crash é `func_002F0AAC` (chamado de `func_002F0A44`), i.e. **dentro do callee**.

**CORRIGE §4fa:** não é "a vtable do objecto `0x0A050000` que é substituída". Esse objecto está bem; a
chamada indirecta que falha está **mais fundo**, em `func_002F0AAC`, sobre um objecto/entrada **derivado
lá dentro** (`r11` avança 8 em 8 ⇒ percurso de uma tabela de entradas).

**Alvo refinado:** `func_002F0A44` → `func_002F0AAC` — que tabela/lista percorre (`r11` a andar 8 em 8) e
porque é que a entrada dá `ctr=1`. O `0x0A050000` e o `func_0014F020` são **contexto**, não o defeito.

**Estado:** [D] — `LDRSH`=0, registry 18/27 e 1/13; condição não satisfeita. 23.ª correcção medida. A via
legítima (sem `F2B_WAKE`) continua a ser a boa: 80 ticks, estados 1→2→3, ~13 MB consumidos, e morre nesta
chamada indirecta dentro de `func_002F0AAC`.

## §4fc — E379 (⚠ "lista de callbacks" RETRACTADO por §4fd/E380: o `+0xC` é NULL, não há tabela de OPDs) descodificação de func_002F0AAC

> **RETRACTAÇÃO (E380):** a leitura "lista de OPDs iterada 8 em 8" está ERRADA. O dump mostra
> `*(obj+0xC)=0` (NULL) e o que eu imprimi como "entradas" a partir de `0x47E00000` são
> **instruções PPC** (código/dados), não descritores. O passo de 8 era o cursor `+8` a avançar.
> Sobrevive: `func_002F0AAC` chama `*( *(r31+0xC) + 0 )` com TOC `+4`. Ver §4fd.

`func_002F0AAC` (onde a via legítima morre) descodificado:
```c
r9  = *(r31 + 0xC);      // OPD (descritor de função) da entrada corrente
ctr = *(r9 + 0);         // endereço de código  <- lê 1 (lixo)  => ICALL-BAD
r2  = *(r9 + 4);         // TOC do descritor
ps3_indirect_call();
```
E o irmão `func_002F0AD8` revela a estrutura de `r31`:
```c
r11 = *(r31 + 8);   // cursor
r9  = *(r31 + 4);   // fim
r0  = r11 + 8;      // avança 8 em 8
if (r0 > r9) -> func_002F0B40   // fim da lista
```
⇒ **`r31` é uma LISTA/vector iterado de 8 em 8**, com `+4`=fim, `+8`=cursor, `+0xC`=elemento (ponteiro
para OPD). Isso explica exactamente os `r11` medidos: `0x4890F978 → 0x4890F980` (**+8**).

**Diagnóstico:** não é corrupção de vtable de objecto (§4fa/§4fb refinados). É uma **lista de callbacks
(OPDs)** a ser percorrida, em que **uma das entradas aponta para um OPD cujo `code` é 1** — entrada não
inicializada / descritor inválido. Cada iteração falha uma vez ⇒ os 8–12 `ICALL-BAD` seguidos.

**Alvo final (concreto):** quem **regista/insere** entradas nesta lista (`r31`: `+4` fim, `+8` cursor,
`+0xC` elemento) e porque é que uma entrada fica com OPD inválido (`*(OPD+0)==1`). Vale a pena dumpar a
lista inteira no crash (percorrer de `+8` até `+4`, 8 em 8, imprimindo cada OPD e o seu `code`) para ver
**quantas** entradas são válidas e **qual** é a má.

**Estado:** [D] — `LDRSH`=0, registry 18/27 e 1/13; condição não satisfeita. A via legítima consome
~13 MB e morre a iterar uma lista de callbacks com uma entrada inválida — alvo agora **estrutural e
dumpável**, não um valor solto.

## §4fd — E380: o defeito é `*(obj+0xC) == NULL` — não há lista; o handler simplesmente não está lá

Dump com `PS3_TRACE_CBLIST` no momento em que o `code` é inválido:
```
[CBLIST] BAD: obj=0x0A050000  +4=0x48000000  +8=0x48000000  +0xC(OPD)=0x00000000  code=0x00000000
[CBLIST] obj[0..3] = 47E00000 48000000 48000000 00000000
(numa 2.ª ocorrência: +8=0x48000148 — o cursor avançou; +0xC continua 0)
```
As "entradas" que imprimi a partir de `0x47E00000` são **instruções PPC**
(`0x901C0488`=`stw r0,0x488(r28)`, `0x7C0803A6`=`mtlr r0`, `0x3BC00000`=`li r30,0`) ⇒ é **código/dados**,
não uma tabela de descritores. **REFUTA §4fc/E379** (não há lista de callbacks iterada).

**Defeito real, simples:** `func_002F0AAC` faz `r9 = *(obj+0xC)` e chama `*(r9+0)`. Medido:
**`*(obj+0xC) = 0` (NULL)** ⇒ lê `*(0)` ⇒ `code=0`; noutras corridas o campo tem lixo e dá `ctr=1`.
Ou seja **o handler/descritor do objecto nunca é preenchido** (fica NULL ou lixo) e o despacho falha.

**Estrutura do objecto `0x0A050000` (medida):** `+0`=0x47E00000 (base), `+4`=0x48000000,
`+8`=cursor (0x48000000→0x48000148, avança), `+0xC`=**handler OPD (NULL ← o defeito)**.

**Alvo final:** quem devia escrever `obj+0xC` (o descritor do handler) e porque não o faz nesta fase.
O objecto está vivo e a trabalhar (o cursor `+8` avança); só falta o handler.

**Estado:** [D] — `LDRSH`=0, registry 18/27 e 1/13; condição não satisfeita. 24.ª correcção medida. A
via legítima consome ~13 MB e morre porque **um campo de handler (`obj+0xC`) está por preencher**.

## §4fe — E381: ORÁCULO CONFIRMA — a consola tem o handler `+0xC` preenchido; nós temos NULL

Probe `oracle_handler.py` (bp `func_002F0AAC`, lê `r31` e campos), consola vs nós no MESMO ponto:

| campo | **consola** | **nós** |
|---|---|---|
| obj | `0x00C20CCC` | `0x0A050000` |
| `+0` (base do buffer) | `0x37EB0000` | `0x47E00000` |
| `+4` (fim) | `0x37EB7FFC` | `0x48000000` |
| `+8` (cursor) | `0x37EB7FF8` | `0x48000000` |
| **`+0xC` (handler OPD)** | **`0x00C20504`** → `code=0x00C13AAC` ✅ | **`0x00000000`** ❌ |

**CONFIRMA §4fd/E380:** o defeito é **o campo `+0xC` (descritor do handler) nunca ser preenchido em nós**.
A consola chega a `func_002F0AAC` com um OPD válido (`0x00C20504`, cujo `code` é `0x00C13AAC`); nós
chegamos com **NULL** e o despacho lê `*(0)`.

**Diferença adicional (a notar):** o buffer da consola é **32 KB** (`0x37EB0000..0x37EB7FFC`) e o nosso
é **2 MB** (`0x47E00000..0x48000000`). Objecto do mesmo tipo, dimensionado de forma diferente — pode ser
outro sintoma da mesma construção incompleta.

**Alvo final (o mais estreito da sessão):** quem escreve `obj+0xC` na consola (o registo do handler) e
porque o nosso caminho não o faz. O objecto é criado e usado (cursor a avançar), mas **sem handler**.

**Estado:** [D] — `LDRSH`=0, registry 18/27 e 1/13; condição não satisfeita. Mas a causa da morte da via
legítima está **confirmada contra a consola**: um campo de handler por preencher (`obj+0xC`), não
corrupção difusa. A cadeia completa desta sessão: intro→entrega→scope→ticks→body-load 13 MB→handler NULL.

## §4ff — E382: procura do construtor de `obj+0xC` NÃO convergiu (resultado negativo, para não repetir)

Tentativas feitas para achar quem preenche `obj+0xC`:
1. **Escritas a `+0xC` com `+0/+4/+8` por perto** (padrão de construtor): dezenas de hits genéricos —
   `+0xC` é um offset comum, sem poder discriminante.
2. **Referências ao holder** (`r2-0x3D40` = `0x0053D438`): 14 sítios, todos na família
   `func_0014DF98…func_0014F164` — **nenhum escreve** `+0` nem `+0xC` da struct; só lêem.
3. **Candidato `func_00014E24`** (escreve `+0,+4,+8,+0xC`, com `*(obj+0xC)=*(r4+0x98)`): **layout
   diferente** do nosso objecto (ali `+0=0`, `+4=0`, `+8=`ponteiro; no nosso `+0`=base do buffer,
   `+4`=fim, `+8`=cursor) ⇒ **estrutura diferente, não é o construtor**.

**Conclusão:** a construção do objecto (`0x0A050000`, com `+0`=base, `+4`=fim, `+8`=cursor, `+0xC`=handler)
**não foi localizada por procura estática de padrão**. O caminho provável é dinâmico: pôr um watch na
escrita de `*(0x0086DBDC+0)` (o slot do holder onde o ponteiro do objecto aparece) ou apanhar no oráculo
quem escreve `+0xC` na consola (bp numa escrita a `obj+0xC` do objecto `0x00C20CCC`).

**Estado final desta sessão:** [D] — `LDRSH`=0, registry **18/27 shells** e **1/13 arenas**; condição
**não satisfeita**. Bloqueio confirmado contra a consola: `*(obj+0xC)` NULL em nós, `0x00C20504` (code
`0x00C13AAC`) na consola. Via legítima medida ponta-a-ponta (80 ticks, 1→2→3, ~13 MB consumidos).

## §4fg — E383: o handler da consola vive num PRX CARREGADO; nós fazemos HLE do LoadModule ⇒ `+0xC` fica NULL

> **PARCIALMENTE CORRIGIDO por §4fi (E385).** SOBREVIVE: o handler da consola vive num PRX (`0x00C13AAC`), fora do
> executável, e nós não mapeamos esse código. CAI a pista «módulo `0x003E`»: o PRX é **`libgcm_sys.sprx`** (mapa de
> memória do log do RPCS3) e o campo é o `callback` do `CellGcmContextData`.

**Consola:** handler `code=0x00C13AAC`, OPD `0x00C20504` — **fora do executável principal**
(`PT_LOAD` = `0x00010000..~0x0057xxxx`). Região `0x00C1xxxx–0x00C2xxxx` ⇒ **módulo/PRX carregado**.

**Nós (boot log):**
```
[ppu] loaded 2 PT_LOAD segments            <- só o executável principal
[cellSysmodule] LoadModule(id=0x003E 'UNKNOWN')   <- stub HLE, não carrega código
[cellSysmodule] LoadModule(id=0x0017 SYSUTIL_NP) / 0x000E SYNC2 / 0x0011 GCM_SYS
(nada mapeado à volta de 0x00C00000)
```

**Explicação estrutural do `+0xC`=NULL:** o handler que a consola põe em `obj+0xC` é uma função **de um
PRX**. O nosso runtime faz **HLE do `cellSysmodule::LoadModule`** (stubs) e **nunca mapeia esse código**;
logo o registo que deveria escrever `obj+0xC` (a apontar para dentro do PRX) não tem alvo — e o campo
fica NULL. Não é corrupção: é **código ausente**.

**Pista concreta:** o jogo pede `LoadModule(id=0x003E)` e o nosso HLE nem reconhece o ID (`'UNKNOWN'`).
Esse é o primeiro candidato ao módulo que fornece o handler.

**Consequência para [D]:** a via legítima (loader a tickar, 13 MB consumidos) morre porque um handler de
módulo não existe no nosso processo. O caminho é: identificar o módulo `0x003E` (e vizinhos), e decidir
entre **carregar o PRX real** ou **fornecer o handler por HLE** com um descritor válido.

**Estado final da sessão:** [D] — `LDRSH`=0, registry **18/27 shells**, **1/13 arenas**; condição **não
satisfeita**. Mas o bloqueio final tem agora **explicação estrutural** (código de módulo ausente por HLE),
não "corrupção do pool de handles".

## §4fh — E384: módulos pedidos pelo jogo — `0x003E` é SCREENSHOT (nome em falta, cosmético); `0x0024` e `0xF035` desconhecidos

> **SUPERADO por §4fi (E385).** A identificação por ID de `LoadModule` era o caminho errado — o mapa de memória
> do log do RPCS3 identificou o módulo directamente (`libgcm_sys.sprx`). Mantido como registo.

`LoadModule` pedidos pelo GoW2 (medido):
```
0x0009 RTC   0x000A SPURS   0x000E SYNC2   0x0010 JPGDEC   0x0011 GCM_SYS
0x0016 SYSUTIL   0x0017 SYSUTIL_NP
0x0024 'UNKNOWN'   0x003E 'UNKNOWN'   0xF035 'UNKNOWN'
```
- **`0x003E` = `CELL_SYSMODULE_SYSUTIL_SCREENSHOT`** — está definido em `cellSysmodule.h:83`; o
  `'UNKNOWN'` no log é só a função de **nome** que não tem o `case` (cosmético, não funcional).
- **`0x0024`** e **`0xF035`** não existem na nossa tabela. `0xF0xx` é a gama de módulos
  **internos/especiais** do SDK.

**Nota de rigor:** identificar o ID **não** identifica o PRX que fornece o handler de `obj+0xC` — o
SCREENSHOT é candidato improvável. O que fica **firme** é o ponto estrutural de §4fg: **todos** estes
`LoadModule` são stubs HLE que **não mapeiam código**, logo qualquer handler que viva num PRX (como o da
consola, em `0x00C1xxxx`) não tem alvo em nós.

**Caminho para o próximo ciclo:** em vez de adivinhar o módulo pelo ID, **usar o oráculo**: na consola,
ler o que está mapeado à volta de `0x00C13AAC` (nome do módulo/segmento) — isso identifica directamente
o PRX que fornece o handler. Depois decidir: carregar esse PRX a sério, ou dar um handler HLE com
descritor válido.

**Fecho da sessão:** [D] — `LDRSH`=0, registry **18/27 shells**, **1/13 arenas**; condição **NÃO
satisfeita**. Cadeia medida: intro→entrega→scope→80 ticks→body-load 13 MB→`obj+0xC` NULL porque o
handler vive num módulo que não carregamos.

## §4fi — E385: o handler NULL é o `callback` do `CellGcmContextData` (libgcm_sys); `PS3_GCM_CB=1` tira o crash: 5→18 shells (A/B)

> **NÚMERO CORRIGIDO (E386):** «80 ticks» era o **cap do probe** `[WADLD-SM]` (`n++<80`,
> `ppu_recomp_001.cpp`), não o loader a parar — no tick #80 ele ainda está em state 2 com `rem=0x66E350`
> (6,7 MB por consumir). O número de ticks real está por medir (`PS3_TRACE_TYMAP_N`). SOBREVIVE o resto.

**Como se identificou (sem adivinhar por ID):** o log do RPCS3 (`~/Library/Caches/rpcs3/RPCS3.log`) regista os
exports de cada PRX com endereço. Mapa medido:
```
sysPrxForUser  0x00C00008..0x00C00AA0   (código em liblv2 0x00BFxxxx)
cellGcmSys     0x00C200AC..0x00C20804   (código em 0x00C13xxx..0x00C14xxx)  <-- libgcm_sys.sprx
cellSpurs      0x00C6321C..0x00C639AC   (libsre.sprx)      sys_fs 0x00C80708..
```
- O OPD da consola `0x00C20504` cai **dentro da gama de dados do `cellGcmSys`**, mas **não é export** (buraco entre
  `cellGcmSetDefaultCommandBufferAndSegmentWordSize` @`0x00C204E4` e `cellGcmGetOffsetTable` @`0x00C2052C`);
  o código `0x00C13AAC` fica entre exports GCM (`0x00C136D0`..`0x00C13F7C`) ⇒ **função interna de `libgcm_sys.sprx`**.
- O `obj` da consola, `0x00C20CCC`, também está nos dados do libgcm_sys ⇒ é o **contexto GCM por omissão do SDK**
  (estático dentro do módulo). E o crash site tem a forma canónica:
  `r9=*(r31+0xC); r0=*(r9); r2=*(r9+4); bctrl` = **`ctx->callback(ctx, count)`** do `CellGcmContextData`
  (`begin@0/end@4/current@8/callback@C`). O handler é o **`cellGcmCallback` real** (overflow do FIFO).
- Em nós, `cellGcmSetupContext` (`libs/video/cellGcmSys.c:356`) escreve **`0` em `+0xC`** — o gap **já medido no
  E66 (2026-08-08)** e com fix gated `PS3_GCM_CB=1` (OPD sintético + sentinela tratada host-side), deixado OFF
  «até o baseline da intro ser provado».

**A/B (config legítima: sem `F2B_WAKE`, com `SCOPE_CLAMP`, `MOVIE_DONE_MS=9000`; uma só variável):**

| | shells | `ICALL-BAD` em `func_002F0AAC` (`r3=0x0A050000`) | fim |
|---|---|---|---|
| A — sem `PS3_GCM_CB` | **5** | 12 | crash (dump de stack host), 6 530 linhas |
| B — `PS3_GCM_CB=1` | **18** | **0** | vivo: `[GCM-CB] #1..#5 overflow ctx=0x0A050000`, flips 0/1/2, SPU jobs; 15 971 linhas |

Baseline da intro em B: `st620 0→1→3→11`, sticky=1, R_PermA completo (`20169344/20169344`, 3 091 preads).
`0x0A050000` = `ctx` do `[GCM-CB]` ⇒ o «objecto» de E375/E381 **era o contexto GCM** o tempo todo.

**O que isto fecha:** a cadeia «corrupção pool-de-handles → handler NULL → módulo em falta» acaba num **campo que o
nosso HLE deixa a zero** e que a consola preenche com o callback do próprio libgcm_sys. É fiel ao console e já tem
implementação; o A/B prova que sozinho vale **5→18 shells sem crash**.

**O que NÃO fecha:** plateau em **18** (E361: a consola faz 397+ constructs type-15, contínuos). `LDRSH`=0,
registry 18/27 shells, 1/13 arenas — **condição não satisfeita**. Próximo: com `PS3_GCM_CB=1` + `PS3_TRACE_TYMAP=1`
ler o `[WADLD-SM]` no plateau (o loader acaba em state 0? fica em 3? ) e comparar com o oráculo front-end (§4fa).
Promoção de `PS3_GCM_CB` a default: há prova de baseline (B); fica para commit separado.

## §4fj — E387: o plateau em 18 shells é um LOOP INFINITO no walk do registry (`func_002ACC30`), dentro do tick #119

**Medido (B3/B4/B6, `PS3_GCM_CB=1`, config legítima):** o loader ticka **119×** (`[WADLD-SM]`, cap levantado),
consome o R_PermA quase todo (`rem 0x133C280 → 0x1A920`, 108 KB por consumir; ring com 246 KB disponíveis), constrói
18 shells (o 18.º no tick 106) e **nunca regressa do tick #119**: `[TICKRET]` regista 118 retornos, todos `r3=1`
(«loop again»), e não há o 119.º. As linhas que continuam no log (SPU jobs, `[MOVIEFSM]`, `[CONTENT] guest/menu`) são de
outras threads — a thread PPU principal está presa.

**Onde (host `sample` da thread principal, duas amostras 8 s afastadas, 100 % dos samples no mesmo sítio):**
```
func_000B71B8 (dispatcher de estado, case 38) → func_00041D5C (driver) → func_002BACBC → func_002B9C80 (leitor de records)
→ ps3_call_opd → func_0023386C (handler do record) → func_002AB270 → func_002AB1AC (lookup por chave)
→ func_002ACC30 (+620/+684/+700: o loop loc_002ACC40/loc_002ACC50) → vm_read32
```
**Estrutura do walk (`ppu_recomp_002.cpp`, `func_002ACC30`):** tabela em `r29` (`+4`=base dos nós, `+8`=fim);
nó de 16 B com `+0`=chave, `+0x8/+0xA/+0xC`=índices de 16 bits (×16 a partir da base). O loop segue `+0xA` até
`nó == fim` ou `chave == r28`. **Um índice que aponte para trás fecha um ciclo e o walk nunca acaba.** É a
«corrupção da tabela de objectos / pool de handles» apanhada em flagrante — não um crash, um spin.

**Sobrevive de antes:** tudo o que foi medido até ao tick 119. **O que muda:** o plateau NÃO é «front-end idle»
(E361) — é a thread principal presa num lookup. Próximo (instrumento já escrito, gated `PS3_TRACE_ACC30_CYCLE`):
ao fim de 200 000 iterações num só lookup, despejar tabela, chave, nome (`g_keyhash_namep`) e a cadeia de 48 nós
com os índices — identifica o nó com o índice para trás; depois, quem o escreveu (walk upstream).
`KEYHASH` ganha `PS3_TRACE_KEYHASH_N` (cap 120 → env); B7 bateu no cap, a chave do hang está por ler.

**Estado:** [D] — `LDRSH`=0, registry 18/27 shells, 1/13 arenas; condição não satisfeita.

## §4fk — E388: o ciclo é DADOS — um nó nunca escrito e um «nó» dentro do ring do WAD que o pump do host enche

**Lift verificado:** `func_002ACD24` lifta correctamente os 4 instr. (`lhz r0,0xC(r3); lwz r9,4(r29); rlwinm ×16; add r30`)
antes do `b 0x2ACC30` — o loop é fiel ao PPC; o spin não é do lifter. (O meu grep filtrava linhas sem branch — corrigido.)

**Dump do ciclo (`PS3_TRACE_ACC30_CYCLE`, B9/B10, reproduz 2/2):** lookup de `'ARRE_DEFAULT_DC'` (chave `0x0E611058`,
nome na stack `0x0FEFF980`, ao contrário dos 2 886 lookups anteriores em `0x008695F4`), tabela `0x00868D2C`,
base `0x40004060`, nil `0x400040C0`:
```
0x400445E0  key=0x276DC841  iA=0xBAC0 iC=0x0000   (> chave → +0xA)  → 0x400BEC60
0x400BEC60  TODO A ZERO                          (≤ chave → +0xC=0) → 0x40004060 (índice 0 = o header do pool)
0x40004060  key=0x400440B8 (um ponteiro) iA=0x4058 (bytes do header)  → 0x400445E0   ← ciclo
```
**Watch de stores (`PS3_WATCH_STORE` nos 16 B de `0x400BEC60` e em `0x400445EA`):**
- **zero escritas** em `0x400BEC60..6C` em toda a corrida — o nó nunca foi inicializado por ninguém (o insert do
  jogo escreve `+4/+8/+A/+C` no nó novo; não passou por aqui ⇒ o índice `0xBAC0` **não veio de um insert**).
- `0x400445E0` está **dentro do ring do WAD** `[0x40044340, +262144)`: o `movie_io_pread` do host (caminho
  `PS3_FIOS_STREAM_PUMP`, `dst = ring + pos % ring_sz`, **independente do cursor do jogo**) escreveu lá 32× 256 KB
  (+1× 128 KB, +2× 3 072 B). O «nó» é bytes do R_PermA. Depois do **último** pread (linha 23 863) o jogo **alocou um
  objecto nessa zona** (`func_00420A44` escreve `1.0f` em `0x400445EC`, linha 24 096) e o walk encravou (25 175).

**O que isto diz:** a entrada no ciclo passa por um nó que vive no ring do WAD ou por um índice para o header (0).
Por decidir (B12, caminho do lookup desde a raiz): qual é o **primeiro** salto ilegítimo — um nó legítimo alocado
na zona do ring depois de o jogo a libertar (e o pump do host a reescrevê-la), ou um link estragado antes disso.
Instrumento novo: `[ACC30-PATH]` grava os primeiros 64 nós visitados no lookup que encrava, marcando `<IN-RING`.

## §4fl — E389: os lookups do loader correm com a TABELA A NULL — o «map» é o endereço de guest 4

> **CORRECÇÃO (E391):** «o walk começa no header do pool (índice 0)» estava errado — o probe `[ACC30-PATH]` só
> vive na cópia do loop em `func_002ACC30` (chunk 002); o lift de `func_002ACBE8` tem a sua própria cópia inline
> (fiel ao PPC), onde o walk começa em `root=*(4)=0x43E8D7F0`. O 1.º nó *registado* é só a 1.ª entrada na cópia
> instrumentada. SOBREVIVE: tabela NULL, `map=4`, o ciclo, e a raiz na pilha de scope.

**Medido (B13, `PS3_TRACE_ARRE`):** na entrada de `func_002ACBE8` para a chave `0x0E611058` (`'ARRE_DEFAULT_DC'`):
`map(r3)=0x00000004`, `lr=0x002AB1F4` (chamada em `func_002AB1AC`, que faz `r28 = r31 + 4` = `tabela + 4`).
Logo **`tabela == NULL`**. O probe `KEYHASH` já mostrava isto sem eu ler: `r3=0x00000004` em **2 840 dos 2 886**
lookups do loader; só os 46 primeiros (`'Master_Server'`, `'WadServer'`, `'ProServer'`…) têm tabela real
(`0x43407790`). Cadeia da tabela: `func_002B0E78` (handler type-1, `this`=r3) passa `this+8` a `func_002B2BA0` →
`func_002AB23C` → `func_002AB1AC(r3=tabela)`; algures nessa cadeia lê-se um ponteiro que está a 0.

**Consequência:** o registry inteiro do WAD foi construído (inserts) e consultado (2 840 hits «válidos») **numa árvore
enraizada no endereço 4**, que o nosso runtime tolera (página baixa acessível). Na consola, `lwz r31,0(r3)` com
`r3=4` seria uma falta de acesso — logo lá a tabela é um objecto real. O spin de E387/E388 é o primeiro lookup que
não «acerta» nessa árvore acidental (`ARRE_DEFAULT_DC`, nome vindo da stack): o walk começa no header do pool
(índice 0) e entra no ciclo.

**Sobrevive:** o ciclo de dados (E388), o pump do host a escrever no ring, o nó nunca escrito. **Muda:** a raiz
não é a corrupção do ring — é **um objecto de registry que nunca é construído/ligado** (ponteiro NULL no handler).
**Em aberto (B14):** `*(4)` lido na entrada = `0x43E8D7F0`, mas o 1.º nó do walk = `0x40004060` — resolver com o
dump a imprimir `*(4)` no momento do spin. Oráculo em curso: `r3` da consola no mesmo lookup (`oracle_arre.py`).

## §4fm — E390: a tabela NULL é o SLOT DA PILHA DE SCOPE — o registry do loader é «o registry do scope actual»

> **CORRECÇÃO (E391):** «o walk começa no header do pool (índice 0)» estava errado — o probe `[ACC30-PATH]` só
> vive na cópia do loop em `func_002ACC30` (chunk 002); o lift de `func_002ACBE8` tem a sua própria cópia inline
> (fiel ao PPC), onde o walk começa em `root=*(4)=0x43E8D7F0`. O 1.º nó *registado* é só a 1.ª entrada na cópia
> instrumentada. SOBREVIVE: tabela NULL, `map=4`, o ciclo, e a raiz na pilha de scope.

**Cadeia lida no PPC (não no lift):** `func_002B2BA0(name)` → `bl func_002AAB64` (obtém a tabela) → `func_002AB23C(tabela, name)`
→ `func_002AB1AC` (`map = tabela + 4`) → `func_002ACBE8(map, key)`. E **`func_002AAB64` não lê um global simples**:
```
g   = *(TOC-0x159C)      ; = *(0x0053FBDC)
idx = *(g+8) ; arr = *(g+4)
return arr[idx]          ; a tabela do SCOPE ACTUAL
```
`func_002AAB84` faz `idx++` — e é chamado no mesmo sítio que o PUSH da pilha de scope de arena (`func_00262C8C`,
§~3590). As duas pilhas andam juntas: o «registry» de cada lookup é o do scope de arena em que se está.

**Logo:** com a pilha de scope **vazia** (idx=128=EMPTY, E353z) ou **clampada** (`PS3_SCOPE_CLAMP=1`, que só
mascara o underflow), `arr[idx]` é 0 → tabela NULL → `map=4` → **todo o registry do WAD vive no endereço 4**
(E389) → o 1.º lookup que não acerta nessa árvore acidental entra no ciclo (E387/E388). O «+13 shells» do clamp
(E353z) era a árvore no endereço 4 a «funcionar» por acidente. Fecha-se assim a cadeia inteira de [D] num único
defeito, já apontado em E321/E353z: **o desequilíbrio enter/leave da pilha de scope** (Root no topo em vez de BP;
`func_0042A0C8` como push órfão foi retractado em §4ei/§4em — o defeito real continua por localizar).

**Próximo (discriminador, alinhado por chave):** `oracle_scopeidx.py` — na consola, por lookup: `key`, `map`,
`root`, `idx`, `arr[idx]`; nós: `[KEYHASH]` (key, r3) + idx (a acrescentar). O 1.º ordinal em que o nosso `map`
é 4 e o da consola não, e o `idx` de cada lado nesse ponto, dizem que enter/leave falta ou sobra.

## §4fn — E391: a consola ENCONTRA `ARRE_DEFAULT_DC` com uma tabela real; nós temos NULL — e o B11 «natural» foi outra intro flaky

**Oráculo (`oracle_arre.py`, EBOOT_intro, headless):** o lookup da chave `0x0E611058` acontece na consola no
**lookup #8437** (nós: #2887) com **`map(r3)=0x306373FC`** (tabela real `0x306373F8`), `root=*(map)=0x3003F510`,
pool `0x00868D2C` = {`0x3003ED40`, base `0x3003ED10`, end `0x3003ED70`} — o mesmo pool que o nosso (`0x40004060/…C0`),
só noutra base de heap. Confirma E389/E390 por comparação directa: **a nossa tabela é NULL onde a consola tem
um objecto**. (O `r3` de retorno não foi lido — a sessão acabou no deadline com 1 hit.)

**Nós (B14):** no momento do spin `*(4)=0x43E8D7F0` (raiz real da árvore acidental no endereço 4), `r31=end`. A
diferença de ordinal (#8437 vs #2887) diz que a consola faz ~3× mais lookups antes deste — o front-end dela está
noutro regime (menu vivo) quando o loader chega aqui.

**B11 (caminho natural, sem `WAD_HOST_READ/STREAM_PUMP/HOST_FILL/REMDEC/FORWARD/ENSURE_BODY`):** intro presa em
`st620=1` (como B5) — flakiness da intro, **não** é medição do caminho natural. Por repetir.

**Próximo:** `oracle_scopeidx.py` (consola: por lookup `key/map/root/idx/arr[idx]`) vs `[KEYHASH]` nosso com
`g/idx/tbl[idx]` (B15) — o 1.º ordinal em que divergimos e o `idx` de cada lado.

## §4fo — E392: alinhado por lookup, a consola faz DOIS ENTERs que nós saltamos — e a LEAVE correspondente corre na mesma

**Por lookup (`oracle_scopeidx.py` vs `[KEYHASH]` com `g/idx/tbl[idx]`, B15):** `g=0x00868C00`, `arr` = tabela de
objectos (`0x30004480` consola / `0x40004480` nós).

| | lookups iniciais (46) | lookups do loader |
|---|---|---|
| consola | idx 15, `tbl[15]` real | **idx 14**, `tbl[14]=0x306373F8` (registry do WAD) |
| nós | idx 15, `tbl[15]=0x43407790/0x433F6770` | **idx 16**, `tbl[16]=0` → `map=4` |

**Sequência de scope sem o par por-frame (`oracle_scope_seq.py`, 1 072 pares de frame filtrados; nós B16, `SCOPESTK`).**
Nota de nomes: no probe nosso `SCOPE-POP` = `func_00262C64` = idx-- = **ENTER**; `SCOPE-PUSH` = `func_00262C8C` = idx++ = **LEAVE**.

| consola (ordem) | nós |
|---|---|
| ENTER `0x41EFC` / LEAVE `0x41F48` | igual |
| **ENTER `0x2BBAF0` / LEAVE `0x2BBB58`** (`func_002BBA64`, criação de arena) | **ausente** |
| ENTER `0x2BA490` (type-21) … registry **15→14** dentro … LEAVE `0x2BB8B8` (registry volta a 15) | ENTER `0x2BA490` … LEAVE `0x2BB8B8` (registry **15→16**: o `idx++` da LEAVE sem o `idx--` dentro) |
| **ENTER `0x42AF50`** (`func_0042AF18`) / LEAVE `0x42A1A4` (`func_0042A0C8`) — em par, 3× | **só a LEAVE `0x42A1A4`** (127→128, clamp) |
| … mais tarde registry fixa-se em 14 (loader) | registry 16 |

**As guardas (PPC):**
- `func_002BBA64`: `size=r3`, **ou** o override global `*(0x0053FFC8)` se ≠0 (é consumido e limpo); `size==-1` →
  `0x2BBC40` antes do ENTER. Ours: `r28=0xFFFFFFFF` (E-anterior).
- `func_0042AF18(r3,r4)`: `r4==0` → salta o ENTER (`0x42B030`); senão `ENTER(*(r4-4+0x9C))`. Ours: `r4==0`.
  A LEAVE em `func_0042A0C8` (saída normal, `func_002AAB84`+`func_00262C8C`) corre independentemente.

**Correcção de registo:** §4ei/§4em NÃO retractaram `func_0042A0C8` (eram sobre nomes/truncado); a minha síntese
anterior estava errada — a LEAVE órfã de `0x42A1A4` (§~3590) **confirma-se** pela comparação com a consola.

**Próximo (a correr):** valores nas duas guardas, consola (`oracle_enters.py`) vs nós (`PS3_TRACE_ENTERS`, B17):
`r4` de `func_0042AF18` e `size`/override de `func_002BBA64`.

## §4fp — E393: nas duas guardas, a consola tem DADOS e nós temos «vazio» — o size da 1.ª arena é `0x0007A120` (o `+0x20` do R_LglScA)

**`func_002BBA64` (criação de arena), `oracle_enters.py` vs `PS3_TRACE_ENTERS` (B17):**

| chamada | consola `size(r3)` | nós |
|---|---|---|
| 1.ª (`lr=0x002BA460`, type-21) | **`0x0007A120`** | **`0xFFFFFFFF`** → early return, sem arena |
| 2.ª | `0xFFFFFFFF` (também early return) | — (só há uma) |

`0x0007A120` é exactamente a palavra `+0x20` do cabeçalho do `R_LglScA` (tabela de E363) — E364 dizia «não é um
campo de tamanho de **ficheiro**»: certo, é o **tamanho da arena** do LglScA. O handler type-21 `func_002BA3F0`
faz `*(ts+0x1B0)=1` (pendente) e `size = bswap(*(r4))`, `arg=bswap(*(r4+4))`, `name=r3+8` → `func_002BBA64`.
**O nosso payload `r4` está a 0xFF** na 1.ª chamada — o record type-21 do LglScA chega ao handler sem dados.
É o mesmo facto de E366-E381 («0 lookups `_R_LglScA`; nunca abre o LglScA como container») visto do lado do
consumidor: sem a arena do LglScA não há ENTER/NEWREG, o registry fica NULL (E390-E392) e tudo o resto segue.

**`func_0042AF18`:** a consola chama-a dezenas de vezes (callers `0x42CE68`, `0x2B0E60` (type-1), `0x41668`,
`0x2BD240`, `0x2BC890`, `0x723D4`, `0x41564`, `0x24CE34`, `0x2903C4`) sempre com `r4` = objecto vivo
(`0x32F84EA4`/`0x306380FC`); **nós nunca a chamamos** (0 hits) — mas corremos a sua LEAVE par (`func_0042A0C8`).
Segunda assimetria, a caracterizar depois do LglScA.

**Próximo (a correr):** `r4` e as primeiras palavras do payload em `func_002BA3F0`, consola (`oracle_t21.py`) vs
nós (`[T21-2BA3F0]`, B18): onde está o payload (ring? corpo?), quem devia tê-lo escrito.

## §4fq — E394: o record do LglScA é despachado na consola por um CAMINHO SÍNCRONO do driver que em nós não corre

**Identidade do record (`oracle_t21.py` vs `[T21-2BA3F0]`, B18):** a consola despacha **4** records type-21
(`size` = `0x0007A120` LglScA, `-1`, `0x001E1400`, `0x00800000`…, a partir de um buffer de cabeçalho `0x30083D60`,
`lr=0x2B9CD4` = retorno do `bctrl` do leitor `func_002B9C80`); nós despachamos **1**, com as palavras
`FFFFFFFF FFFFFFFF 00000000 00000000 02000000` = **byte a byte o 2.º record da consola** (o do R_PermA), lido
directamente do ring (`buf=0x40044360` = ring+0x20). O record do LglScA **nunca chega ao leitor** em nós.
(Nota lateral: o nosso `lr` no handler é `0x2B9CA8` — o `ps3_call_opd` do lift não actualiza `lr` no `bctrl`.)

**O caminho (stack da consola no despacho do LglScA):** `func_002BA3F0 ← func_002B9C80 ← func_002BACBC ←
0x2BAE18 ← 0x41F6C ← func_000B71B8…` com `ts+0x74=0xC00` (3 072 = o LglScA), `state=2`. Isto é o driver a chamar
**`func_002BAD10(desc, 0x10000210)` em `0x41F68`** (flag `0x200` = **síncrono**): abre o contentor
(`func_002B4340`), e se `r3!=0` e `ts+0x74!=0`, **corre ela própria o loop de ticks** (`0x2BAE14`,
`func_002BA76C` até devolver 0) — é aí que os records do LglScA são lidos. Só depois o driver chama
`func_002BAD10(desc, 0x10)` em `0x41FC8` (R_PermA, assíncrono) e entra no loop de `0x41FE4` que conhecíamos.
Na consola (`oracle_bad10.py`): open síncrono → `r3=1`, `ts+0x74=0xC00`; depois `0x10` → `0x133C280`;
mais tarde outros contentores (`0x342A60`…).

**Nós (B19, `[BAD10]`):** o driver faz as duas chamadas (`0x10000210` em `0x41F6C`, `0x10` em `0x41FCC`) mas o
probe **logo a seguir ao open** (linha seguinte a `func_002B4340`) **nunca dispara**, e nenhum tick tem
`lr=0x2BAE18` (todos `0x41FE8`). O controlo sai do lift de `func_002BAD10` por um fragmento não instrumentado
(`func_002BADF4`/`func_002BAE20`/`func_002BAE48`/close) — B20 em curso com probes em cada um.

**Consequência (a cadeia inteira):** sem o loop síncrono não há records do LglScA → o type-21 com `0x7A120` não
chega → `func_002BBA64` só vê o `-1` do R_PermA → sem arena → sem ENTER/NEWREG → registry NULL → árvore no
endereço 4 → spin em `ARRE_DEFAULT_DC` → 18 shells. Fecha E366-E381 («nunca abre o LglScA como container»)
com o mecanismo: **o caminho síncrono de `func_002BAD10`**.

## §4fr — E395: com `PS3_MOVIE_DONE_MS=3000` UMA corrida em três correu o caminho síncrono — registry real, 70 shells, e um `abort()` do jogo no alocador

> **HIPÓTESE «frame abandonada» REFUTADA (E396, mesmo dia):** não há `[ICALL-ASCII] abort CE03C` nem skips nos
> runs maus (o único `longjmp` do runtime não disparou). O probe «após o open» não dispara porque o open
> corre **noutro fragmento**: `func_002BAD10` toma o ramo «busy» `0x2BAE48` (há gestor em `*(r30+0x58)`), que
> termina em `b 0x2BAD78` → fragmento `func_002BAD78` com a **sua própria cópia** do open e do loop síncrono
> (a mesma armadilha de §4fn/`func_002ACC30`). O probe tem de ir para `func_002BAD78`. SOBREVIVE o resto.

**Bisect pedido pelo utilizador (`PS3_MOVIE_DONE_MS` 9000→6000→4000→3000, critério `st620→0`):** todas chegam a
`st620=0`; 6000 e 4000 dão o plateau habitual (18 shells); **3000 deu 70 shells, 257 ticks** — e dois repetidos a 3000
(com probes de entrada ligados) deram 18/18. É uma **corrida** que o open síncrono do LglScA ganha às vezes.

**A corrida boa (`bis_3000.log`):** `[WADLD-SM]` com **4 ticks `lr=0x2BAE18`** (o loop síncrono de `func_002BAD10`
correu) + 253 do loop normal; `[KEYHASH]` **6 066 lookups com `idx=14 tbl[14]=0x433F6770`** (registry real, a forma
exacta da consola) e **zero** com `map=4`; sem ciclo, sem `ICALL-BAD`; `LDRSH`=0. Terminou com o **jogo a chamar
`sys_process_exit(1)`** após `abort()` em `0x379070 ← 0x262D24 ← 0x262EB0 ← 0x2D9788 ← 0x27B70C ← 0x27B7F4 ←
0x422F90 ← 0x42010 (driver)`. `func_00262D08` = «arena esgotada → handler `0x378E4C` → abort»: o alocador de
arena (grow em `0x262EAC`) falhou — arena errada (Root vs BP, E321) ou tamanho errado; a consola não aborta. É a
**parede seguinte**, 52 shells depois da actual.

**Nos runs maus:** `[BAD10]` mostra as duas entradas do driver, mas o probe na linha a seguir a `func_002B4340`
(open) **nunca dispara** e o driver chega à 2.ª chamada na mesma. Hipótese a testar (B21, probe no driver logo após
a 1.ª chamada): a frame de `func_002BAD10` é **abandonada** (uma espera do runtime que desenrola a stack host) em vez
de regressar — o que explicaria o carácter não determinístico (só quando o open completa antes da espera).
Os probes de fragmento de B20 **não foram inseridos** (script aninhado falhou em silêncio) — sem efeito na conclusão.

**Sobrevive:** toda a cadeia E385-E394. **Muda:** o bloqueio não é estrutural no lift — é uma corrida no open
síncrono, e a via boa existe no nosso runtime (1/3). Quando a via boa corre, a parede seguinte é o `abort()` do
alocador de arena.

## §4fs — E397: a corrida é o open síncrono do LglScA — `func_002BAD10` devolve 0 em 2 de 3 corridas; anatomia do open no PPC

**Taxa (3000 ms, probes off, 3 corridas):** 18/18/**70** shells; a boa tem 4 ticks síncronos, 6 091 lookups em
`tbl[14]`, e acaba no `abort()` do alocador (E395). **`[DRV]` (B21):** nas más, a 1.ª `func_002BAD10` (síncrona)
**devolve 0**; a 2.ª (assíncrona) devolve 1. Não é abandono de frame (E396) — é o open a falhar.

**`func_002B4340(op, desc, flags)` no PPC:** `op+0xC=flags`; com `flags&0x20` → `func_00373D00` (path) →
`func_002B3890` → **`func_0030D578(sched, 0, path, 1, &op+4)`** (submete a op FIOS; o **resultado é escrito
assincronamente em `op+4`**); com `flags&0x200` (síncrono) → **poll `func_002B4224(op)` até ≠0** → devolve
**`r3 = (*(op+4) != 0)`**. `func_002B4224`: `h=*(op+8)`; `*(h+0x90)` (done, `isync`) → 0 = ainda não;
≠0 → `func_002B4274` (DONE): `func_00306610(1,&res,0)` (wait/resultado da op) — `0x8001070A`/`0x80010705`
= pendente (→ `0x2B4330`); 0 = OK (`op+0xC |= 0x10000`, `op+0x10=…`); outro = `func_002B3CB0` (erro);
depois `func_0030AE58(sched,h)` (delete) e `op+8=0`, devolve 1.

**Logo o `0` do open síncrono = a op ficou «done» com `op+4 == 0`** — a completação nunca entregou o resultado
(ou entregou 0). É o mecanismo de **E244** («o open do WAD COMPLETA mas o submetedor dorme em `lwcond opWait`
e a limpeza do scheduler cancela a op sem dono») — então subordinado, agora o **defeito raiz de [D]**, com um
consumidor preciso (o LglScA síncrono) e uma taxa (2/3 falha a 3000 ms; ~3/3 a 9000 ms).

**Em curso (B22):** `[BAD78]` (o fragmento real: `r3`, `ts+0x74` após o open) e `[DONE-4274]` (código do
`func_00306610`, `op+4/+0xC/+0x10`) em 3 corridas a 3000 ms.

## §4ft — E398: a op FIOS do LglScA COMPLETA BEM (tamanho 0xC00 entregue) e mesmo assim o open síncrono devolve 0

**B22 (3 corridas a 3000 ms, `[BAD78]`+`[DONE-4274]`, 3/3 falharam):** sequência idêntica nas três:
```
[DONE-4274] #3 wait_ret(r3)=0x00000000 op=0x008695D4 op+4=0x4301C1D0 op+8(h)=0x4300D110 op+0xC=0x10000210 op+0x10=0x00000C00
[BAD78] after-open #1 r3=0x00000000  flags=0x10000210 ts+0x74=0x00000C00 state=1
[BAD78] CLOSE(sync fail) …           → [DRV] after 1st func_002BAD10 (sync) returned r3=0
[BAD78] after-open #2 r3=0x00000001  flags=0x10 (R_PermA, assíncrono, ok)
```
Ou seja: o `func_00306610` devolve **0 (OK)**, `op+4` tem o ponteiro de resultado (`0x4301C1D0`, com `+0x48` =
**0xC00 = o tamanho do LglScA**, copiado para `op+0x10`), `ts+0x74=0xC00`. **A op não foi cancelada nem ficou
vazia** — a hipótese E244/E397 («completação sem resultado») **cai para este open**. O que falha é o **valor de
retorno** de `func_002B4340`: a cauda em `0x2B4434` calcula `r3 = (*(op+4) != 0)` (srawi/xor/subf/rlwinm) = 1
esperado, e o lift devolve **0**. Os helpers `ppc_rlwinm(…,1,31,31)`/`ppc_sraw` estão certos para estes valores
(máscara 1, bit de sinal). Suspeitos: o fragmento `func_002B441C` (cauda), a restauração `_cs_` de `r31`, ou o
caminho `0x2B4308` (`op+8==0`). Em curso (B23): probes na entrada/poll/cauda de `func_002B441C` + watch em `op+4`.

**Sobrevive:** toda a cadeia até ao open síncrono (E394-E397) e o mecanismo E244 para a op de LEITURA do R_PermA
(§4ai). **Cai:** a atribuição da falha do open do LglScA ao cancel FIOS. **Nota:** nas corridas boas (1/3 sem
probes) o mesmo caminho devolveu 1 — o defeito é não-determinístico dentro do próprio lift/runtime.

## §4fu — E399: A CORRIDA, medida com thread ids — a thread do scheduler FIOS ZERA `op+4` entre o DONE e a cauda do open

**B23 (`PS3_WATCH_STORE=0x008695D8` = `op+4` do op do loader, + probes em `func_002B441C`):**
```
main (tid …818)  func_0030D5CC (submit FIOS, via func_002B43DC)   op+4 := 0, depois := 0x4301C1D0 (ptr de resultado)
main             [441C] entry  op+4=0x4301C1D0 op+8=0x4300D110 op+0xC=0x10000210
FIOS (tid …883)  func_0030CD1C+0x1B4 ← func_0030CD48+0x4A8         op+4 := 0            ← a limpeza do scheduler
main             [441C] poll-ret r3=1  op+4=0  op+8=0  op+0xC=0x10010210 (bit16 = done)
main             [441C] tail  r0=0 → r3=0      → CLOSE(sync fail) → func_002BAD10 devolve 0
```
O DONE (`func_002B4274`) ainda leu `op+4` válido (copiou o tamanho `0xC00` para `op+0x10`) e chamou
`func_0030AE58` (delete da op); **entre esse delete e a leitura da cauda (`lwz r0,4(r31)` em `0x2B4434`, ~10
instruções depois) a thread do scheduler FIOS corre `func_0030CD48 → func_0030CD1C` (a passagem de cancelamento/
limpeza de E238-E245) e escreve 0 em `op+4`.** Na consola a cauda lê antes dessa passagem. Em nós o delete passa
por lwmutex/lwcond HLE que **larga o giant lock** (`sysPrxForUser.c:443-450`, `ppu_giant_lock_acquire_prio`) e a
thread FIOS (prio-waiter) entra imediatamente. A mesma classe de E244, com o instante e a instrução exactos.

**Consequência:** é o defeito raiz medido de [D]: a partir daqui, quando o open síncrono devolve 1 (1/3 das corridas
a 3000 ms), o LglScA é processado, a arena criada, o registry fica real e a construção avança até 70 shells (E395).

**A/B em curso (B24):** `PS3_GIANT_TIMESLICE=0` (desliga a preempção temporal do giant lock) a 3000 ms ×2 — se o
open passar a devolver 1, a ordem é imposta pela nossa preempção; senão, é o hand-off no lwmutex do delete.
Fix candidato fiel: não entregar o giant lock a prio-waiters enquanto a thread principal está dentro do DONE/delete
(secção FIOS), ou o que E244 já propunha: a limpeza não cancelar/limpar uma op completada há menos de um tick.

## §4fv — E400: `PS3_GIANT_TIMESLICE=0` não muda a ordem (2/2); o zero em `op+4` é o «clear do handle do utilizador» do free da op FIOS

- B24: com a preempção temporal do giant lock desligada, a sequência é **idêntica** (FIOS zera `op+4` durante o poll,
  cauda lê 0, open devolve 0). O hand-off não é o timer — é dentro do delete (`func_0030AE58`), via lwmutex/lwcond HLE.
- PPC de `func_0030CD1C+0x0C..0x24`: `r9 = *(fiosop+0xB8)` (o slot do utilizador registado no submit, = `&op+4`);
  se ≠0 → `*(r9) = 0`. É a semântica normal do FIOS («ao libertar a op, anula o handle do utilizador»). Logo a
  **única** diferença possível com a consola é a **ordem**: lá a cauda (`0x2B4434`) lê `op+4` antes de a thread FIOS
  libertar a op; cá a thread FIOS corre primeiro.
- Em curso: `oracle_optail.py` (consola: bp na cauda e no clear, com `op+4` em cada) e B25 (`PS3_LWCOND_TRACE=1`: que
  lwmutex/lwcond a thread principal larga dentro do delete).

## §4fw — E401: A CAUSA — um yield/sleep de 50 ms NOSSO, injectado a seguir ao delete da op no DONE, inverte a ordem; a consola lê `op+4` antes do free

**Consola (`oracle_optail.py`, bp na cauda `0x2B4434` e no clear `0x30CD40`):** na cauda do open síncrono do
loader, **`*(op+4) = 0x33018528`** (≠0 → `r3=1`); o clear do slot só acontece **depois** (o 2.º TAIL, de outro op,
já vê `loader-op+4=0`). Ordem na consola: **cauda → free**.

**Nós (B25, `PS3_LWCOND_TRACE=1`):** entre `[441C] entry` e `poll-ret`: `F2B-RESTATUS`/`F2B-STREAM-PUMP` (host),
`DONE-4274` (ok), e a seguir ao `func_0030AE58` (delete) **o nosso bloco injectado em `func_002B42B4`**
(`ppu_recomp_005.cpp:36700`, «FIOS-42B4-CANCEL-YIELD: DONE early path cancel must free op»):
```
ppu_giant_lock_release(); ps3recomp_giant_lock_yield_sleep1();
usleep(PS3_FIOS_DONE_YIELD_MS*1000)   /* default 50 ms, NÃO gated */
ppu_giant_lock_acquire();
```
→ `WAIT-EXIT cond='schedul'` na thread FIOS → `func_0030CD48→func_0030CD1C` liberta a op e **zera `op+4`** →
`SIGNAL 'schedul'` → a thread principal volta, `poll-ret r3=1` com `op+4=0` → cauda devolve 0.
Ordem em nós: **free → cauda**, imposta por nós. Viola a regra «paliativos gated OFF por default» e é infiel à
consola (que não cede ali). A corrida de E395 (1/3 boas) era o FIOS não conseguir libertar a op dentro dos 50 ms.

**A/B em curso (script serializado):** (A) `PS3_FIOS_DONE_YIELD_MS=0` (só tira o sleep) ×2 a 3000 ms; (B) bloco
inteiro gated por `PS3_FIOS_DONE_YIELD` (default OFF) — rebuild — ×2 a 3000 ms e ×2 a 9000 ms (config default).
Critério: `[DRV] after 1st func_002BAD10 (sync) returned r3=1`, `sync_ticks>0`, `map4=0`, `idx14>0`, shells ≫ 18.

## §4fx — E402: FIX PROVADO — sem o yield injectado, o open síncrono devolve 1 em 5/5 corridas com intro completa; registry real, 70 shells; parede seguinte = arena esgotada

| variante | corridas | open síncrono | shells | lookups no endereço 4 | registry | fim |
|---|---|---|---|---|---|---|
| `PS3_FIOS_DONE_YIELD_MS=0` (só sem sleep), 3000 ms | 2/2 | **r3=1** | **70** | 0 | `idx=14`, 6 091 lookups | `abort()` do alocador |
| bloco gated OFF, 3000 ms | 2/2 | r3=1 | 70 | 0 | idem | idem |
| bloco gated OFF, **9000 ms (config default)** | 1/1 (+1 intro flaky, st620=1) | r3=1 | 70 | 0 | idem | idem |

Fix: `gow2-recomp/patch_e401_fios_done_yield_gate.py` (idempotente; `PS3_FIOS_DONE_YIELD=1` repõe o antigo). É a
remoção de um paliativo NOSSO, fiel à consola (`oracle_optail`). Fecha a cadeia E387→E401 de [D]: spin no registry
← tabela NULL ← scope sem ENTER ← arena do LglScA não criada ← record type-21 não despachado ← loop síncrono não
corrido ← open síncrono a devolver 0 ← `op+4` zerado antes da cauda ← o nosso yield de 50 ms.

**Parede seguinte (medida nas corridas boas):** só **2** records type-21 chegam a `func_002BBA64` (`0x0007A120` =
arena do LglScA, e `-1` do R_PermA); os 70 shells alocam do mesmo sítio (`this=0x43E10630`, `r3=0x442FDCxx`
a avançar 0x3C por shell) e o 71.º esgota a arena → `func_00262D08` → `abort()` → `sys_process_exit(1)`. A consola
recebe mais type-21 (`0x001E1400`, `0x00800000`…) de contentores que carrega **por outros sítios do driver**
(`func_002BAD10` com `lr=0x42F0C` e `0x42AA4`, tamanhos `0x133C280` e `0x342A60`, `oracle_bad10`), que em nós ainda
não acontecem. `LDRSH`=0. **Condição do hook: registry populado SIM (real, 6 091 lookups, 70 shells > 27); compiles
de nível NÃO (`LDRSH`=0)** — parcialmente satisfeita, com o mecanismo inteiro fechado por medição.

## §4fy — E403: o driver chega aos estados 13 e 21 (BAD10 `lr=0x42F0C` / `0x42AA4`)?

Estático (sessão Grok, 2026-09-04, sem boot): há **9** `bl 0x2BAD10` no EBOOT. Os dois da intro
(`0x41F68` flags `0x10000210` síncrono LglScA; `0x41FC8` flags `0x10` async R_PermA) já correm em nós
(E402). Os dois citados no E402:

| lr | caso do switch `0x4234C` | flags | a seguir |
|---|---|---|---|
| `0x42F0C` | **13** (`0x0D` → `func_00042DFC`) | `0x10` | grava `+0x550=14` e `b 0x42304` |
| `0x42AA4` | **21** (`0x15` → `func_00042998`) | `0x10000010` | se `r3!=0`, grava `+0x550=22` e retorna |

Ambos decrementam `*(obj+0x44)` e chamam `0x43FF30` (SMPD/TOC) **antes** do open. Os outros 5 sítios
(`0x428C4`, `0x43550`, `0x437A8`, `0x439A8`, `0x43B90`) são casos 22/11/… do mesmo driver.

`patch_e401` **não está aplicado** no lift de produção (`recomp_macos_v2`): o bloco
FIOS-42B4-CANCEL-YIELD corre sempre; só o `usleep` honra `PS3_FIOS_DONE_YIELD_MS` (default 50).
Baseline desta medição = variante A do E402 (`PS3_FIOS_DONE_YIELD_MS=0`), que já dava r3=1 / 70 shells.

### Question

O nosso `*(driver+0x550)` chega a 13 ou 21, ou o driver nunca sai dos estados da intro?

### P1 — nunca chega
Então o gap é a **transição de estado** (contador `+0x44`, fila `+0x40`, SMPD `0x43FF30`), não o open.
`[LDRSTATE]` não mostra 13 nem 21. `[BAD10]` (quando existir) não mostra `lr=0x42F0C` nem `0x42AA4`.

### P2 — chega e o BAD10 corre
Então o gap é o **payload/arena** desses contentores (size, type-21). `[LDRSTATE] 13` e/ou `21` aparecem
e `[BAD10]` mostra os lr correspondentes.

### Neither
O estado passa (`[LDRSTATE] 13` ou `21`) e o `bl` não dispara (lift/fragmento). Ou o contrário.

Controlo (void se falhar): intro `st620>=3`; `[LDRSTATE]` com pelo menos um estado ≠ vazio (a sonda
E130 já está no `boot_gow2`); shells ≫ 18 **ou** `abort()` do alocador (baseline E402). Primeiros
`[BAD10]` (fase 2) têm de incluir `lr=0x41F6C` e `0x41FCC`.

Cap: `[LDRSTATE]` imprime só em mudança; `[WADSLOTS]` cap 40; `[BAD10]` cap 400 (fase 2).
Janela: 150 s headless, mata por PID. 2 corridas na fase 1 (só `PS3_TRACE_REG`); fase 2 depois do rebuild.

<!-- CUT LINE — nothing above is rewritten after running -->

### Resultado (2026-09-04)

Controlo do baseline E402 (70 shells / `abort()` da arena) **FALHOU**. Não dá para decidir P1/P2 da parede 70→71.

| run | binário | yield | intro | BAD10 lr | LDRSTATE | WADLD-SM | shells | fim |
|---|---|---|---|---|---|---|---|---|
| r1 | `boot_gow2` antigo | ON (`MS=0`; `yield_sleep1` ×4) | 3→11→0 | (sonda ausente) | 0 | #1 state=1 rem=0 | 6 | FREELIST-TAG-GUARD. VOID |
| r2 | `boot_gow2_e403` chunks 001+005 misturados | OFF | 3→11→0 | 0 | 0 | 0 | 5 | idle flip. VOID (objectos misturados) |
| r3 | `boot_gow2_e403` **coerente** (7 chunks) | OFF (0 prints) | 3→11→0 | **#1 `0x41F6C` flags=`0x10000210`; #2 `0x41FCC` flags=`0x10`** | 0 | #1 state=1 rem=0 | 6 | FREELIST-TAG-GUARD |

Sonda `[BAD10]` chegou ao binário (controlo da sonda OK: os dois sítios da intro aparecem, cap 400 não atingido). **Nenhum** `lr=0x42F0C` nem `0x42AA4`. O switch `func_00042294` não correu (`[LDRSTATE]`=0, `[WADSLOTS]`=0).

r3 timeline: intro acaba → `ICALL-BAD` ctr=`0x43E480` lr=`0x43EA44` → BAD10 sync LglScA + `submit r_lglsca.wad_ps3` → BAD10 async PermA → loader `state=1 rem=0` → FREELIST `head=0`. O `rem=0` é o modo E397 (open síncrono não entrega o corpo), **não** o E402 (70 shells, arena `0x7A120`).

`patch_e401` está aplicado e o yield não dispara, mas o open do LglScA **não** reproduz o r3=1 / rem=`0xC00` do E402 neste binário/env (headless `PS3_NO_RSX=1`, `PS3_GCM_CB=1`, `PS3_MOVIE_DONE_MS=3000/9000`). O `boot_gow2` de produção também não (r1).

Próximo: recuperar o baseline de 70 shells (retorno de BAD10 #1, por que `rem=0` com o yield gated OFF) **antes** de repetir a pergunta 13/21.
