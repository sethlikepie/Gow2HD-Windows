# O jogo NÃO está preso: o main loop corre. A parede é `tab[0x58]` — o tipo WAD aponta para dados

**Data:** 2026-08-01 · Medido em `boot_gow2.jtfix` com a recipe do gate
(`arm_menu_fast_recipe`, Metal REAL — não `PS3_NO_RSX`), 3 corridas por sonda, kill
sempre por PID. **Nenhum rebuild: todas as sondas usadas já estavam no binário.**

## A correcção que muda o enquadramento todo

A nota de 07-31 (`onde-a-main-para-pos-auto-load…`) concluiu, do backtrace lldb, que a
main estava num *"loop vivo sem saída"* dentro de `func_002B2E04`/`func_00242C94`, e que
*"o menu nunca funcionou neste projecto"*.

**O backtrace estava certo. A leitura do código estava errada em dois pontos.**

### 1. Os dois sinais contraditórios eram ambos verdadeiros

| sinal | valor | |
|---|---|---|
| `2B2E04-PROBE#P1` (sonda no lift) | **0 linhas em 3/3** | o corpo pós-`func_00242C94` nunca corre |
| `func_002B2E04` no backtrace lldb | **5/5 amostras** | está na pilha |

Não há contradição: `func_002B2E04` chama `func_00242C94` **uma vez, antes** do seu
próprio laço, e as três sondas `P1/P2/P3` estão todas **depois** dessa chamada. A main
entra em `func_002B2E04`, entra em `func_00242C94` e **não volta** — por isso está na
pilha e por isso as sondas nunca disparam.

### 2. `func_00242C94` não é uma parede — é o MAIN LOOP do jogo

A nota anterior escreveu que ele *"só sai quando os DOIS bytes de flag do nó estão a
zero"*. É **ao contrário**: o teste lifted é `if (byte == 0) goto corpo`, ou seja
**continua enquanto o byte for zero** e só sai quando os dois forem **não-zero**:

```c
loc_00242CFC:                              // topo
    func_00194D3C(*(r31));                 // cellSysutilCheckCallback + cellPadGetInfo2
    r9 = *(r31);
    if (*(uint8*)(r9+4) == 0) goto corpo;   // continua enquanto ==0
    if (*(uint8*)(r9+5) == 0) goto corpo;
    *(*(TOC-0x244C)) = 1;  return;          // só aqui sai
corpo:                                      // UM FRAME
    ps3_indirect_call(*(r30+0x460C));       // o tick instalado
    func_00262C64(...); func_001E54E0(..., dt); func_00262C8C();
    goto loc_00242CFC;
```

E quem põe `*(r9+4)` a 1 é o **callback do cellSysutil** (`func_00194DCC`), no ramo
`func_00194E54`, quando o evento é **`0x101` = `CELL_SYSUTIL_REQUEST_EXITGAME`** (os
outros ramos tratam `0x121`/`0x122` = `DRAWING_BEGIN`/`DRAWING_END`). O mesmo byte `+4`
é a condição de saída do laço de shutdown `func_00194FFC`.

**Traduzindo: `func_00242C94` é `while (!quitRequested) { runOneFrame(); }`. Correr para
sempre é o comportamento CORRECTO.** Não há nada a "desbloquear" ali.

## O main loop corre mesmo — medido

`PS3_TRACE_POSTTHR=1`, 3 corridas, contadores do `[POSTTHR] SUMMARY`:

| fn | run 1 | run 2 | run 3 | |
|---|---:|---:|---:|---|
| `2B2DD0` (tick de frame) | **2001** | **2001** | **2001** | o frame corre |
| `B951C` (update) | 2000 | 2000 | 2000 | |
| `CC9D0` | 4000 | 4000 | 4000 | |
| `grand` (total da sonda) | 20553 | 19717 | 19358 | varia — não é cap global |

E o `lr` da primeira entrada nomeia o sítio sem ambiguidade:

```
[POSTTHR] enter fn=2B2DD0 tot=1 post=0 r3=0x00000000 lr=0x00242D04
```

