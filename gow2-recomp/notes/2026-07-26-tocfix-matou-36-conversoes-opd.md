# O TOCFIX matou 36 conversões OPD — e os patches disseram rc=0

**Data:** 2026-07-26
**Gravidade:** alta — perda funcional silenciosa num lift que foi **promovido a produção**
**Descoberto por:** classificação dos 33 marcadores que o gate de baseline acusa (Fase 3)

## O que aconteceu

O lifter mudou o shape do restauro do TOC depois de uma chamada:

```c
// ANTES (lift de 20 jul)
ctx->gpr[2] = vm_read64(ctx->gpr[1] + 0x28);

// AGORA
ctx->gpr[2] = 0x00541178ULL; /*TOCFIX ld r2,N(r1)*/
```

Escala medida:

| | lift antigo (`.pre_v4`) | lift novo (produção) |
|---|---|---|
| restauro dinâmico do TOC | **17 602** | 4 |
| `TOCFIX` estático | 0 | **17 268** |

Todas as agulhas dos `patch_*_opd.py` terminam na linha antiga. Deixaram de casar.

## Porque não deu erro

Os patches **correm com `rc=0` e não fazem nada**. Não falham — reportam sucesso:

```
$ patch_32e200_opd.py <lift novo>
  32E200: fixed 0 OPD sites (pat1=0 pat2=0), remaining indirect=7
  OK patch_32e200_opd
  rc=0
```

`fixed 0` e mesmo assim `OK` + `rc=0`. Um patch que corre e não produz o efeito é pior que
um que falha ruidosamente, porque o `apply_all_patches.sh` classifica-o `ALREADY-APPLIED`
(conteúdo não mudou) e o gate fica verde.

## A perda funcional (medida, não inferida)

Não é só a tag de diagnóstico. É a **conversão de chamada indirecta para OPD**:

| | antigo | novo | delta |
|---|---|---|---|
| `ps3_call_opd` no lift inteiro | **52** | **16** | **−36 (−69%)** |
| guarda B71 (`0x4F000000u`) | 111 | 75 | −36 |
| `g_wadld_eof_ea` | 11 | 2 | −9 |

Sítios concretos que ficaram em `ps3_indirect_call` em vez de `ps3_call_opd`:

- `func_0032E200` — 7 sítios. É o dispatcher de vcall `+0x8`/`+0xC` que **alcança o ICGLdr**.
- `func_0039E5D8` — 2 sítios (finalize aninhado da cadeia GroupEnd).
- `func_00151248` / `func_0014A01C` / `func_0014AD94` — 7 sítios (ctor/init do ICG).

Ou seja: o caminho que alimenta o **registry de shaders** (parede D) perdeu as conversões OPD.

## Dois órfãos que não eram probes

- **`[B71]`** — não era diagnóstico, era uma **GUARDA**. `func_0002F3F0` tinha o corpo
  reescrito à mão com um bounds-check (`_sp < 0x10000u || _sp >= 0x4F000000u → devolve 0`)
  mais um cap de 256 chars no loop, para o hash de nomes do registry `393E0` não pendurar
  em memória-lixo não-nula. Corpo caiu de 2464 para 1424 bytes. **Sem escritor.**
  Cuidado com a homonímia: `patch_b71_cb56c_reuse_block.py` é de OUTRO B71 (`func_000B71B8`)
  e emite `[POSTINTRO] B71`, não `[B71]`.
- **`[BA808]`** — além da tag (gated por `PS3_TRACE_BA808`), o mesmo bloco carregava
  `g_wadld_eof_ea` e `PS3_WADLD_NO_EOF_EXIT`, que são **funcionais**. Sumiram os três.

## Duas classificações que eu tinha errado

- `[ICG-PATH-OPD]` e `[ICG-VCALL]` **não são órfãos**. O escritor é
  `patch_icg_ctor_opd.py`, que constrói a tag dinamicamente (`f"[{tag}]"`, linha 61) e a
  passa nas linhas 136/138. Um `grep` literal não os encontra. São RECUPERÁVEIS, mesma
  causa TOCFIX.
- `[OPDISP]` é **OBSOLETO** e por boa razão: o lifter actual materializa sozinho a jump
  table de `func_002A209C` (`switch ((uint32_t)ctx->ctr)` cobrindo 54/54 alvos —
  exactamente a base `0x2A2134` + os offsets que o patch injectaria). O comportamento
  está preservado nativamente; só a probe desapareceu.

## Porque o smoke não apanhou

O gate M0 mede a intro até `st620=11`. O `boot_gow2` do lift promovido dá 11 em 6/6 — e
está correcto. A perda manifesta-se **depois**: nas conversões OPD do caminho de shaders
(parede D) e no WADLD (parede C2). Um gate que só cobre a intro não podia ver isto.

**Lição:** `st620=11 em 6/6` prova equivalência **na intro**, não equivalência funcional.
Foi essa a inferência que fiz ao recomendar a promoção, e estava errada.

## O que fazer

A causa raiz é **uma só e é mecânica**. Corrigir as agulhas dos `patch_*_opd.py` para
aceitarem os dois shapes do TOC repõe as 36 conversões. Não é preciso reverter o lifter.

Além disso, os patches têm de **falhar** quando não convertem nada. `fixed 0` + `OK` +
`rc=0` é o defeito de processo que deixou isto passar — é âmbito da Fase 4 (catálogo de
patches como gate que falha).

Ver também: [`2026-07-26-strip-block-comeu-o-lift.md`](2026-07-26-strip-block-comeu-o-lift.md)

---

## Adenda (fim do dia): as 47 conversões voltaram, e o gate ainda acusa

Depois de corrigir as agulhas dos cinco patches OPD e o `patch_14b1f0_opd.py`, o
lift **regenerado do zero** tem as conversões todas:

```
ps3_call_opd(ctx,  no recomp_macos_v3  =  47      (alvo pre_v4: 47)
```

O gate de baseline, já com os critérios reformulados, dá **243/246 com 0 ausentes
e 0 a menos**. O que resta é um único par de grupo:

```
A MENOS (grupo) opd-dispatch: ps3_indirect_call+ps3_call_opd
                esperado >=15847  encontrado 15811
```

### O deficit de 319 não é perda — é a soma de duas populações opostas

Medido por função, cruzando `recomp_macos_v2.pre_v4` com `recomp_macos_v3`:

| | sítios de dispatch |
|---|---|
| perdidos em funções que só existem no lift ANTIGO (4252 funções, 170 com dispatch) | −619 |
| **ganhos** em 90 funções COMUNS | **+291** |
| saldo | **−319** |

As funções comuns onde o v3 tem MAIS dispatch (`func_00362E00` 0→16,
`func_00029E28` 0→12, e mais 88) são o **`truncated-bounds repair` a recuperar
código que o lift antigo descartava**. E boa parte das 4252 ausentes é lixo: já
estava medido que 1919 das funções só-no-antigo caem fora de qualquer secção
executável — o lift antigo desassemblou dados.

Ou seja: o limiar absoluto soma código inventado a partir de dados com código
real recuperado, e chama ao resultado uma regressão. **É o mesmo defeito de
critério que fez o `PREAMBLE ps3_indirect_call` falhar quando o lift melhorou.**

Fica como dívida declarada para a Fase 4: o limiar do grupo `opd-dispatch` tem
de ser recalibrado contra o lift actual, excluindo as funções que não existem em
ambos — comparar populações diferentes não mede nada.
