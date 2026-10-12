# CAUSA RAIZ: o nosso `F2B-STREAM-PUMP` escreve 20 MB numa janela hard-coded e passa por cima dos objectos de tipo do jogo

**Data:** 2026-08-01 · **MEDIDO em 2/2 corridas, byte a byte iguais, incluindo o valor
final do veneno.** É o achado desta sessão, e é um defeito **nosso**.

## A linha que fecha o caso

```
[WATCHSTORE] BULK who=movie_io_pread fread [0x400C0000,+131072) cobre alvo 0x400C3D88 -> agora 0x9102FFFF
```

`0x9102FFFF` é exactamente o valor que o `[WADLD-VT28] #49` lê como vtable do tipo do WAD,
e de onde saem os `[ICALL-BAD] ctr=0x40637408` e os `UNCOMMITTED` em `0x91030027`/`0x91030047`
(= `0x9102FFFF+0x28` / `+0x48`).

## O código, com os números todos

O bloco `F2B-STREAM-PUMP` (instalado no lift por
`recomp_mid_v2/patch_fios_f2b_open_block_install.py`, corpo de `func_002B4274`) tem isto:

```c
uint32_t ring    = 0x40080000u;
uint32_t ring_sz = 0x00100000u;      /* 1 MiB ring  <- HARD-CODED */
uint32_t pos = 0, chunk = 0x20000u;  /* 128 KiB */
while (pos < _sz && nchunk < 400) {
    uint32_t n = _sz - pos; if (n > chunk) n = chunk;
    uint32_t dst = ring + (pos % ring_sz);
    if (dst + n > ring + ring_sz) n = ring + ring_sz - dst;
    movie_io_pread(_mfd, vm_base + dst, n, pos);   /* escrita HOST directa */
    total += got; pos += got; nchunk++;
}
```

`_sz` é o tamanho do ficheiro. Para o `r_perma.wad_ps3` isso são **20 169 344 bytes**, que o
pump despeja em **154 chunks** de 128 KiB dentro de `[0x40080000, 0x40180000)`, dando
**20 voltas** à janela de 1 MiB. Confirmado no log:

```
[FIOSOPEN] F2B-STREAM-PUMP fo=0x430189D8 mfd=0x4D560003 pumped=20169344/20169344 chunks=154
```

E a janela **não é dele**. O ring verdadeiro do guest, lido do próprio objecto de stream
pelo `f2b_stream_fill` (`host_gow2_f2b.c`), é:

```
[FIOSOPEN] F2B-STREAM-FILL stream=0x4007FCD0 base=0x40083D40 avail=262144 ...
```

| | início | fim | tamanho |
|---|---|---|---|
| ring **real** do guest (`base`/`cap` lidos do objecto) | `0x40083D40` | `0x400C3D40` | 256 KiB |
| janela **hard-coded** do pump | `0x40080000` | `0x40180000` | 1 MiB |

O pump escreve **0xBC2C0 bytes (770 KiB) para lá do fim do ring real**, em cheio no heap do
guest. E é lá que vivem os objectos que a fábrica de tipos acabou de construir:

| objecto | endereço | tipo |
|---|---|---|
| `tab[0x14]` — `WadServer` e os 20 outros `*Server` | `0x400C3D48` | dentro da janela |
| `tab[0x58]` — `WAD_R_LglScA` / `WAD_R_Perm` (`w0=0x80000016`) | **`0x400C3D88`** | dentro da janela |

## A cadeia causal completa, cada elo medido

