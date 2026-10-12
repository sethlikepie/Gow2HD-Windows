# Mapa mat+off -> combination Default.ps3fx (func_00168350)

Confirma/corrige a tabela de `shader-task-1-brief.md` por leitura directa do corpo de
`func_00168350` em `recomp_macos_v2/ppu_recomp_000.cpp:308336-308412` (definicao unica
no lift; sem fragment copy em `ppu_recomp_002.cpp` -- confirmado por
`grep -c "^void func_00168350(" ppu_recomp_002.cpp` = 0).

## Prologo relevante

- `r29 = r3` no entry (:308350) -- `r29` guarda o ponteiro `this` do material por toda
  a funcao (nunca reatribuido depois disto).
- `r30 = vm_read32(mat+0x94)` (:308362) -- "mode" que escolhe entre `func_00168538` /
  `func_0016872C` / `func_001685E0` / `func_00168670` / fallthrough (builder Default).
- `r30 = vm_read8(mat+0x98)` (:308373, reaproveitando r30) -- byte usado num
  `vm_write32` para uma fila/allocator interno (nao guest-visible via a combination
  string; nao e' um dos 4 campos do format).
- `r0 = vm_read8(mat+0x7C)` (gate) e `r11 = vm_read32(mat+0x78)` (:308387-308388) --
  se `gate==0`, salta directo para `loc_00168440` com `r0=0`; senao recalcula `r0` a
  partir de `vm_read32(TOC-0x3AE0)` (global, NAO um campo do material).

## `loc_00168440` -- estado final antes de `func_001F2AD4(ctx)`

Ordem literal das atribuicoes (nao importa, so o valor final por registo):

```
r3 = vm_read32(TOC-0x3ACC)                          # format ptr, deve ser 0x4C61B0
r4 = bitextract( sign(r11) - (sign(r11) ^ r11) )    # r11 = word@mat+0x78
r5 = sign-extend(r0)                                # r0: 0 se gate@+0x7C==0, senao
                                                     #     bitextract(global@TOC-0x3AE0)
r6 = vm_read8(mat+0x90)                             # RAW byte
r7 = vm_read8(mat+0x99)                             # RAW byte
func_001F2AD4(ctx)   # guest sprintf-alike: fmt=r3, args=(r4,r5,r6,r7)
```

`func_001F2AD4` recebe fmt em r3 e os 4 argumentos inteiros em r4..r7 -- ordem PPC
ABI padrao (r3=1o arg, r4=2o, ...). Como a string e'
`"TEXTURE=%d;CONSTCOLOR=%d;HASCOLOR=%d;TRANSFORM=%d"`, os 4 `%d` mapeiam
sequencialmente para r4,r5,r6,r7.

## Correcao a hipotese do Task 1 brief

O brief (e o plano grok) hipotetizou `TEXTURE=u8@+0x90 (r6)` e `HASCOLOR=u8@+0x99 (r7)`.
Pela leitura directa do codigo (posicao de registo == posicao do `%d` na string), o
mapeamento real e':

| Slot na string (ordem) | Registo | Valor real                                             |
|-------------------------|---------|---------------------------------------------------------|
| **TEXTURE** (1o `%d`)   | r4      | bit derivado de `word@mat+0x78` (`!=0` -> 1, `==0` -> 0) |
| **CONSTCOLOR** (2o `%d`)| r5      | `gate@mat+0x7C==0 ? 0 : (global@TOC-0x3AE0 != 0 ? 1 : 0)`|
| **HASCOLOR** (3o `%d`)  | r6      | `vm_read8(mat+0x90)` RAW                                 |
| **TRANSFORM** (4o `%d`) | r7      | `vm_read8(mat+0x99)` RAW                                 |

Ou seja: `mat+0x90` e `mat+0x99` alimentam **HASCOLOR/TRANSFORM**, nao TEXTURE/HASCOLOR
como hipotetizado. TEXTURE e CONSTCOLOR sao *bits derivados*, nao bytes crus do
material -- CONSTCOLOR em particular depende de um **global** (TOC-0x3AE0), nao so do
material, gated por `mat+0x7C`.

Isto e' consistente com a mensagem observada `TEXTURE=0;CONSTCOLOR=1;HASCOLOR=0;
TRANSFORM=0`: nao precisa de "word@+0x78==0 mas CONSTCOLOR=1 mesmo assim" ser uma
contradicao -- CONSTCOLOR=1 vem do gate@+0x7C!=0 E global@TOC-0x3AE0!=0, um caminho
totalmente separado de +0x78.

**Confirmado com medicao real (Task 2, PS3_TRACE_COMBOPROP, Boot A, 2026-07-21):**

```
[COMBOPROP] #1 site=func_001683DC mat=0x0FEFFAF8 fmt=0x004C61B0 t90(+0x90)=0 h99(+0x99)=0
  w78(+0x78)=0x00000000 mode94(+0x94)=0x00000001 gate7c(+0x7C)=1 g3AE0=0x005229F8
  args(a1,a2,a3,a4)=(0,1,0,0)
```
imediatamente seguido, no mesmo log, por:
```
ERROR: Invalid shader combination: $/enginesupport/shaders/PS3/Default.ps3fx
  (TEXTURE=0;CONSTCOLOR=1;HASCOLOR=0;TRANSFORM=0)
```

args(a1,a2,a3,a4)=(0,1,0,0) bate DIGITO A DIGITO com TEXTURE=0;CONSTCOLOR=1;HASCOLOR=0;
TRANSFORM=0 -- confirma empiricamente a correcao acima (r4=TEXTURE, r5=CONSTCOLOR,
r6=HASCOLOR@+0x90, r7=TRANSFORM@+0x99), refutando a hipotese original do brief.
`fmt=0x004C61B0` bate exactamente com a rodata esperada. Detalhe completo, incluindo
a descoberta de que o site real e' um de 5 fragmentos-irmao em `ppu_recomp_002.cpp`
(nao `func_00168350` sozinho) e a interpretacao de hipoteses: ver
`.superpowers/sdd/shader-task-2-report.md`.

## Rodata confirmada no EBOOT.ELF (sem commitar o ELF)

```
TEXTURE=%d;CONSTCOLOR=%d;HASCOLOR=%d;TRANSFORM=%d  off 0x4b61b0  VA 0x4c61b0
Invalid shader combination                          off 0x4b5fbf  VA 0x4c5fbf
```

Ambos batem exactamente com o esperado no brief (`0x4c61b0`, `0x4c5fbf`).
