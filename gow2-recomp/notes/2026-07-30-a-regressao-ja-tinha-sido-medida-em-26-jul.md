# A regressão já tinha sido medida a 26 de Julho — e o gate deixou-a passar

**Data:** 2026-07-30 · **Método:** `git log` na janela do bissect + contagem de funções
nos lifts guardados. Nada corrido; tudo verificável com dois comandos.

## O que se procurava

O bissect fechou a janela em **25 Jul 18:23 → 26 Jul 02:35**. Faltava saber o que caiu lá
dentro. A resposta estava num commit da própria janela, cujo título já dizia tudo:

```
21eecdc  26 Jul 01:46  rdy0: 1o binario de um lift 100% regenerado — e a regressao medida
```

## O que esse commit registou, na altura

Texto literal do corpo:

```
O BINARIO ARRANCA mas REGRIDE. Medido, mesma recipe M0, mesmo ambiente, 6 runs
de cada, e o mesmo grep nos dois:

                        producao    relift v3
  st620 max              11 (6/6)     0 (6/6)
  funcoes registadas       56223        51990
  marcadores do baseline  244/244      211/244

4262 funcoes do v2 nao existem no v3 (e 29 novas). O lift e' auto-consistente:
0 chamadas directas sem definicao. Logo as 4262 so' podem ser atingidas por
despacho INDIRECTO -- OPD, vtable, jump table ou a tabela por EA do ppu_loader.
Diagnostico da causa raiz em curso.
```

**A regressão foi vista, medida e escrita — a 26 de Julho, às 01:46.**

## O que aconteceu a seguir, 53 minutos depois

```
0c5ef0f  26 Jul 02:39  rdy0: o lift regenerado PASSA o gate M0 — st620=11 em 5/6
```

O `st620` voltou a 11 e o gate M0 deu verde. **Mas o gate M0 mede exactamente o elo
errado.** O `st620` é a FSM do movie player da intro; ele progride até ao fim da intro
mesmo quando tudo o resto está partido — foi isso que esta sessão mediu, quatro vezes.

A partir daí a regressão deixou de ser visível: passou a haver um verde a tapá-la.
O marco v1.0 promoveu o `recomp_macos_v3` a produção a 29 de Julho com esse mesmo gate.

## A confirmação, medida hoje

| | funções `void func_*` definidas |
|---|---:|
| `functions.json` declara | 20 750 |
| `recomp_macos_v2.pre_v4` — o lift que gerou o `boot_gow2.pre_v3`, **que funciona** | **56 072** |
| `recomp_macos_v2` — o v3 promovido, **produção actual, que falha** | **51 917** |
| **diferença** | **4 155** |

Bate com as 4262 do commit de 26 Jul (a diferença de ~100 é o lifter ter evoluído entre
as duas datas).

O lifter descobre muito mais do que o `functions.json` declara — 20 750 declaradas contra
50–56 mil emitidas. **A regressão está na descoberta**, e o que se perdeu são funções que,
por construção, só são alcançáveis por despacho indirecto.

## CORRECÇÃO ao número acima — 2 855, não 4 155

O `4 155` da tabela é a diferença bruta de contagens e **está enganado por lixo**. O diff
nominal, feito a seguir:

```
lift ANTIGO (funciona)   total= 56 072   em .text= 54 674   FORA do .text=  1 398
lift NOVO   (falha)      total= 51 917   em .text= 51 917   FORA do .text=      0
PERDIDAS                 total=  4 253   em .text=  2 855   FORA do .text=  1 398
NOVAS                    total=     98   em .text=     98   FORA do .text=      0
```

(O segmento executável do `EBOOT.ELF` é `0x00010000–0x0050C7E0`; tudo fora disso não é
código.)

As primeiras "perdidas" são `func_00000000`, `func_00000004`, `func_00000008`… —
endereços sequenciais a partir de zero. **Não são funções**, são artefactos.

Portanto o quadro honesto é **dos dois lados**:

- **O lift novo MELHOROU numa coisa:** deixou de emitir 1 398 funções em endereços que não
  existem no binário. O antigo tinha-as; o novo tem zero.
- **E PIOROU noutra:** perdeu **2 855 funções em endereços reais do `.text`**, e ganhou 98.

O número que interessa à regressão é **2 855**, não 4 155. Escrevi 4 155 no commit
`eedea6b` antes de separar o lixo; fica corrigido aqui.

Verificação adicional: o `thr_auto_load` (`func_00147038`) chama 6 funções directamente no
lift antigo e **nenhuma delas está entre as perdidas**. Logo a perda não está no corpo
directo do `thr_auto_load` — se contribui, é mais abaixo na cadeia, por despacho indirecto.

## Porque isto liga ao diagnóstico do ponto 1

A nota `2026-07-30-ponto1-despacho-vtable-o-lift-esta-fiel.md` estabeleceu, por medição
estática, que toda a cadeia da parede TYPE15 é alcançável **exclusivamente por despacho
indirecto**: `func_00029AF0` é o slot 3 da vtable `0x00510FD0` e não tem um único `bl` no
ELF inteiro; `1D7FCC` e `1A9D24` são o primeiro método virtual de duas vtables irmãs.

O commit de 26 Jul diz que as funções perdidas *"só podem ser atingidas por despacho
indirecto"*. É a mesma população.

**As seis funções que examinei existem nos dois lifts** (verificado: `func_00029AF0`,
`func_000CD9DC`, `func_000CD498` estão em `pre_v4` **e** em `v3`). Logo a cadeia que
examinei não é, ela própria, a que se perdeu — mas vive no mesmo mecanismo, e o caminho do
2.º movie passa por despacho indirecto em vários pontos.

## O que isto muda no marco v1.1

A Fase 6 (REG-02, "causa nomeada por ficheiro e linha") tem agora um ponto de partida
concreto em vez de uma janela de oito horas:

> **Que 4 155 funções o `recomp_macos_v3` deixou de emitir face ao `recomp_macos_v2.pre_v4`,
> e qual o passo do `ppu_lifter.py` que as descobria e deixou de descobrir?**

É uma pergunta respondível com dois `grep` e um `comm`, sem correr o jogo:

```bash
cd ../gow2-recomp
grep -ho '^void func_[0-9A-F]\{8\}' recomp_macos_v2.pre_v4/ppu_recomp_*.cpp | sort -u > /tmp/a
grep -ho '^void func_[0-9A-F]\{8\}' recomp_macos_v2/ppu_recomp_*.cpp        | sort -u > /tmp/b
comm -23 /tmp/a /tmp/b | wc -l     # as que se perderam
comm -13 /tmp/a /tmp/b | wc -l     # as que apareceram
```

E a Fase 7 (GATE) ganha o seu melhor argumento: **a contagem de funções emitidas é um elo
da cadeia que o gate tem de medir.** 56 072 → 51 917 é uma queda de 7,4% que nenhum
`st620` iria apanhar — e não apanhou, durante quatro dias.

## Ressalva importante, e desconfortável

Isto não desfaz o marco v1.0. O que o v1.0 entregou — o re-lift reprodutível, o catálogo
de patches como gate, a promoção formalizada — continua válido e é o que torna esta
correcção possível.

Mas o v1.0 **promoveu a produção um lift que já era conhecidamente pior**, e fê-lo com um
aceite que a própria dívida declarada avisava ser insuficiente ("o smoke não cobre WADLD
nem o caminho de shaders"). A dívida estava certa. Só se manifestou um passo antes do
previsto.