```
1.  ctor constroi o objecto de tipo do WAD e escreve a vtable
      [WATCHSTORE] w32 [0x400C3D88]=0x005170B8  ra0=func_002B11B8+0x8344
2.  func_0042B078 publica-o em tab[0x58]
      [WATCHSTORE] w32 [0x00868DA0]=0x400C3D88
3.  1o WAD (r_lglsca, 3 KB): a fabrica resolve BEM
      [WADLD-VT28] #28 obj=0x400C3D88 vt=0x005170B8 opd=0x0051B2F8 code=0x0039E6B4
4.  abre o r_perma (20 MB) -> o NOSSO pump despeja 20 MB em [0x40080000,+1MiB)
      19 x [WATCHSTORE] BULK who=movie_io_pread fread [0x400C0000,+131072)
      ... a ultima deixa 0x9102FFFF na palavra 0 do objecto
5.  2o WAD: a fabrica resolve para lixo
      [WADLD-VT28] #49 obj=0x400C3D88 vt=0x9102FFFF opd=0x00000000 code=0x00000000
6.  o walk do typemap despacha atraves do lixo
      [ICALL-BAD] ctr=0x40637408 lr=0x002B0EBC r3=0x400C3D88
      [vm] UNCOMMITTED read32 access 0x91030027   (= 0x9102FFFF + 0x28)
```

## Como se chegou aqui (e porque não se chegou antes)

O `PS3_WATCH_W32` do motor só vive dentro de `vm_write32`. Foi acrescentado um
`PS3_WATCH_STORE` no `heap_w()` — o ponto por onde passam `vm_write8/16/32/64` — e **também
não apanhou nada**: 8 escritas, todas da construção, em 2/2 corridas.

Isso foi o que resolveu o caso: **provou que o escritor não é o guest**. Um `pread` host
escreve directamente em `vm_base + dst` e é invisível a qualquer watch que viva nos
acessores. Foram então instrumentados os cinco sítios do motor+port que escrevem host-directo
em `vm_base` (`ps3_watch_store_bulk`, commit `92c6705`), e o primeiro disparo nomeou o
culpado.

**Lição:** um watch que "não dispara" não prova que ninguém escreve — prova que ninguém
escreve *por aquele caminho*. A pergunta a fazer a seguir é sempre "que caminhos é que este
watch não vê?".

## O gate para desligar já existia

O próprio bloco tem `PS3_FIOS_STREAM_PUMP=0`. A experiência discriminadora está a correr:
com o pump desligado, o critério é `[WADLD-VT28] #49 vt=0x005170B8` (em vez de `0x9102FFFF`)
e zero `[ICALL-BAD] ctr=0x40637408` — sem regressão nos elos a montante
(`r_perma bytes_read=20169344`, `startseq=2`, `thr_auto_load end`).

## O fix, quando a experiência confirmar

O pump existe porque *"Guest never reaches 002B3D1C for WAD (freelist desync first)"*. Mas:

1. Escrever 20 MB numa janela de 1 MiB significa que **só o último MiB sobrevive** — o resto
   do trabalho é deitado fora de qualquer maneira.
2. A janela é **hard-coded** e não pertence ao pump; o `f2b_stream_fill`, chamado
   imediatamente a seguir, faz a coisa certa: lê `base` e `cap` do próprio objecto de stream
   do guest e enche só `[base, base+cap)`.

O fix mínimo e fiel é **o pump passar a ler `base`/`cap` do objecto de stream**
(`st = *(type_sys+0x1A8)`, `base = *(st+0)`, `cap = *(st+0xC)`) em vez das constantes, e
nunca escrever fora desse intervalo — exactamente a disciplina que o `f2b_stream_fill` já
tem. Vira `patch_*.py` idempotente, como manda o CLAUDE.md.

## O fix aplicado e o que ele mede

`recomp_mid_v2/patch_fios_f2b_pump_ring_bounds.py` (idempotente, `ALREADY` na 2.ª
passagem; o nome começa por `patch_fios_f2b_p` para o glob do `apply_all_patches.sh` o
correr **depois** do `patch_fios_f2b_open_block_install.py`, que instala o bloco).

Medido em **3/3 corridas** com `PS3_WATCH_STORE=0x400C3D88 PS3_TRACE_TYMAP=1`:

| sinal | antes | depois |
|---|---:|---:|
| `F2B-STREAM-PUMP pumped` | 20169344 em 154 chunks | **262144 em 2 chunks** (um ring) |
| `[WATCHSTORE] BULK` sobre o alvo | 19 | **0** |
| `[WADLD-VT28] #49 vt` | `0x9102FFFF` | **`0x005170B8`** (igual ao #28) |
| `ICALL-BAD ctr=0x40637408` | 10 | **0** |
| `F2B-STREAM-FILL` | 64 | 64 |
| `startseq(handle=` | 2 | 2 |
| `R_PermA full` | 1 | 1 |
| `REPLAY-NOPIC` | 4 | 4 |
| `SetFlip` | ~2250 | ~2380 |

**A corrupção está resolvida e não há regressão nos elos a montante.**

## Mas a parede não caiu — mudou de sítio, e isso diz-se sem rodeios

Nas mesmas 3 corridas o boot continua a abortar, agora noutro alvo:

```
antes:   [ppu] FATAL: stuck calling 0x40678C90 (2000 times) -- aborting run
depois:  [ppu] FATAL: stuck calling 0x00514E80 (2000 times) -- aborting run
                [ICALL-BAD] ctr=0x00514E80 lr=0x0024E2D4 r3=0x00000000 r12=0x88004044
```

`thr_auto_load end` deu **0 em 3/3 antes E depois** nesta série (as corridas com
`PS3_TRACE_TYMAP` ligado nunca lá chegaram; sem ele, o histórico é ~1 em 3). Ou seja:
**estas corridas não provam nem regressão nem não-regressão** do `thr_end` — a prova tem
de vir do `smoke_chain_gate.sh` com as sondas desligadas, que está a correr.

O novo alvo é qualitativamente diferente: `0x00514E80` está no **segmento de dados do
guest** (vizinhança das vtables `0x005170B8`/`0x0051B2F8`), não é lixo de heap como o
`0x40678C90`. O `lr=0x0024E2D4` cai no walker de registos do WAD
(`func_0024E1E8` → `func_0024D570`, laço `r24` de 0 a 0x30 de 4 em 4). Parece uma
vtable a ser chamada onde devia estar um OPD — uma indirecção a mais ou a menos —, não
memória destruída.

**Hipótese, não medida:** com o objecto de tipo intacto, o walk do WAD passou a executar
código que antes nunca corria (com a vtable destruída, o despacho caía no `ICALL-BAD` e
não fazia nada), e o `0x00514E80` é o primeiro problema real desse caminho. Consistente
com o log crescer ~200 linhas, mas **ninguém provou** que é código novo.

## O gate oficial, 6 corridas, sondas desligadas

```
run  st620 startseq nopic thr_end r_perma setflip_after_rperm pad  elo_stopped
 1     11      2      4      0       1            8            0   AUTO_LOAD (thr_end)
 2     11      2      4      0       1            9            0   AUTO_LOAD (thr_end)
 3     11      2      4      0       1           10            0   AUTO_LOAD (thr_end)
 4     11      2      4      0       1            9            0   AUTO_LOAD (thr_end)
 5     11      2      4      0       1           10            0   AUTO_LOAD (thr_end)
 6     11      2      4      0       1           10            0   AUTO_LOAD (thr_end)
elo_stopped=nenhum em 0 de 6 (limiar: 4)   -> rc=1
```

**0 de 6. Não arredondo isto para "quase".** Os cinco primeiros elos passam em 6/6 e o
`setflip_after_rperm` estabilizou em 8–10 (a Fase 7 tinha registado **0 mesmo no binário
de referência**, com um único outlier de 107 numa das seis corridas). O elo que falha é o
`AUTO_LOAD`.

**Ressalva importante sobre a comparação:** o marco v1.1 registou o `thr_end` a passar
*com o watchdog subido* (`PS3_STUCK_ICALL_LIMIT` elevado), não com o default de 2000. Como
o abort pós-fix é exactamente esse disjuntor, o gate com o limite subido é a comparação
honesta — está a correr. Até haver esse número, **não está estabelecido** se isto é
regressão do fix ou se o gate com o default sempre deu 0/6.

## Ressalva honesta

Isto explica e corrige a corrupção de `tab[0x58]`. **Não está provado** que seja a única
coisa entre aqui e um menu. O que está provado é que este stomp era real, era nosso, e
destruía o objecto que o walk do typemap precisa.
