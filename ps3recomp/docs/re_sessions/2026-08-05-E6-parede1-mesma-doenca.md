# Sessão de RE — **E6**: `func_002545B0` (parede 1) é a mesma doença da parede 4?

Continuação do inventário da Fase 11 (`.planning/ROADMAP.md`, parede 1) e do critério 3
medido pelo E5 (`docs/re_sessions/2026-08-04-E5-aceite-parede-4.md`): com o fix v2 da
parede 4 aplicado e os gates OFF, o boot aborta **antes** de a parede 4 ser sequer
exercitada, dentro da própria `func_002545B0`, 6/6 corridas, `[ppu] FATAL: stuck calling
0x00514E80 (2000 times)`.

Nota operacional (recuperação de sessão): esta sessão caiu por erro de API antes de
produzir ledger. O trabalho de análise estática abaixo foi refeito e é o que fica
registado agora, ANTES de qualquer corrida de boot desta sessão. Regra nova: commitar
a cada achado, não guardar para o fim.

---

## O que já estava PROVADO antes desta sessão (não redescoberto aqui — só citado)

Fontes: `../gow2-recomp/notes/2026-08-01-a-parede-nova-nomeada-no-de-lista-com-payload-nulo.md`,
`../gow2-recomp/notes/2026-08-02-parede-1-duas-classes-um-tag.md`, e os três patches de
sonda já existentes (`recomp_mid_v2/patch_2545b0_entry_probe.py`,
`patch_254610_empty_list_gate.py`, `patch_254628_list_probe.py`).

1. **`func_002545D4` é um fragmento; a entrada real é `func_002545B0`** — confirmado
   desmontando o EBOOT.ELF (`stdu r1,-176(r1)`, quatro `std r28..r31`, `mr r28,r4`).
2. **`func_002545B0` é chamada exactamente 2 vezes por corrida.** #1: `arg=0x4063858C`,
   lista circular válida, `head=0x40007F34`. #2: `arg=0x407790D0`, `head=0x00000000` —
   entra em ciclo.
3. **`arg+0x7C` (= `0x4077914C`) é `matriz[1][0]`** de um objecto que `func_0024C1F8`
   (inicializador de matriz identidade, 33 instruções, sem ramos) escreveu como `0.0f` —
   confirmado por `PS3_WATCH_STORE`, escrita única e legítima.
4. **O mesmo layout de lista está certo para o caso #1** — não é bug do lifter no walk
   (`beq cr7 -> 0x00254644` bate com o PPC original), nem no prólogo (`func_002545B0`
   copia `r28=r4` correctamente antes de trampolinar).
5. **O mecanismo de mistura de famílias, achado por análise do walker do WAD**
   (`func_0024E1E8`/Ghidra `FUN_0024e198`):
   ```c
   r0  = *(uint16*)(obj + 2);           // low16 do cabeçalho (metade BAIXA)
   idx = (r0 << 2) & 0x3FFFC;
   fab = *(tab + idx);                   // tab = 0x00868D48
   fab->vt[0x18](fab, obj);
   ```
   Duas famílias dão `*(obj+2) = 1`: listas (`w0=0xC0010001`) e registos WAD
   (`w0=0x40030001`). A metade ALTA (`0xC001` vs `0x4003` — o subtag verdadeiro) não
   entra no índice. Registos WAD acabam despachados para a fábrica do tag 1
   (`this=0x400C61C8`), cujos métodos assumem layout de lista.
6. **`lr` capturado num `bctrl` é um artefacto do lifter, não fiável** — o próprio
   `ppu_loader` só escreve `ctx->lr` antes de `bl`; num `bctrl`/`ps3_indirect_call`
   fica preso no último `bl` directo. Isto já enganou uma ronda anterior (mandou para
   `func_0024D5BC`, refutado com `PS3_TRACE_D5BC`: 758 nós saudáveis).
7. **Existem sondas gated já instaladas no lift de produção** (não desta sessão, de
   2026-08-01/02, sobrevivem a `apply_all_patches.sh` via os três patches citados) MAIS
   duas sondas dentro do próprio corpo de `func_0024E1E8` no lift (`PS3_TRACE_TYPETAG` —
   imprime `obj`, `tag=*(obj+2)`, o `fab` resolvido, e um dump único da tabela de
   registry `[REGTAB]`; e `PS3_TRACE_B71` — traça os call-sites `func_0024D570` e
   `ps3_indirect_call` dentro desta função). Estas ainda não tinham sido corridas com o
   fix v2 da parede 4 já aplicado por baixo.