`0x00242D04` é o `lr` que o lift põe imediatamente antes de `func_00194D3C` **dentro de
`func_00242C94`** — confirma que o tick é despachado indirectamente pelo corpo do main
loop (o único chamador *directo* de `func_002B2DD0` em todo o lift é
`func_002B2E04:36435`, que já provámos nunca ser alcançado).

`func_002B2DD0` = `{ func_002B2660 (despacho virtual), func_000B951C (update),
func_002B7188 (timer/dt) }`.

**O `2001` idêntico em 3/3 fica POR EXPLICAR** (o `grand` varia nas mesmas corridas, e o
sumário intermédio a `grand=20000` mostrava `tot=1862`, ou seja ainda a crescer). Não é
o disjuntor `PS3_STUCK_ICALL_LIMIT` — nenhuma corrida imprimiu `FATAL: stuck calling`.
Experiência discriminadora óbvia e ainda não feita: repetir com `timeout` diferente
(45 s) — se der 2001 outra vez é limite duro, se der ~1000 é proporcional ao tempo.

## O `SetFlip` pára ANTES do `thr_auto_load`, não depois

A nota de 07-31 diz "0 flips depois do `thr_auto_load() end`". Verdade, mas o corte é
mais cedo: o **último `SetFlipCommand` está na linha 3725** e a abertura do
`r_lglsca.wad_ps3` na **3728**. O `thr_auto_load() end` só aparece na **4187**.
Os ~2250 flips são todos da intro; o desenho pára quando o **carregamento do WAD**
começa, não quando o AutoLoad acaba.

## A parede real: `tab[0x58]` — o tipo do WAD aponta para dados comprimidos

Com `PS3_TRACE_TYMAP=1` o walk do typemap **acontece e é real** — 224 entradas
`[WADLD-T1]`, com os nomes verdadeiros dos subsistemas do jogo:

```
Master_Server WadServer ProServer GOServer AnimServer BhvrServer EvtServer
CollisionServer ScriptServer SoundServer TextureServer MatServer LightServer
CameraServer EffectsServer EpiServer GfxClutServer WaypointServer
renMasterSvr renPrimMaster renEEPrimSvr renFlashServer renModelServer renParticleSvr
SCRX_R_Perm LGTX_R_Perm TXRX_R_Perm MATX_R_Perm CAMX_R_Perm GFXX_R_Perm
GFX_SCREEN_00 PAL_SCREEN_00 MAT_stencil MAT_trail1 ...
```

`result=0 / hit=0` em todas é **normal** — é o "ainda não registado", e o caminho segue
para a fábrica de tipo (`func_002B0FB4`), que resolve **128 vezes com OPD válido**:

```
[WADLD-VT28] #2 obj=0x400C3D48 vt=0x00516F70 opd=0x0052C8B0 code=0x00423C98
```

**Excepto dois nomes:**

```
[WADLD-T1SZ] #28 name='WAD_R_LglScA' u0=32768 u2=22 w0=0x80000016 idx=0x58 tab=0x00868D48 obj=0x400C3D88
[WADLD-T1SZ] #49 name='WAD_R_Perm'   u0=32768 u2=22 w0=0x80000016 idx=0x58 tab=0x00868D48 obj=0x400C3D88
```

`idx = u2*4 = 0x16*4 = 0x58`, e `w0 = 0x80000016` — o **mesmo padrão de bit alto** que o
`[TYPE15] CB56C type high-bit 0x15 -> 0x80000015 (match WAD path)`. Este é o tipo
contentor do próprio WAD, o vizinho imediato do TYPE15 na tabela (`tab[0x54]` vs
`tab[0x58]`).

E o objecto para onde `tab[0x58]` aponta **não é um objecto** — com
`PS3_TRACE_ICALL_BAD_MEM=1`:

```
[ICALL-BAD-MEM] r3 base=0x400C3D88 words16:
   9102FFFF 07009502 B90B9002 FFFF0800 9502BA0B 8F02FFFF 09009502 BB0B8E02 ...
```

São **dados de stream comprimido**, não uma vtable. E a aritmética fecha exactamente:
a primeira palavra `0x9102FFFF` é usada como ponteiro de vtable, e o log traz

