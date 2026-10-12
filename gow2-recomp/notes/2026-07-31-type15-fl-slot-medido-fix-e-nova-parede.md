# TYPE15: o slot muda para 'Orbs' entre as duas chamadas — medido, corrigido na
# origem, e uma nova parede a seguir

**Data:** 2026-07-31 · **Medido 3/3 corridas** em `boot_gow2_type15_probe` (bug) e
`boot_gow2_type15_fix` (fix), ambos binários de teste dedicados, nunca sobre o
`boot_gow2` de produção. Recipe: `arm_menu_fast_recipe` (lib_boot_chain_metrics.sh)
+ `PS3_TRACE_TYPE15_FL=1 PS3_TRACE_FACTORY=1`, timeout 90s, kill sempre por PID.

## A medição pedida, respondida sem inferência

Instrumentei `func_0039E794` (`recomp_macos_v2/ppu_recomp_001.cpp:272286-272393`)
com fl/head/slot/count à entrada e à saída de cada chamada
(`recomp_mid_v2/patch_type15_fl_slot_probe.py`, gate `PS3_TRACE_TYPE15_FL=1`,
OFF por default). 3/3 corridas do binário SEM fix, byte a byte idênticas:

```
[TYPE15FL] pre#1  fl=0x47D00400 head=0x40100638 slot=0x40100844 count=1
[FACTORY]  39E794 leave this=0x47D00000 product r3=0x42F85AE4        <- valido
[TYPE15FL] post#1 fl=0x47D00400 head=0x40100638 slot=0x40100844 count=1   <- IGUAL a pre#1
[TYPE15FL] pre#2  fl=0x47D00400 head=0x40100638 slot=0x4F726273 count=1   <- "Orbs"
[FACTORY]  39E794 leave this=0x47D00000 product r3=0x4F72626F        <- 'Orbo'
[TYPE15] freelist REPLENISH fl=0x47D00400 mid=0x47D00418 shell=0x47D00800 count=1
[TYPE15FL] post#2 fl=0x47D00400 head=0x47D00418 slot=0x47D00804 count=1   <- reparado
```

Respostas directas às três perguntas do pedido:

1. **O slot muda de válido para 'Orbo' entre as chamadas?** Não exactamente —
   muda para **'Orbs'** (`0x4F726273`), uma palavra ASCII vizinha mas distinta
   do produto 'Orbo' (`0x4F72626F`) que a fábrica devolve. `head` (o endereço
   onde o slot vive) é **sempre o mesmo**, `0x40100638`, nas duas chamadas — não
   é o ponteiro que muda, é o CONTEÚDO nesse endereço que fica stream-stomped
   por outra alocação do guest entre as duas chamadas (o guest constrói ~15
   OUTRAS fábricas de tipos diferentes nesse intervalo — ver linhas 3800-3809
   do log).
2. **O reparador dispara?** Sim. A condição de `host_gow2_factory.cpp:512`
   cobre `'Orbs' >= 0x4F000000` tal como cobre `'Orbo'`, e o log confirma
   `[TYPE15] freelist REPLENISH ...` entre `pre#2` e `post#2`.
3. **E não resolve — porquê?** Porque o reparador corre no **epílogo** de
   `func_0039E794` (bloco "Always snap successful products"), ou seja DEPOIS
   das duas chamadas internas (`ps3_call_opd`) que já executaram o construct
   guest e já leram o `slot` corrompido. `post#2` mostra o estado bom — mas é
   bom só para uma HIPOTÉTICA 3ª chamada, nunca para a que acabou de correr.

## Causa raiz (não é o índice, não é o reparador — é o REHOME)

`ps3_type15_note_resolve` (REHOME, `host_gow2_factory.cpp:388-468` /
`ppu_recomp_001.cpp:272077+`) copia a free-list antiga (`fl`, em `0x401xxxxx`)
para a zona pinada com um `vm_write32(k_fl_pin+off, vm_read32(fl+off))` **RAW**
— byte a byte, sem traduzir ponteiros internos. O primeiro word da free-list
antiga (`*fl` = `fl+0x18`, o "mid" da mesma estrutura) é um ponteiro
AUTO-REFERENTE: aponta para dentro do PRÓPRIO blob copiado. O copy raw não
sabia disso — copiou o valor tal e qual, deixando o `head` da free-list PINADA
a apontar de volta para a arena ANTIGA, não protegida:

```
[TYPE15] REHOME freelist old=0x40100620 pin=0x47D00400 *fl=0x40100638 fl+4=0x00000001
                                                            ^^^^^^^^^^
                                    deveria ser 0x47D00418 (k_fl_pin+0x18)
```

Ou seja: o REHOME pinou o ponteiro EXTERIOR (`this+0x24` agora aponta para
`k_fl_pin`), mas o ponteiro INTERIOR (o `head` dentro do blob copiado) ficou
pendurado (dangling) na arena antiga — um nível de indirecção pinado, o
seguinte não. É o mesmo padrão de bug já documentado para a vt do objecto
factory (a stomp `"_Cou"`), agora a atingir a free-list em vez do word0.

## Fix na origem (não é reparador tardio, não é CRC bypass, não é forjar)

Depois do copy raw, traduzir qualquer palavra copiada cujo valor caia dentro de
`[fl, fl+0x200)` para o mesmo deslocamento dentro de `[k_fl_pin, k_fl_pin+0x200)`
— exactamente a relocação que um copy de estrutura ligada precisa para ficar
auto-contido (o mesmo layout que `ps3_type15_freelist_replenish` já constrói de
raiz, agora também aplicado ao copy do REHOME).