8. **Fix da parede 4 (v2) não resolve isto** — medido pelo E5, 6/6 corridas, com o fix v2
   aplicado e gates OFF: mesmo `FATAL`, mesma cadeia de retorno. Consistente com serem
   **duas instâncias distintas do mesmo padrão** em subsistemas diferentes: a parede 4
   está no typewalk de shaders (`func_0041FF70`/`func_0041F700`, endereços `0x0041xxxx`);
   a parede 1 está no acquire/pool de registos WAD (`func_0024E1E8`/`func_0024D570`,
   endereços `0x0024xxxx`) — **não é o mesmo container**, é o mesmo padrão em dois sítios.

## O que esta sessão NÃO deve assumir sem medir (aviso do coordenador)

Ao investigar o vídeo a tocar duas vezes noutra frente, mediu-se que o primeiro
`cellVdec StartSeq` é **estéril** — zero `DecodeAu` entre ele e o segundo, ambos abrindo
o mesmo ficheiro. Padrão desta base de código: uma chamada que acontece, reporta
sucesso, e não produz trabalho. Consequência aqui: não assumir que os 2 `[E545B0]` por
corrida (achado #2 acima) são as ÚNICAS visitas relevantes, nem que a contagem de
"quantos objectos WAD passam pelo dispatch de `func_0024E1E8`" é 1 só porque só se viu 1
`arg` problemático — **isto tem de ser contado nesta sessão**, não herdado por analogia
da parede 4 (lá a contagem também deu `n=1`, mas são censos de sítios diferentes).

---

## Pergunta (uma frase)

> **No dispatch de `func_0024E1E8` (`ctx->gpr[21]` = objecto entregue a
> `func_002545B0`), o `w0` COMPLETO (`vm_read32(gpr[21]+0)`) lido nesse ponto, para a
> chamada que crasha, é `0x40030001` — confirmando in-boot, nesta sessão, que é a MESMA
> classe de bug da parede 4 (dispatch por `low16` sozinho, ignorando o `subtag` nos 12
> bits altos) — e quantos objectos no total, ao longo de uma corrida completa, sofrem
> esta má-classificação neste sítio específico?**

## Âncoras (reaproveitadas, verificadas nesta sessão por leitura do lift)

| âncora | valor |
|---|---|
| lift base | `../gow2-recomp/recomp_macos_e6wall1/` (cópia de `recomp_macos_e4fix2`, que já tem o fix v2 da parede 4 — `apply_all_patches.sh` corrido nesta sessão, `PASS` em `lift_parity`/`MANIFEST`/`audit_boundaries`) |
| entrada real da parede 1 | `func_002545B0` (`func_002545D4` é fragmento, +36 bytes) |
| dispatcher suspeito | `func_0024E1E8` (nome do lift) = `FUN_0024e198` (nome Ghidra, `.opd`, size=960) |
| chamada intermédia (não o culpado, já refutado 2026-08-01) | `func_0024D5BC` (758 nós saudáveis, `PS3_TRACE_D5BC`) |
| sonda já instalada #1 | `PS3_TRACE_TYPETAG` (+ `PS3_TRACE_TYPETAG_CAP`) — imprime `obj`, `tag=*(obj+2)`, `fab`, dump `[REGTAB]` único |
| sonda já instalada #2 | `PS3_TRACE_B71` (+ `PS3_TRACE_B71_CAP`) — traça os 4 call-sites nesta função (`func_0024D570`, `ps3_indirect_call` ×2) |
| sonda já instalada #3 | `PS3_TRACE_E545B0` (+ `_CAP`) — entrada de `func_002545B0`: `this`, `arg`, `lr`, `sent=arg+0x7C`, `head=*(sent)` |
| sonda já instalada #4 | `PS3_TRACE_LIST254` (+ `_CAP`) — nó a nó do walk interno de `func_002545D4` |
| gate de diagnóstico (não-fix) | `PS3_LIST254_EMPTY_IF_NULL` |
| predicado de poda da parede 4 (candidato a reaproveitar) | `tools/test_wall4_subtag_fix.py::prune(w0)` = `(w0&0xFFFF)==1 and ((w0>>16)&0xFFF)!=1` |
| tabela de registry | `tab = 0x00868D48` |
| `this` da fábrica errada (tag 1) | `0x400C61C8` |

---

## Previsões — ESCRITAS ANTES DE CORRER O BOOT NESTA SESSÃO

### P1 — confirma a mesma doença, sítio novo

Com `PS3_TRACE_TYPETAG=1 PS3_TRACE_B71=1 PS3_TRACE_E545B0=1` numa corrida de 90s:

- O `[TYPETAG]` mostra `tag=1` (o `*(obj+2)` já sabido) e um `[REGTAB]` com `fab=0x400C61C8`
  no índice `idx=0x0004` (tag 1) — confirmando que é ESTA tabela/fábrica que a nota de
  2026-08-02 já tinha nomeado.
- Uma leitura ADICIONAL nesta sessão (`vm_read32(obj+0)`, o `w0` completo, que o
  `[TYPETAG]` existente NÃO imprime — só imprime `*(obj+2)`) dá **`0x40030001`** para a
  chamada que corresponde ao `[E545B0] #2` (o `arg` problemático).
- `[B71]` mostra a sequência `func_0024D570` → `ps3_indirect_call`, sem passar por
  `func_0024D5BC` — fechando definitivamente a pista errada de 2026-08-01.
- **Se isto bater**, o mecanismo é: `func_0024E1E8` (pool acquire) obtém um objecto cujo
  `w0` é `0x40030001` (registo WAD, `subtag=3`), calcula `idx` só do `low16=1`, resolve
  `fab=tab[idx]=0x400C61C8` (fábrica de LISTAS), e invoca `fab->vt[0x18](fab, obj)` =
  `func_002545B0(0x400C61C8, obj)` — o mesmo padrão exacto da parede 4, com `prune(w0)`
  a identificá-lo correctamente (`(0x40030001&0xFFFF)==1` e `((0x40030001>>16)&0xFFF)==3
  != 1` ⇒ `prune=True`).

### P2 — o `w0` não é `0x40030001`, ou a contagem é > 1

Se o `w0` lido directamente for outro valor, ou se houver MAIS do que um objecto
mal-classificado a passar por este dispatch numa corrida (contagem > 1), a hipótese de
"filtro cirúrgico único, igual à parede 4" fica mais fraca — pode ser preciso um filtro
mais amplo, ou o objecto pode não vir de `func_0024E1E8` sozinho (pode haver mais de um
produtor). Neste caso registo a contagem exacta e decido entre A) ainda um filtro no
mesmo sítio, cobrindo N casos, ou B) reabrir para outro dispatcher.