```
[vm] UNCOMMITTED read32 access 0x91030027   (= 0x9102FFFF + 0x28)
[vm] UNCOMMITTED read32 access 0x91030047   (= 0x9102FFFF + 0x48)
[ICALL-BAD] ctr=0x40637408 lr=0x002BACE4 r2=0x406373F8 r3=0x400C3D88 r11=0x400C3D88 r12=0x40004024
```

`+0x28` é precisamente o slot que `func_002B0FB4` lê (`opd = *(vt+0x28)`).

### Onde estão os call sites (o `lr` do lift é fiável aqui)

| `lr` | função | contexto |
|---|---|---|
| `0x002B0EBC` | `func_002B0E78` | o próprio sítio da sonda `[WADLD-T1]` — o walk do typemap |
| `0x002B2BDC` | `func_002B2BA0` | lookup de membro chamado por `func_002B0E78` |
| `0x002BAC50`, `0x002BACE4` | `func_002BAB88` | consumidor a jusante |

## O que fica estabelecido

- **MEDIDO (3/3):** o main loop do jogo corre; `func_002B2DD0` entra 2001 vezes,
  despachado indirectamente por `func_00242C94`. Não há deadlock nem spin morto.
- **MEDIDO (3/3):** `func_00242C94` só sai em `REQUEST_EXITGAME`; correr para sempre é
  o contrato.
- **MEDIDO (2/2):** o walk do typemap corre inteiro, 224 nomes reais, 128 fábricas de
  tipo resolvidas com OPD válido.
- **MEDIDO (2/2):** `tab[0x58]` (tipos `WAD_R_LglScA` / `WAD_R_Perm`, `w0=0x80000016`)
  aponta para `0x400C3D88`, que contém dados de stream; `*(0x400C3D88)=0x9102FFFF` é
  usado como vtable e os `UNCOMMITTED` em `+0x28`/`+0x48` batem certo ao byte.
- **MEDIDO:** o `SetFlip` pára na abertura do 1.º WAD (linha 3725→3728), não no
  `thr_auto_load end` (linha 4187).
- **INFERIDO, não observado:** que `0x400C3D88` já foi um objecto válido e foi
  **sobrescrito** pelos dados do WAD. Suporta-o o facto de `tab[0x14]=0x400C3D48`
  (0x40 bytes antes) ainda ter `vt=0x00516F70` válido, e de a fábrica #2 ter devolvido
  `r3=0x400C3D8C` — 4 bytes depois do endereço agora corrompido. Ninguém viu o byte mudar.
- **POR EXPLICAR:** o `2001` idêntico em 3/3.
- **POR VERIFICAR:** se `boot_gow2.pre_v3` (a referência) tem a mesma corrupção em
  `tab[0x58]` — decide se isto é regressão ou defeito antigo.

## O watch de store: o escritor NÃO passa por `vm_write*`

`PS3_WATCH_W32=0x400C3D88,0x00868DA0` — **8 escritas, byte a byte idênticas em 2/2
corridas**, e são exactamente o ciclo de vida do objecto:

```
[0x00868DA0]=0x00000000  ra0=func_002B2998+0x180   ra1=func_002B2EEC+0xD0   <- tab[0x58] a zero
[0x400C3D88]=0x40083D04  ra0=func_00263178+0x9CC   ra1=func_002B28BC+0x120  <- allocator
[0x400C3D88]=0x00511628  ra0=func_002B11B8+0x757C  ra1=ps3_indirect_call    <- ctor base
[0x400C3D88]=0x005116E8  ra0=func_002B11B8+0x7670                           <- ctor
[0x400C3D88]=0x00517198  ra0=func_002B11B8+0x8244                           <- ctor
[0x400C3D88]=0x005170B8  ra0=func_002B11B8+0x8344                           <- vtable FINAL
[0x00868DA0]=0x400C3D88  ra0=func_0042B078+0x1A4                            <- publica em tab[0x58]
```

