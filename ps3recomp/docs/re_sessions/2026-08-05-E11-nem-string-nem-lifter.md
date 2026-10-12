# E11 — nem string nem lifter: uma palavra parcialmente sobrescrita

**Data:** 2026-08-05
**Antecessor:** [E10](2026-08-05-E10-a-13a-chamada.md)
**Investigação delegada:** Grok (`run-msghs4zb-yp21wr`, ~11 min), depois verificada
e corrigida aqui.

As duas hipóteses do E10 estavam ambas erradas. O que sobrou é mais estreito e
mais útil do que qualquer uma delas.

---

## O que o Grok provou (verificado por mim antes de aceitar)

**H2 — composição de 16 bits do lifter — REFUTADA.** Duas provas independentes:

1. `lis r*, 0x726D` (`addis RT,0,0x726D`): **0 ocorrências no `EBOOT.ELF`**.
2. A base **varia entre corridas da mesma build**. Um imediato do lifter daria
   a mesma constante sempre. Medido por mim, não relatado:

| corrida | base | ASCII |
|---|---|---|
| `cpy284_mf_run1` | `0x726D372E` | `rm7.` |
| `cpy284_mf_run2` | `0x726D532E` | `rmS.` |
| `cpy284_mf_run3` | `0x726D252E` | `rm%.` |
| `e6wall1` (build anterior) | `0x726D612E` | `rma.` |

**A cadeia até ao valor**, com âncoras que o Grok localizou:

```
func_002182A4       pool = *(tabela)
  → func_00263554   head = *(pool+4);  next = *head   <- a leitura UNCOMMITTED
  → func_00227788   2o pop (size-class)
  → func_002210BC   grava this+0x5C/0x60/0x64/0x68
  → func_00220284   trata como ponteiro de floats -> 16 escritas stride 0x10
```

O `ra=func_00263554+0x56C` dos logs é o retorno host de `vm_read32(head)`. Ou
seja: **a cabeça da free-list já contém este bit-pattern quando o pop corre** —
não é composto no sítio da falha. Isso está provado.

Os call sites de `func_00220284` são só dois tail-trampolines
(`func_00220F88`, `func_002210BC`), o que fecha a pergunta "quem chama".

---

## Onde corrigi o veredicto do Grok

O Grok concluiu **H1 — o valor é um pedaço do nome `r_perma.wad`** (`rma.` existe
uma vez no ELF, dentro de `r_perma.wad_ps3`). **Não aceito**, e os números que o
próprio Grok recolheu são o que o desmente: `rm7.`, `rmS.` e `rm%.` **não
existem** como literais no ELF. Três das quatro observações não são string
nenhuma do binário.

Duas medições minhas fecham o assunto:

**1. Só um byte varia.**

| byte | valores | ASCII | |
|---|---|---|---|
| 0 | `0x72` | `r` | **constante** |
| 1 | `0x6D` | `m` | **constante** |
| 2 | `0x25 0x37 0x53 0x61` | `% 7 S a` | **varia** |
| 3 | `0x2E` | `.` | **constante** |

Um esqueleto de 3 bytes fixos com um byte a variar não é um pedaço de um nome.
E os valores que o byte 2 toma — `%`, `7`, `S`, `a` — não são um conjunto
plausível de caracteres numa posição de um path.

**2. O desalinhamento é estrutural.**

```
0x726D372E  mod 4 = 2   mod 16 = 14
0x726D532E  mod 4 = 2   mod 16 = 14
0x726D252E  mod 4 = 2   mod 16 = 14
0x726D612E  mod 4 = 2   mod 16 = 14
```

`func_00220284` copia grupos de 16 bytes com stride `0x10`. Uma base legítima
para isso é alinhada a 16. **As quatro estão 14 bytes fora — sempre a mesma
quantidade.** Um valor aleatório não acerta quatro vezes no mesmo desalinhamento.

## O que sobra (mais estreito, e testável)

O campo não contém uma string inteira nem um imediato: contém uma **palavra com
um esqueleto fixo e um byte de conteúdo variável**, sempre com o mesmo
desalinhamento. Isso é a assinatura de uma **palavra parcialmente sobrescrita** —
uma escrita estreita (byte/halfword) a cair sobre uma palavra que já lá estava,
ou dois campos a serem fundidos.

Que o esqueleto `72 6D .. 2E` seja legível como `rm?.` continua a apontar para
uma região de strings, mas como **vizinhança**, não como o valor: o que lá está
é provavelmente o resto de um path com um byte substituído.

Formulação para o E12, com o teste que a decide:

> **H3 — escrita estreita sobre a cabeça da free-list.** `*(pool+4)` é escrito
> por um store de byte ou halfword em vez de word, deixando 3 bytes de conteúdo
> anterior. **Teste:** dump de 16 bytes à volta de `pool+4` no momento do
> UNCOMMITTED — se os bytes vizinhos forem texto contíguo e coerente, o campo
> está dentro de um buffer de nome e o bug é de *layout/offset*; se forem lixo
> heterogéneo, é uma escrita estreita e o bug é de *largura de store*.

O Grok já tinha sugerido este dump como próximo passo (`PS3_TRACE_FLHEAD`,
`PS3_TRACE_SZCLASS`, `PS3_TRACE_POOLIDX` na mesma corrida). Concordo, e o
critério acima diz o que cada resultado significa — escrito antes de o correr.

## Tensão por resolver (declarada pelo Grok, mantida)

A sonda regista `dst60 = 0` na chamada #13, e o laço tem `if (dst == 0) salta`.
Os RAs host das escritas batem no **primeiro** laço de floats. Ou o destino
efectivo vem de `this+0x64` (terceiro laço) e o mapeamento de RA host está
desfasado nesta build, ou o estado entre o print da sonda e a escrita não é o
que o print sugere. **Não fechado.** Não inventei explicação.

## Refutado por medição — não voltar a perseguir

1. **H2, imediato `lis 0x726D` do lifter** — 0 ocorrências no ELF; valor varia
   entre corridas da mesma build.
2. **H1, o valor é o nome `r_perma.wad`** — 3 das 4 observações (`rm7.`, `rmS.`,
   `rm%.`) não existem como literais no ELF.
3. **O valor ser um ponteiro legítimo** — `≡14 mod 16` em 4 de 4, num laço que
   copia grupos de 16 bytes.
4. **O `oris 0x726D` que existe no lift** ser o culpado — está em
   `ppu_recomp_001/003/006`, em regiões de dados-como-código (`"Full"`,
   `"GodM"`), e **não** em nenhuma das quatro funções da cadeia.

## Método

Delegar valeu a pena: o Grok fez em 11 minutos o rastreio de call sites e a
prova negativa no ELF, que me teriam custado bastante mais. Mas o **veredicto
dele estava errado**, e estava errado a partir de dados que ele próprio tinha
recolhido correctamente — reportou que `rm7.`/`rmS.`/`rm%.` não existem no ELF e
mesmo assim concluiu que o valor era um nome de ficheiro.

> Regra: aceitar de um delegado os **factos** que ele mediu e verificar sempre
> a **inferência**. Aqui os factos estavam certos, a ponte entre eles não. Verificar
> custou dois comandos.