### Nem P1 nem P2 — refutação total

Se `[B71]`/`[TYPETAG]` não dispararem de todo com estes gates (ex.: a função não é
alcançada por este caminho nesta build, ou o `ps3_indirect_call` que dispara não é o
mesmo que entrega `arg=0x407790D0` a `func_002545B0`), a atribuição de 2026-08-01 ao
`func_0024E1E8` fica em aberto outra vez, e a próxima sonda tem de procurar o
call-site pelo MESMO método (`PS3_TRACE_E545B0` na entrada, nunca o `lr` do `bctrl`).

<!-- CUT LINE — nada acima desta linha é reescrito depois de correr o boot -->

---

## Medição (Task 1) — 3/3 corridas, `boot_gow2_e6wall1`

Lift dedicado `../gow2-recomp/recomp_macos_e6wall1/` (cópia de `recomp_macos_e4fix2` +
`apply_all_patches.sh` `PASS` + a sonda nova
`recomp_mid_v2/patch_24e2d4_typetag_w0_probe.py`, 8/8 sítios do fragmento aplicados).
Binário dedicado `../gow2-recomp/boot_gow2_e6wall1` (`build_macos.sh recomp_macos_e6wall1`,
0 erros). `pgrep -fl boot_gow2` vazio antes e depois das 3 corridas — zero órfãos.