- **Fonte versionada:** `host_gow2_factory.cpp` (patch manual, commit pendente).
- **Fix reproduzível no lift:** `recomp_mid_v2/patch_type15_fl_rehome_fixup.py`
  (idempotente, aplica-se a `ppu_recomp_001.cpp`, marcador
  `TYPE15-FL-REHOME-FIXUP`, verificado com re-corrida = `ALREADY`, rc=0).
- **Probe usado para medir e para verificar o fix:**
  `recomp_mid_v2/patch_type15_fl_slot_probe.py` (marcador
  `TYPE15-FL-SLOT-PROBE`, gate `PS3_TRACE_TYPE15_FL=1`, OFF por default,
  read-only).

Verificado 3/3 corridas de `boot_gow2_type15_fix` (lift `recomp_macos_v2.type15fl_fix`,
NUNCA `recomp_macos_v2` de produção):

```
[TYPE15] REHOME freelist old=0x40100620 pin=0x47D00400 *fl=0x47D00418 fl+4=0x00000001
                                                            ^^^^^^^^^^ agora dentro do pin
[TYPE15FL] pre#1  fl=0x47D00400 head=0x47D00418 slot=0x40100844 count=1
[FACTORY]  39E794 leave this=0x47D00000 product r3=0x42F85AE4
[TYPE15FL] pre#2  fl=0x47D00400 head=0x47D00418 slot=0x40100844 count=1   <- ja NAO corrompe
[FACTORY]  39E794 leave this=0x47D00000 product r3=0x00000000            <- mas agora NULL
[TYPE15FL] pre#3  fl=0x47D00400 head=0x47D00418 slot=0x40100844 count=1
[FACTORY]  39E794 leave this=0x47D00000 product r3=0x00000000
```

**A corrupção ASCII ('Orbs'/'Orbo') desaparece por completo, 3/3.** `head` fica
estável em `0x47D00418` (dentro da zona pinada) nas três chamadas — a arena
antiga deixa de ser lida através deste caminho.

## Mas abre-se uma parede nova: NULL em vez de garbage

`slot` (`0x40100844`, o endereço do "shell"/template) nunca muda entre as três
chamadas — nem `count` (fica sempre `1`). O 1º construct usa esse shell e
devolve produto válido. O 2º e o 3º construct, lendo o MESMO shell nunca
avançado, devolvem **`r3=0x00000000`** em vez de garbage. O guest não faz
null-check do que recebeu de volta e passa a martelar chamadas indirectas por
resolver:

```
$ grep -c '^\[ppu\] unresolved indirect call' <log>
  bug (Orbo):  37   (limitado)
  fix (NULL): 9831 + 9831   (a martelar 2 alvos em loop apertado, esgota os 90s)
```

Isto é **pior para progresso imediato** do que o bug original neste recorte de
90s: o processo gasta o timeout inteiro a martelar chamadas indirectas por
resolver em vez de continuar a cadeia. `thr_auto_load() end` fica em 0 em
ambos (bug e fix) dentro desta janela — **não regride nem avança** nesse
critério específico, mas o comportamento pós-construct é claramente mais
disruptivo com o fix.

Métricas de não-regressão anteriores à parede (idênticas bug vs fix):
`startseq=2 thr_end=0 r_perma=1 nopic=4` nas 4 corridas. `st620` máx=11 nas
janelas capturadas em ambos.

## Hipótese para a próxima medição (não verificada — é INFERÊNCIA)

`count` nunca decrementa nem `head` avança em NENHUMA das duas versões (bug ou
fix) — isto já era verdade ANTES do fix (`post#1 == pre#1` byte a byte). Ou
seja: o mecanismo de "pop" da free-list nunca modifica os campos que este
probe lê. Duas hipóteses, nenhuma confirmada:

1. O `slot` (shell) tem uma flag de "já usado" dentro do PRÓPRIO objecto
   (não no header fl/head) que o construct verifica antes de copiar — e essa
   flag foi consumida na 1ª chamada, nunca reposta porque o REHOME também não
   sabia sobre ela (mesma classe de bug: copy raw sem semântica).
2. O CB56C não devia chamar esta fábrica pinada uma 2ª vez para o mesmo tipo
   — a causa raiz estaria a montante (porque é que type=0x15 é despachado
   duas vezes), não dentro de `func_0039E794`.

Distinguir as duas exige uma medição dedicada (dump do shell em `0x40100844`
antes/depois da 1ª chamada, e/ou instrumentar o `CB56C` para ver se a 2ª
invocação de tipo 0x15 é uma repetição legítima do guest ou um efeito
colateral de outro caminho).

## Estado dos artefactos

- `host_gow2_factory.cpp`: fix aplicado (não commitado ainda).
- `recomp_mid_v2/patch_type15_fl_slot_probe.py`: novo, idempotente, PROBE
  (gated, OFF por default) — sobrevive a re-lift.
- `recomp_mid_v2/patch_type15_fl_rehome_fixup.py`: novo, idempotente,
  FUNCIONAL (fix de correctude, sempre activo, sem gate) — sobrevive a re-lift.
- `recomp_macos_v2` (produção): **NÃO tocado.** O fix só foi aplicado e testado
  em clones isolados (`recomp_macos_v2.type15fl_probe`,
  `recomp_macos_v2.type15fl_fix`) e binários de teste dedicados
  (`boot_gow2_type15_probe`, `boot_gow2_type15_fix`).
- `smoke_chain_gate.sh` completo **NÃO corrido** — `thr_end` não passou (ficou
  em 0 nas duas versões dentro da janela de 90s), portanto não se aplicava a
  condição do pedido ("se o fix fizer o thr_end passar, corre o gate
  completo"). A nova parede (NULL sem null-check) precisa de ser fechada
  primeiro.
