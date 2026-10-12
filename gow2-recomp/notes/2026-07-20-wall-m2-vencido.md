# O wall M2 caiu — e nao era SPURS

Data: 2026-07-20 · macOS/arm64 · Apple M5 · Phase 3 Task 3.2/3.4 do plano
`../ps3recomp/docs/superpowers/plans/2026-07-20-gow2-full-bringup.md`.

## O que estava escrito

`SPURS_M2_FINDINGS.md` e `GOW2_BOOT_STATE.md` concluiam, com watchpoint de
hardware, que a flag `0x86E118` **nunca era escrita por ninguem** durante ~8 s de
spin, e atribuiam isso ao kernel SPURS em falta — orcado no `docs/SPURS_BRINGUP.md`
como o Marco 2.5, "6–12 pessoa-meses".

**A medicao estava certa. A conclusao estava errada.**

## Causa real: lift desactualizado, bug de rA=0 no `stwcx.`

`func_0030600C` **escreve a sua propria flag de saida** — e um push LIFO
lock-free auto-satisfeito, nao uma espera por um produtor externo. O store e um
`stwcx. r29, 0, r10`.

Em PowerPC, um campo **rA de 0 nas formas indexadas significa o literal 0, nao o
GPR0** (r0 nao e codificavel como base nessas formas). O lift antigo emitia:

```c
uint64_t ea = ctx->gpr[0] + ctx->gpr[10];   /* ERRADO */
```

e GPR0 tinha scratch vivo (`gpr[0] = -gpr[29]`, do teste de nao-nulo do no).
Resultado: o store ia para `0x86E118 - node_ptr`, um endereco aleatorio. Como
GPR0 nao mudava entre o `lwarx` e o `stwcx.`, a reserva **casava** e o store
**tinha sucesso** — em silencio, no sitio errado.

Por isso o watchpoint em `0x86E118` via zero escritas: a escrita existia e ia
para outro lado. E cada iteracao fazia uma escrita selvagem de 4 bytes em memoria
alheia, o que explica plausivelmente o `0x58585858` e o ponteiro OOB `0xFD86Cxxx`
registados no `GOW2_BOOT_STATE.md`.

## O fix ja existia no repo

`../ps3recomp/tools/ppu_lifter.py:203-210` define `_ra0()` documentando
exactamente esta regra, e as linhas 1184/1188 encaminham `lwarx`/`stwcx.` pelos
helpers `ppu_lwarx`/`ppu_stwcx`. O `recomp_macos/` e que era um lift **anterior
ao fix**:

| | `recomp_macos` (antigo) | `recomp_macos_v2` (novo) |
|---|---|---|
| chamadas `ppu_lwarx` | **0** | **5277** |
| sites CAS inline com `gpr[0]` | **5800** | **0** |

Bastou re-liftar:

```bash
../ps3recomp/.venv/bin/python ../ps3recomp/tools/ppu_lifter.py \
    EBOOT.ELF --functions functions.json --output recomp_macos_v2 -j 8
./build_macos.sh recomp_macos_v2
```

(56 072 funcoes, 31 chunks. Com o fix do O(n^2) e `-j 8`, o lift leva ~1 min.)

## Dois bloqueios encontrados no caminho

**1. `g_trampoline_fn` nao linkava.** O lifter emitia
`extern "C" __thread void (*g_trampoline_fn)(void*)` nos chunks, mas no Apple o
`ppu_loader.cpp` define-o como `thread_local` do C++ — um `extern "C" __thread`
nao emite o wrapper TLS do Itanium que as TUs C++ exigem, entao os chunks pediam
o simbolo C simples `_g_trampoline_fn`, que nao existe. Corrigido **no lifter**
(ramo `__APPLE__`), nao a mao no output.

**2. Memoria local do RSX nao commitada.** Passado o wall, o titulo entra no
`cellGcmSys`, faz `SetTile`/`SetDisplayBuffer` de 1280x720 e morre com SIGBUS no
primeiro `vm_write32` para o guest `0xC0F40000`. O `boot_main.cpp` ja commitava
`0xC0000000..0xD0000000` (os 256 MB que o `cellGcmGetConfiguration` anuncia) e o
`boot_macos.cpp` nao — gap de paridade do host que a Task 1.1 nao apanhou porque
o boot nunca la tinha chegado. Corrigido, regulavel por `PS3_VM_RSX_MB`.

## Resultado medido

| | lift antigo | lift novo |
|---|---|---|
| linhas de log em 25 s | 412 | **9 131 168** |
| `*0x86E118 +0` | `0x00000000` | **`0x43005290`** |
| `*0x86E118 +4` | `0x00000000` | **`0x430052C0`** |
| `AddWorkload` | **0** | **3** |
| `cellGcmSys` | 0 | **5561** |
| `[SPU]` (jobs a correr) | 0 | **15 511** |
| CPU | ~1,5 % (bloqueado) | ~315-390 % (a trabalhar) |

Os valores em `0x86E118` sao ponteiros de heap reais — o push LIFO aterra no
sitio certo. `AddWorkload` deixou de ser 0, portanto o kernel SPURS (que ja
estava vivo e vazio) passa a ter trabalho de verdade.

O titulo chega agora a **intro**: abre os `_movies/smlogo_v2.*` (falha por
`PS3_NOMOVIES=1`, que e o esperado nesta config).

## Proximo estado, nomeado

O boot ja nao para: corre continuamente no init do RSX / intro. Pendentes
imediatos:

- **5 NIDs distintos por resolver** (o smoke fixa o limite em 5):
  `0x4692AB35`, `0x4A5EAB63`, `0x63FF6FF9`, `0xDB869F20`, `0xEFEB2679`.
- `[SPU] indirect branch to unknown LS address 0x03881 (image 0)` em repeticao —
  o spu0 salta para um endereco de LS que o lift nao cobre.
- Volume de log: 9 M linhas em 25 s torna o smoke pesado; vale gatear o spam do
  SPU.

## Licao

O `CLAUDE.md` manda "suspeitar de nos mesmos primeiro". Aqui o suspeito nao era
um paliativo nosso, era um **artefacto gerado desactualizado** — e o fix estava
commitado no lifter desde `edfad91` (2026-07-08). Vale verificar a data e o
conteudo do lift antes de aceitar qualquer conclusao sobre o comportamento do
guest: `grep -c ppu_lwarx recomp_*/ *.cpp` responde em segundos e teria poupado
o orcamento de "6–12 pessoa-meses".