Env: `PS3_NO_RSX=1` (headless, sem depender do backend gráfico) +
`PS3_TRACE_TYPETAG=1 PS3_TRACE_TYPETAG_W0=1 PS3_TRACE_TYPETAG_CAP=-1
PS3_TRACE_B71=1 PS3_TRACE_E545B0=1`, resto do env canónico (`env_gow2.sh`).

| corrida | `[TYPETAG-W0]` | `seen` | `would_prune` | `[E545B0]` correspondente | `FATAL` |
|---|---|---:|---:|---|---|
| 1 | `obj=0x4077AC10 w0=0x40030001 low16=0x0001 subtag=0x003 prune=1` | 1 | 1 | `arg=0x4077AC10 lr=0x0024E2D4 head=0x00000000` | sim, 2000× |
| 2 | `obj=0x4077AC10 w0=0x40030001 low16=0x0001 subtag=0x003 prune=1` | 1 | 1 | `arg=0x4077AC10 lr=0x0024E2D4 head=0x00000000` | sim, 2000× |
| 3 | `obj=0x4077AC50 w0=0x40030001 low16=0x0001 subtag=0x003 prune=1` | 1 | 1 | `arg=0x4077AC50 lr=0x0024E2D4 head=0x00000000` | sim, 2000× |

**Idêntico nas três** (o endereço varia ligeiramente entre corridas — variação normal do
heap — mas `w0`, `subtag`, `low16`, `prune` e a contagem `seen=1/would_prune=1` não
variam). **P1 confirmado, número a número.**

### Controlo — prova de que a leitura está no sítio certo

- O `[E545B0]` #0 SAUDÁVEL (`arg=0x4063858C`, `lr=0x0004200C`) aparece nas 3 corridas
  ANTES do `[TYPETAG-W0]`/`[REGTAB]`, com um `lr` completamente diferente — **não passa
  por este sítio** (controlo negativo: o dispatch de `func_0024E1E8` não é a única via de
  entrada em `func_002545B0`, e a via saudável não o usa).
- O `[E545B0]` #0 PATOLÓGICO (`arg=0x4077AC10`/`0x4077AC50`, `lr=0x0024E2D4`) casa, EA a
  EA, com o `obj` do `[TYPETAG-W0]` imediatamente anterior nas 3 corridas — controlo
  positivo: é o MESMO objecto, medido em dois pontos independentes do código.
- `[TYPETAG]` (a sonda irmã, já existente) mostra `tag=1 idx=0x00004 fab=0x400C5048
  fabvt=0x00515AA0 code=0x0039D428` — **não é directamente `this=0x400C61C8`**; é um
  nível de indirecção a mais do que a nota de 2026-08-02 assumia: `func_0024E1E8`
  despacha para uma fábrica de tipos (`func_0039D428`), que só depois cascata para
  `func_0039D764` → `func_002545B0` (confirmado pelo `[B71]` rbp frame walk das 3
  corridas: `#4 func_0039D428 → #2 func_0039D764 → #0 func_002545D4`, idêntico ao já
  registado pelo E5). Não muda o mecanismo (é a mesma classificação por `low16` só, no
  mesmo `tab=0x00868D48`), mas corrige um detalhe da nota anterior: o `this` errado que
  entra no funil é `fab=0x400C5048` (a fábrica genérica do tag 1), não `0x400C61C8`
  directamente.
- `[REGTAB]` confirma a tabela: `tag=1 idx=0x0004 fab=0x400C5048` — coerente em todas as
  3 corridas.

### Censo (responde à pergunta): **`seen=1, would_prune=1` em 3/3 corridas**

Ao longo de cada corrida inteira (até ao abort), o dispatch de `func_0024E1E8` só é
exercitado **UMA vez** neste caminho, e essa única vez É o caso patológico. Não há
nenhum objecto legítimo a passar por este sítio nesta janela de medição — o censo não
encontrou nenhum caso que um filtro aqui pudesse podar incorrectamente. Isto responde à
ressalva do coordenador (não assumir a contagem por analogia): a contagem foi MEDIDA
nesta sessão, neste sítio, e dá `n=1`, tal como a parede 4 — mas por medição própria, não
por herança.

### O que fica CONFIRMADO por medição (P1, 3/3)

1. O objecto entregue a `func_002545B0` na chamada patológica tem `w0=0x40030001`
   (`subtag=3`, `low16=1`) — o MESMO padrão de bits da parede 4.