A sequência `0x511628 → 0x5116E8 → 0x517198 → 0x005170B8` é o idioma clássico de
atribuição de vptr ao longo da cadeia de herança em C++. **E depois nada.**

Como o `PS3_WATCH_W32` só vive dentro de `vm_write32`, isso podia ser um store de 8/16/64
bits. Foi acrescentado um `PS3_WATCH_STORE` (commit `fc2bddc` no ps3recomp) no `heap_w()`
— o único ponto por onde passam `vm_write8/16/32/64` — com disparo por **sobreposição de
intervalo**. Resultado, 2/2 corridas (uma delas com `thr_end=1`):

```
WATCHSTORE total=8   -- as MESMAS 8, todas w32, nenhuma com o veneno
[WADLD-VT28] #28 obj=0x400C3D88 vt=0x005170B8   (mesmo log)
[WADLD-VT28] #49 obj=0x400C3D88 vt=0x9102FFFF   (mesmo log)
```

**MEDIDO: o stomp não passa por nenhum `vm_write*`.** Logo é uma escrita **HOST directa
em `vm_base`**. Os candidatos, enumerados por grep no motor e no port:

| sítio | ficheiro |
|---|---|
| `movie_io_pread(…, vm_base + base + avail, n, …)` | `gow2-recomp/host_gow2_f2b.c:212` (o pump do stream do WAD) |
| `memmove/memcpy` da compactação do ring | `host_gow2_f2b.c:166,178` |
| `memcpy(vm_base + dst_ea, bounce, got)` | `libs/video/fios_aread_hle.c:136` (AREAD HLE) |
| `memcpy(ea_ptr, ls_ptr, size)` — **PUT de SPU** | `runtime/spu/spu_dma.h` |
| `fread(vm_base + buf, …)` | `runtime/ppu/ppu_fs.cpp:427` (cellFsRead) |

Geometria que torna dois deles plausíveis: o ring do stream é
`base=0x40083D40 cap=262144` → acaba em `0x400C3D40`, e os objectos de tipo foram
alocados **logo a seguir** (`0x400C3D48`, `0x400C3D88`). Um overrun de ≥0x48 bytes
aterra em cheio no alvo. E o conteúdo que lá fica (`9102FFFF 07009502 B90B9002 …`) tem
mesmo cara de payload comprimido.

Os cinco sítios foram instrumentados com `ps3_watch_store_bulk(ea, len, who)` — mesma
env var, etiqueta `[WATCHSTORE] BULK`, chamada **depois** da escrita para imprimir o
valor que ficou no alvo. Medição a correr.

## O `2001` é limite duro — e é o NOSSO disjuntor (resolvido)

Repetida a corrida com `timeout=45 s` em vez de 90 s: `2B2DD0 tot=2001` **outra vez**.
Não é proporcional ao tempo.

E a explicação estava no fim dos mesmos logs, escondida por um `head -5` meu:

```
[ppu] FATAL: stuck calling 0x40678C90 (2000 times) -- aborting run
```

É o `PS3_STUCK_ICALL_LIMIT` do `ppu_loader.cpp` (default **2000**). Cada frame do main
loop faz uma chamada indirecta não-resolvida para o **mesmo** alvo `0x40678C90`; ao fim de
2000 repetições idênticas o motor faz `exit(3)`. Daí `2B2DD0 tot=2001` — 2000 abortadas
mais a que estava em curso — igual ao byte em 3/3 corridas e independente do timeout.

**Não é um limite do jogo nem uma parede nova: é o nosso circuit-breaker a matar o
processo**, e o `0x40678C90` é a mesma assinatura já registada na nota `gate6-0x40678c90`.

Lição de método, outra vez a mesma: um `grep ... | head -5` escondeu a linha que fechava
a questão. Quando um número não faz sentido, ler o **fim** do log inteiro antes de
construir hipóteses.

## Lição de método (a terceira vez que esta sessão a paga)

Duas medições que se contradizem podem estar **ambas certas** — a contradição estava na
minha leitura do código, não nos dados. Antes de escolher entre elas, valeu mais reler o
lift e perguntar *"onde exactamente está a sonda em relação à chamada?"*.
