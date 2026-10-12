# Duas hipóteses do lifter, refutadas por A/B — e o método fica montado

**Data:** 2026-07-30 · **Método:** correr o `ppu_lifter.py` contra o `EBOOT.ELF` real com
e sem cada mecanismo, e contar `void func_*`. ~16 s por corrida, zero compilação de C++.
O `tools/ppu_lifter.py` **não foi alterado** — o A/B usa cópias.

## O alvo

A regressão perdeu **2 855 funções de código real** (`.text` `0x10000–0x50C7E0`) entre o
lift que gera o `boot_gow2.pre_v3` (funciona, 54 674 em `.text`) e o `recomp_macos_v3`
(falha, 51 917). Ver `2026-07-30-a-regressao-ja-tinha-sido-medida-em-26-jul.md`.

A janela do bissect é 25 Jul 18:23 → 26 Jul 02:35, e o lift `v3` foi gerado a ~01:46
(commit `21eecdc`). Os commits do `ppu_lifter.py` nessa janela:

```
1694fca  25 Jul 19:00  consumidor de invalid_instructions (salto de padding/dados-no-texto)
fbbecd6  25 Jul 19:22  round 1 da B3 -- config malformado deixa de passar em silencio
efefcfb  25 Jul 23:52  Task B2 -- jump tables declaradas no TOML
27c7657  26 Jul 00:06  round 1 da B2 -- merge de switch tables deduplica alvos
1c30e12  26 Jul 02:31  truncated-bounds repair          <- 4 min antes do boot_gow2_v4
```

## Hipótese 1 — `truncated-bounds repair`: REFUTADA

Era a minha candidata favorita, e por má razão: foi commitada **quatro minutos** antes de
o `boot_gow2_v4` (que falha) ser construído. Proximidade temporal não é causa.

O mecanismo é plausível à primeira vista — ao estender os limites de uma função, endereços
que antes eram entradas *mid-function* separadas passariam a cair dentro dos limites e
deixariam de gerar wrappers. Menos wrappers, menos funções.

**Medido, A/B com o mesmo ELF e o mesmo `functions.json`:**

| | funções emitidas | mid-function wrappers |
|---|---:|---:|
| **com** repair (o actual) | **51 917** | 30 927 |
| **sem** repair | **51 839** | 30 849 |

O repair **adiciona 78** funções. Não remove nenhuma. A hipótese estava invertida.

(Também não é a causa cronológica: o lift `v3` foi gerado a ~01:46 e este commit é de
02:31 — o `v3` nem sequer o tinha.)

## Hipótese 2 — `invalid_instructions`: REFUTADA

Esta parecia melhor ainda, porque explicaria **os dois lados** do resultado: o lift novo
perdeu 2 855 funções reais **e** deixou de emitir 1 398 funções em endereços que não
existem no binário. Um mecanismo que "salta padding e dados-no-texto" produziria
exactamente esse par.

E está na janela, a 25 Jul 19:00 — depois do `pre_v3` (18:23), antes do `v3` (01:46).

**Refutada por leitura do próprio commit e confirmada por inspecção do build:**

O corpo do `1694fca` diz, textualmente: *"Sem config, o comportamento é byte-a-byte
idêntico ao scan antigo."* O `invalid_instructions` é populado a partir de `--config`
(`ppu_lifter.py:587-591`).

E o `build_macos.sh:77` invoca:

```bash
python3 "$PS3/tools/ppu_lifter.py" "$EBOOT" --functions "$FUNCS" -o "$LIFT" -j 4
```

**Sem `--config`.** O `config/gow2_recomp.toml` existe mas, como o seu próprio cabeçalho
declara, "ainda NÃO está ligado ao `ppu_lifter.py` — isso é a Task A2". Logo o dicionário
está vazio e o mecanismo é um no-op neste lift.

## O que sobra da janela

Por eliminar, todas testáveis pelo mesmo A/B:

- `efefcfb` (jump tables do TOML) e `27c7657` ("merge de switch tables **deduplica
  alvos**") — a deduplicação é a que mais promete, porque remover alvos duplicados remove
  funções. Mas ambas dependem provavelmente de `--config`, tal como a hipótese 2.
- `fbbecd6` — validação de config; quase de certeza inerte sem `--config`.
- **Uma possibilidade que ainda não explorei:** o `recomp_macos_v2.pre_v4` pode ser
  bastante mais antigo do que 25 Jul, e nesse caso as 2 855 não vêm de um só commit da
  janela mas da acumulação de várias mudanças. **Verificar a data de geração do
  `pre_v4` é o primeiro passo, e é grátis.**

## E o passo grátis deu resultado: o `pre_v4` é MAIS ANTIGO do que parece

Corri-o logo, e muda o enquadramento:

```
recomp_macos_v2.pre_v4/ppu_recomp_000.cpp   -> SEM carimbo lifter-rev
recomp_macos_v2/ppu_recomp_000.cpp          -> /* lifter-rev: 0852305 */   (26 Jul 16:11)
```

O carimbo `lifter-rev` foi introduzido pelo commit `bc3707d`, a **24 Jul 13:48**. Um lift
sem carimbo foi gerado **antes disso**.

A data do directório (`25 jul 18:23`) é de quando foi copiado/renomeado, não de quando foi
gerado — o `promote_lift.sh` faz backup por *rename*, o que preserva a mtime da cópia, não
a da geração.

**Consequência para o diagnóstico:** as 2 855 funções **não se perderam num commit da
janela de oito horas**. O `pre_v4` é anterior a 24 Jul 13:48; o `v3` é de 26 Jul. A
diferença acumula **dois a três dias** de mudanças no lifter, não oito horas.

Isto não invalida o bissect de binários — ele continua a dizer, correctamente, que
`pre_v3` funciona e `v4` não. Mas separa duas coisas que eu tinha juntado:

- a **janela do binário** é 25 Jul 18:23 → 26 Jul 02:35 (medida, sólida);
- a **janela do lift** é ≤ 24 Jul 13:48 → 26 Jul 01:46 (bem maior).

O `boot_gow2.pre_v3` de 25 Jul 18:23 foi construído a partir de um lift já com dias.

**Isto reabre candidatos que eu tinha excluído por data**, incluindo o
`46a4c3f 24 Jul 15:06 "resolve base RA estaticamente nos D-form (ressuscita callee-save)"`
e o `145fe58 25 Jul 14:46 "jump tables ressuscitados"` — este último é particularmente
interessante, porque *acrescentar* descoberta de jump tables muda quais os alvos que
viram funções próprias e quais ficam `kept internal` (hoje: 1 537 case targets, dos quais
**1 299 kept internal** e só **237** viram funções).

## O método, que fica montado e é o que interessa reter

O A/B do lifter custa **~16 segundos por variante** e não compila uma linha de C++:

```bash
# 1. copiar o lifter e as dependências para um directório de trabalho
mkdir -p $T/toolsab && cp ../ps3recomp/tools/*.py $T/toolsab/
#    (o lifter exige ppu_disasm.py no mesmo directório — falha com
#     "Error: ppu_disasm.py must be in the same directory." se faltar)

# 2. desligar o mecanismo na CÓPIA, nunca no original

# 3. correr e contar
python3 $T/toolsab/ppu_lifter_variante.py EBOOT.ELF --functions functions.json \
        -o $T/lift_variante -j 4
cat $T/lift_variante/ppu_recomp_*.cpp | grep -c '^void func_'
```

O lifter já imprime contadores por mecanismo, que é meio caminho andado:

```
Boundary recovery: split 3 merged function(s) via prologue scan
Truncated-bounds repair: extended 180 function(s), 18984 byte(s) recovered
jump tables: 137 dispatchers, 1537 case targets, 1299 kept internal, +237 case funcs
Mid-function pass 1..5: 17783, 11817, 1233, 89, 5  -> 30927 total
```

Aritmética que fecha: 20 750 declaradas + 30 927 mid-function + 237 case funcs ≈ 51 914,
contra 51 917 emitidas. **Praticamente todas as funções do lift vêm do passo
mid-function**, não do `functions.json` — que declara só 20 750. É aí que 2 855 se podem
perder sem ninguém dar por isso, e é aí que a Fase 6 deve olhar primeiro.

## Ressalva

Nenhuma destas corridas foi levada a binário. São contagens de funções emitidas, não
prova de comportamento. Um lift com mais funções não é necessariamente melhor — 1 398 das
que o antigo tinha eram endereços inexistentes.