2. O predicado `prune(w0) = (low16==1 and subtag!=1)` de
   `tools/test_wall4_subtag_fix.py` classifica correctamente este caso como `prune=1`,
   SEM alteração — é literalmente a mesma doença, medida num sítio diferente.
3. O sítio de entrada no funil é `func_0024E1E8` (nome do lift; Ghidra `FUN_0024e198`),
   no dispatch imediatamente antes de `ps3_indirect_call` que resolve `fab=tab[low16(obj)]`
   e invoca `fab->vt[0x18](fab, obj)`.
4. `func_0024D5BC` continua irrelevante (não aparece em nenhuma das 3 corridas com
   `PS3_TRACE_B71` ligado) — a atribuição de 2026-08-01 mantém-se refutada.
5. O `lr` capturado por um `bctrl`/`ps3_indirect_call` continua a ser um artefacto do
   lifter (`lr=0x0024E2D4` nas 3 corridas do caso patológico é o `lr` do ÚLTIMO `bl`
   antes da cascata de `ps3_indirect_call`s, não o call-site directo) — confirmado de
   novo, não uma descoberta desta sessão.

---

---

## RESULTADO — a parede 1 está QUEBRADA (medido 2026-08-05, gates OFF)

Binário `boot_gow2_p1fix`, construído com `FORCE_REBUILD_LIFT=1` a partir de
`recomp_macos_e6fix` (8 marcadores `PAREDE1-SKIP`). Os três gates de diagnóstico
(`PS3_LIST254_EMPTY_IF_NULL`, `PS3_POOL_NULL_IF_BAD`, `PS3_LIST547_EMPTY_IF_BAD`)
confirmados **unset** dentro de cada corrida.

| corrida | `FATAL 0x00514E80` | `st620` | `[STATE] frame` | `startseq` | linhas |
|---|---|---|---|---|---|
| 1 | **0** | 11 | 0 | 1 | 2872 |
| 2 | **0** | 11 | 0 | 1 | 2752 |
| 3 | **0** | 11 | 0 | 1 | 1609 |

**Corridas sem o `FATAL`: 3 de 3.** Referência de 2026-08-04 (E5, mesmo gate OFF):
**0 de 6** — o `FATAL: stuck calling 0x00514E80` abortava todas. Zero órfãos.

⇒ **VEREDICTO: QUEBRADA.** O aborto desapareceu; o boot passa a chegar a
`[CONTENT] boot logo queue DONE — hold cleared (guest/menu)` e continua vivo.

### A parede seguinte, nomeada pela própria medição

O sintoma mudou de **aborto** para **stall**: o boot já não morre, fica a girar no sampler
com `st620=11 -> 11` e `overlay_done=1` depois de o logo terminar. O `[STATE] frame=1`
continua a **0** nas três corridas — o loop principal ainda não despacha o primeiro estado.
Uma parede que mata foi substituída por uma que espera, o que é progresso, e é o próximo alvo.

### DUAS COISAS NÃO MEDIDAS — não tratar como resolvidas

1. **`startseq` caiu de 2 para 1.** A referência de ontem tinha duas sequências de vídeo;
   agora há uma. Pode ser consequência legítima do fix (o caminho que produzia a segunda
   passava pelo objecto mal classificado que agora é podado) **ou** uma regressão (o segundo
   movie deixou de acontecer). **Não foi medido qual.** É a primeira coisa a verificar.
   Nota: se for legítimo, é também a explicação do vídeo observado a tocar duas vezes.
2. **A corrida 3 tem 1609 linhas contra ~2800 das outras.** Variação grande demais para
   ignorar. Pode ser a flakiness de timing conhecida, mas o critério (tamanho do log e
   presença do `allocate(0x7D00000)`) **não foi confirmado** nesta corrida.

### Achado lateral, do mesmo dia

Ao investigar o vídeo a tocar duas vezes mediu-se que o **primeiro `cellVdec StartSeq` é
estéril**: zero `DecodeAu` entre ele e o segundo, e os dois abrem o **mesmo** ficheiro
(`SmLogo_v2.m2v` e `smlogo_v2.m2v`, diferem só em maiúsculas). O gate contava `startseq=2`
como PASS — contava uma chamada, não trabalho feito.
