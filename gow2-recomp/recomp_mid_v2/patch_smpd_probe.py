#!/usr/bin/env python3
"""PS3_TRACE_SMPD -- sonda discriminadora B vs H-hook (plano EOS, Task 1).

WHY
---
Depois do wall [B] (st620 "parece" parar em 3, WADs nunca abrem), falta saber
se o arme do read-hook de EOS (`g_movie_eos_ea`, ver
`../../ps3recomp/runtime/ppu/ppu_loader.cpp:722-726`) chega mesmo a ser LIDO
com valor 1 por algum dos gates reais da FSM (classe B: fix no consumidor) ou
se nenhum gate o ve' (classe H-hook: o hook nao cobre o leitor certo). Esta
sonda e' SO' leitura: nunca escreve `obj+0x620` (st620) nem `obj+0x744`.

O QUE O BRIEF ORIGINAL ERRAVA (RE re-verificada nesta task, grep por
`vm_read8(ctx->gpr[30] + 0x744)` no lift `recomp_macos_v2/ppu_recomp_*.cpp`)
-------------------------------------------------------------------------
O brief listava `func_002C0508 / func_002C0570 / func_002C05C8` como os 3
leitores de `+0x744`. Essa lista e' aproximada e parcialmente errada:

  * `func_002C0508` (o "pump", `ppu_recomp_001.cpp:36482`) despacha por
    JUMP TABLE lendo **st620** (`obj+0x620`), nao `+0x744`. O `+0x744` que o
    grep acha DENTRO do corpo textual desta funcao (`:36550`) fica DEPOIS de
    um `ps3_indirect_tail(ctx); return;` incondicional (`:36525`) sem label
    a apontar para ali -- e' codigo morto (artefacto do sweep linear do
    lifter sobre os bytes da tabela de saltos + o handler que se segue no
    binario original). Nunca executa. O mesmo padrao repete-se em
    `func_002C0570` (`ppu_recomp_002.cpp:224667`, e' o clone re-entrante do
    pump chamado por `func_002C0614` quando `obj+0x71C!=0`): a leitura de
    `+0x744` em `:224703` e' igualmente codigo morto a seguir ao seu proprio
    `ps3_indirect_tail`.
  * `func_002C05C8` (`ppu_recomp_002.cpp:224742`) o brief acertou o endereco
    por aproximacao, mas nao percebeu o papel: NAO e' o handler do estado 3
    (isso e' `func_002C05F8`, ja coberto por `patch_st3_probe.py` /
    `PS3_TRACE_ST3` -- nao voltar a sondar). E' a funcao de RETRY chamada por
    `func_002C07D8` (ver abaixo) quando o estado 1 ainda nao viu `+0x744!=0`;
    o SEU corpo (repete a mesma logica de `func_002C05F8`: poll de
    `func_002B4224`, escreve st620=2->3, so' DEPOIS le' `+0x744`) e' RE-
    ALCANCAVEL a cada retry -- portanto e' um gate real, so' que por acaso.
  * O leitor que faltava ao brief por completo: `func_002C07D8`
    (`ppu_recomp_001.cpp:36760`), o handler do ESTADO 1 (jump table:
    st620==1 -> 0x2C07D8). Lê `+0x744` como primeira instrucao; `!=0` avanca
    (st620=2->3, trampolim para `func_002C05F8`); `==0` chama
    `func_002C05C8` (o retry acima). Este e' o gate mais cedo na FSM.

Grep completo por `vm_read8(ctx->gpr[30] + 0x744)` no lift (2026-07-20, commit
`80e44fa` + patches desta sessao) da' 10 ocorrencias em 4 ficheiros. Deste
total, 3 sao sondados aqui como gates de leitura (Sites A/B/E), 1 ja' esta'
sondado por `patch_st3_probe.py` (002C05F8), 2 sao codigo morto (mesmo
padrao "depois de indirect_tail sem label", dentro de
`func_002C0508`/`func_002C0570` -- nunca executam) e os ultimos 4
(`func_002C0788`/estado 10, `func_002C07F8`/estado 11 em
`ppu_recomp_001.cpp`, mais os espelhos standalone
`func_002C0804`/`ppu_recomp_005.cpp:54714` e
`func_002C082C`/`ppu_recomp_021.cpp:538089`, clones re-entraveis do loop de
"playing" do estado 11) ficavam FORA do escopo NESTA contagem original (RE
inicial da task). FIX (review findings 1+2 -- ver seccao dedicada ao fim
deste docstring): `func_002C0788` (estado 10) ganha o Site G, pos-arm-gated;
`func_002C05F8` (estado 3) ganha o Site F, tambem pos-arm-gated, convivendo
com o ST3-PROBE pre-existente sem o substituir nem duplicar. `func_002C07F8`
(estado 11) e os 2 espelhos standalone continuam fora do escopo -- nao
pedidos pela review.

SITES SONDADOS (7, read-only)
------------------------------
| Site | Funcao        | Ficheiro:linha (lift actual)   | O que loga |
|------|---------------|---------------------------------|------------|
| A    | func_002C07D8 | ppu_recomp_001.cpp:36761        | [EOSGATE] site=002C07D8 val + branch |
| B    | func_002C05C8 | ppu_recomp_002.cpp:224755       | [EOSGATE] site=002C05C8 val + branch |
| C    | func_002BFF88 | ppu_recomp_001.cpp:36203 (antes)| [STOP] SMPD prev_st620 + r3/r4 |
| D    | func_002BFF88 | ppu_recomp_001.cpp:36147 (apos) | [STOPENTRY] obj + st620 (qualquer ramo) |
| E    | func_002C069C | ppu_recomp_001.cpp:36660 (apos) | [EOSGATE] site=002C069C val + branch |
| F    | func_002C05F8 | ppu_recomp_001.cpp:36611 (apos) | [EOSGATE] site=002C05F8 val+branch+armed_ea, SO pos-arme (fix) |
| G    | func_002C0788 | ppu_recomp_001.cpp:36759 (apos) | [EOSGATE] site=002C0788 val+branch+armed_ea, SO pos-arme (fix) |

Site C (MovieStop): o ramo comum `loc_002C0048:` (com ou sem teardown de
1..0xA) faz um broadcast 'SMPD' (0x534D5044) / tamanho 0x1C / param 0 via
`func_0043FF30`, ANTES de escrever `st620=0`. No lift, os registos r3..r9
desse broadcast sao carregados umas linhas ANTES do proprio write (agenda-
mento tipico de compilador PPC) -- por isso r3/r4 ja' valem 'SMPD'/0x1C no
ponto da sonda, sem qualquer escrita extra. O valor "anterior" de st620 nao
sobrevive em nenhum registo nesse ponto (gpr[0] ja foi reciclado para 0, o
valor a escrever) -- a sonda faz UMA leitura extra (`vm_read32`) do proprio
`obj+0x620` ainda-nao-escrito, puramente para logar; nao toca em `ctx->gpr`
nem precede/substitui o write real, que fica intacto.

func_002C05F8 (estado 3, `ppu_recomp_001.cpp:36595`) MANTEM-SE sondado so'
por `patch_st3_probe.py` / `PS3_TRACE_ST3` -- nao duplicado aqui.

SITES D/E (adicionados DEPOIS do GREEN #1 empirico -- ver abaixo)
------------------------------------------------------------------
O GREEN #1 (arm real, `PS3_MOVIE_DONE_MS=5000`, 40s) revelou um buraco na
cobertura acima: o read-hook do motor (`[MOVIEEOS] read-hook HIT`, log
INCONDICIONAL do proprio `ppu_loader.cpp`, nao desta sonda) disparou 3x logo
a seguir ao arme, e o sampler independente (`movie_eos_arm.c`, `[MOVIEFSM]`)
registou `st620 3 -> 0` pouco depois -- ou seja, ALGUM Stop aconteceu. Mas
nem os Sites A/B (cada um so' dispara UMA vez, na transicao inicial 0->1->3,
muito antes dos 5s do arme -- estrutural, nao e' questao de cap) nem o Site C
(so' cobre o ramo comum `1..0xA` de `func_002BFF88`) nem o ST3-PROBE
pre-existente (400 capado, EXAUSTO 10 linhas ANTES do arme neste log -- o
"trap" que o brief avisava: `grep -n 'arming EOS'` = linha 22250,
`grep -n ST3-tag | tail -1` = linha 22240) mostraram qualquer `val=1` nem
qualquer `[STOP]`. RE de `func_002BFF88` mostra que ela tem um 3o ramo, so'
tomado quando `st620==0xB` (11, "playing"): trampolina para `func_002C008C`
(`ppu_recomp_001.cpp:36221`), que por sua vez trampolina para
`func_002BFFE0`/`func_002BFFF4` (`ppu_recomp_002.cpp:224556`/`224604`) --
MESMO padrao de broadcast SMPD + write st620=0 do Site C, so' que num sitio
que a task original nao listava (porque a RE estatica que gerou o brief nao
tinha como prever que o arme empurraria a FSM ate' ao estado 0xB antes do
Stop). Para fechar o buraco SEM reescrever os Sites A/B/C nem tocar
`patch_st3_probe.py`, dois sites novos, ambos read-only:

  * Site D -- `func_002BFF88`, logo a seguir a' PRIMEIRA leitura de st620
    (entrada da funcao, `ctx->gpr[0] = vm_read32(ctx->gpr[31] + 0x620);`,
    ANTES de qualquer branch). Loga TODAS as chamadas ao MovieStop,
    qualquer que seja o ramo tomado a seguir -- inclui o `st620==0xB` que o
    Site C (no write comum `1..0xA`) nao ve.
  * Site E -- `func_002C069C` (estado 4, `ppu_recomp_001.cpp:36659`), o
    PRIMEIRO gate de `+0x744` a jusante do estado 3 na jump table. Se isto
    mostrar `val=1`, e' prova directa e nao-capada (ao contrario do
    ST3-PROBE) de que a FSM avancou por causa do arme.

AMBIGUIDADE DO GATE ENV (resolvida por instrucao explicita desta task)
-----------------------------------------------------------------------
Nome novo `PS3_TRACE_SMPD` (NAO reutiliza `PS3_TRACE_ST3`), para as duas
sondas serem controlaveis independentemente. Marker: `SMPD-PROBE`. OFF por
default (sem a env, zero linhas `[EOSGATE]`/`[STOP]` -- baseline identico).

ANCORAGEM (por que region-scoped em vez de `str.replace` piano no ficheiro
inteiro, ao contrario de `patch_st3_probe.py` mas IGUAL a
`patch_fios_sticky.py`, o script idempotente mais recente deste dir)
-----------------------------------------------------------------------
O corpo de `func_002C05C8` (site B) e' TEXTUALMENTE IDENTICO ao codigo morto
que sobra dentro de `func_002C0570` a seguir ao seu `ps3_indirect_tail`
(mesmo bytes de guest, `0x2C05C8`-`0x2C0614`, duplicados pelo lifter -- ver
acima). As 3 linhas que terminam na leitura de `+0x744` aparecem 2x em
`ppu_recomp_002.cpp`: uma vez (morta) dentro de `func_002C0570`, outra vez
(real) como o corpo de `func_002C05C8`. Um `str.replace(needle, insert, 1)`
ingenuo no ficheiro inteiro apanharia a ocorrencia ERRADA (a morta, que nunca
loga nada) sem sequer falhar. Por isso as insercoes sao aplicadas dentro da
REGIAO da funcao-alvo (assinatura ate' ao proximo `void func_`), com
`region.count(ancora)` a exigir exactamente 1 dentro dessa regiao -- se for
0 ou >1, `SystemExit` (fail loud; nunca forcar). E' "`str.replace` sobre uma
fatia" do texto, nao `re.sub` (escapes de regex corrompem C literal).

Idempotente por site (nao por ficheiro inteiro): cada `_edit_after`/
`_edit_before` verifica se `ancora+bloco` (ou `bloco+ancora`) ja' esta' na
regiao antes de tentar aplicar -- uma 2a corrida, ou uma corrida parcial
anterior interrompida a meio, fica ALREADY site a site e completa o que
faltar, em vez de um unico gate "MARKER in ficheiro inteiro" que esconderia
uma aplicacao parcial.

FIX (review findings 1+2 do Task 1, aplicado sem tocar patch_st3_probe.py)
----------------------------------------------------------------------------
Finding 1 (Important): a classificacao B do relatorio original era INFERIDA,
nao MEDIDA -- st620 chegou a 11 (Site D apanhou o Stop no ramo 0xB), logo os
gates dos estados 3 e 10 "tinham" de ter lido +0x744!=0, mas nenhum dos dois
tinha sido medido DIRECTAMENTE pos-arme: o estado 3 (func_002C05F8) so' tinha
o ST3-PROBE pre-existente, cujo cap de 400 esgota ANTES do arme (medido no
relatorio: `arming EOS` na linha 22250 de smpd_arm1.log, ultimo `[ST3]` na
22240 -- os 400 val=0 sao TODOS pre-arme, inconclusivos); o estado 10
(func_002C0788), confirmado por RE a ler +0x744, nunca tinha sido
instrumentado (ficava listado como candidato "Site F+", nunca aplicado).

Dois sites novos, MESMO marker/gate desta sonda (SMPD-PROBE / PS3_TRACE_SMPD),
mas com uma SEGUNDA condicao: so' logam quando `g_movie_eos_ea!=0` (o arme ja
aconteceu). Isto evita reproduzir a armadilha do cap do ST3-PROBE, que conta
TODAS as chamadas (armadas ou nao) -- aqui o cap de 400 so' comeca a contar
DEPOIS do arme, nunca e' gasto em leituras pre-arme irrelevantes.
`g_movie_eos_ea` e' o global do host que o proprio read-hook de EOS ja usa
(`ps3recomp/runtime/ppu/ppu_loader.cpp:722`, `uint32_t`, linkage C++ direto,
sem name-mangling de variavel -- mesmo padrao ja' usado em
`ps3recomp/runtime/ppu/tests/boot_main.cpp:212`); nao e' um mecanismo novo, e'
o MESMO sinal de armamento que ja' condiciona a task inteira. Le-lo e'
host-side, read-only puro -- nao toca `ctx->gpr` nem memoria guest, e nao
chama `vm_read8(+0x744)` de novo (uma leitura extra ali perturbaria o proprio
read-hook que se quer medir -- usa-se sempre o `ctx->gpr[0]` ja computado pelo
lifter na leitura original, exactamente como os outros sites).

  * Site F -- `func_002C05F8` (estado 3). Ancora identica a' do ST3-PROBE (a
    leitura crua de +0x744), mas o `_edit_after` e' scoped a' regiao desta
    funcao: encontra a ancora 1x (o ST3-PROBE nao a duplica, so' acrescenta
    texto a seguir dela) e insere o Site F logo depois -- ANTES do bloco
    ST3-PROBE ja' presente. Sem conflito: cada bloco e' um `{ }` fechado com
    os seus proprios `static` de escopo de bloco (nomes iguais, `_on`/`_n`,
    mas objectos DIFERENTES em blocos lexicos diferentes -- mesmo precedente
    ja' usado pelos Sites C+D dentro da mesma func_002BFF88). Nao edita nem
    reordena `patch_st3_probe.py`; convive com ele.
  * Site G -- `func_002C0788` (estado 10). Primeiro site desta sonda nessa
    funcao (antes intocada). Mesmo padrao `branch=adv|wait` derivado de
    `val!=0` que A/B/E ja' usam (nao da CR do PPC, do proprio valor lido --
    fiel ao que o branch real faz: `cr&2` (EQ, val==0) desvia para o retry
    `loc_002C07CC`; caso contrario cai directo no avanco para `loc_002C0794`).

Ambos os sites tambem imprimem `armed_ea=0x%08X` (o proprio `g_movie_eos_ea`)
para cross-referencia directa contra o `ea=` do mesmo log -- confirma, linha
a linha, que o site esta' a olhar para o MESMO endereco que o arme registou,
sem depender de inferencia nenhuma.

Finding 2 (Important, sem impacto de codigo nesta sonda): os comandos de
verificacao do relatorio terminavam com um `pkill -9 -f boot_gow2` NU depois
do kill por PID -- pode matar o boot de OUTRA sessao concorrente (a regra do
plano e' matar so' pelo PID capturado, TERM depois -9, nunca pkill nu). Fix
nos comandos de re-verificacao usados para testar este ficheiro, documentado
no relatorio (seccao "Fix (findings 1+2)"), nao no script.

CORRECCAO 2026-07-25 (re-lift: handlers da FSM deixaram de ser funcoes)
------------------------------------------------------------------------
Contra um lift limpo do ppu_lifter.py actual, 5 dos 6 alvos deixaram de
existir como `void func_XXXXXXXX(...)`:

    func_002C07D8, func_002C05C8, func_002C069C, func_002C05F8, func_002C0788
    -> "funcao(oes) nunca encontrada(s) em nenhum ppu_recomp_*.cpp"

O COMPORTAMENTO NAO DESAPARECEU -- mudou de forma. O lifter passou a resolver
a jump table do pump: `func_002C0508` tem agora

    switch ((uint32_t)ctx->ctr) { ... case 0x002C07D8u: goto loc_002C07D8; ... }

e cada handler de estado e' um LABEL (`loc_002C05F8:`, `loc_002C069C:`,
`loc_002C0788:`, `loc_002C07D8:`) DENTRO de func_002C0508, em vez de um
fragmento standalone duplicado. As leituras de `+0x744` continuam la',
uma por handler (verificado: 5 ocorrencias de
`vm_read8(ctx->gpr[30] + 0x744)` em ppu_recomp_001.cpp, nas linhas dos
labels 05F8 / 069C / 0788 / 07D8 / 082C).

Consequencia deste re-shape para os Sites B e F: no lift ANTIGO havia DUAS
copias textuais da MESMA instrucao guest `0x002C05F8` (uma dentro do
fragmento duplicado `func_002C05C8`, que caia por fall-through
05C8 -> 05F0 -> 05F8, e outra em `func_002C05F8`). O lifter novo emite UMA
so'. Para NAO perder a distincao que os relatorios usam
(`site=002C05C8` = gate alcancado pelo retry do estado 1;
`site=002C05F8` = gate alcancado pelo estado 3 via jump table ou por 07D8),
introduz-se uma flag LOCAL do host em func_002C0508 (`_smpd_from05C8`,
`int`, inicializada a 0 no topo da funcao e posta a 1 no `loc_002C05C8`).
Nao toca em `ctx->gpr` nem em memoria guest, nao altera fluxo, e' apenas o
registo do caminho tomado -- os dois blocos de log ficam entao ancorados na
mesma leitura, mutuamente exclusivos pela flag, reproduzindo exactamente as
duas linhas de log que o lift antigo produzia.

O script aceita AS DUAS FORMAS: se `void func_002C05F8(...)` existir (lift
antigo) usa a regiao dessa funcao e nao mexe na flag; caso contrario procura
o label dentro de `func_002C0508`. Os Sites C/D (`func_002BFF88`) nao foram
afectados -- essa funcao continua standalone.

Idempotencia: passou a ser por TAG unico dentro do bloco (`SMPD-PROBE#A`...)
em vez de "ancora+bloco contiguos". Com dois blocos (B e F) a partilhar a
mesma ancora, o teste antigo deixava de casar a partir da 2a insercao e o
script re-inseria em cada corrida.

Uso:  patch_smpd_probe.py [DIR_DE_LIFT]     (default: ../recomp_macos_v2)
Reaplicado por ../apply_all_patches.sh apos cada re-lift (nao tem check
registado em apply_all_patches.sh -- e' sonda pura, sem efeito funcional,
mesmo precedente de patch_st3_probe.py, que tambem nao tem).
"""
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lift_paths import resolve_lift_paths  # noqa: E402

MARKER = "SMPD-PROBE"
# No lift novo os handlers da FSM sao labels DENTRO deste pump (jump table
# resolvida pelo lifter); no lift antigo eram fragmentos standalone.
CONTAINER = "func_002C0508"

ROOT_DEFAULT = str(Path(__file__).resolve().parent.parent / "recomp_macos_v2")

# Ancora partilhada por A/B/E/F/G: a leitura crua de obj+0x744.
READ744 = "        ctx->gpr[0] = vm_read8(ctx->gpr[30] + 0x744);\n"

ON = ("{ static int _on=-1; if(_on<0){extern char* getenv(const char*);\n"
      "            const char* _e=getenv(\"PS3_TRACE_SMPD\"); _on=(_e&&*_e&&*_e!='0')?1:0;}\n")

# Flag de caminho (so' usada quando os handlers sao labels): distingue o gate
# 0x002C05F8 alcancado pelo retry do estado 1 (loc_002C05C8 -> 05F0 -> 05F8)
# do mesmo gate alcancado pelo estado 3 / por loc_002C07D8.
FLAG_DECL = (
    "        /* " + MARKER + "#FLAG: caminho ate' ao gate 0x002C05F8 (host-only, nao toca guest) */\n"
    "        int _smpd_from05C8 = 0; (void)_smpd_from05C8;\n"
)
FLAG_SET = (
    "        /* " + MARKER + "#FLAGSET: entrou pelo retry do estado 1 */\n"
    "        _smpd_from05C8 = 1;\n"
)


def _blk(tag, body):
    return "        /* " + MARKER + "#" + tag + ": " + body


# ---- Site A: estado 1 (guest 0x002C07D8) ------------------------------------
SITE_A_BLOCK = _blk("A", "gate EOS do estado 1 (0x002C07D8) -- brief nao listava este site */\n") + (
    "        " + ON +
    "          if(_on){ static int _n=0; if(_n++<400){\n"
    "            fprintf(stderr,\"[EOSGATE] site=002C07D8 obj=0x%08X ea=0x%08X val=%u branch=%s\\n\",\n"
    "              (uint32_t)ctx->gpr[30],(uint32_t)(ctx->gpr[30]+0x744),(unsigned)ctx->gpr[0],\n"
    "              ((unsigned)ctx->gpr[0]!=0)?\"adv\":\"wait\");\n"
    "            fflush(stderr); } } }\n"
)

# ---- Site B: gate alcancado pelo retry 0x002C05C8 ----------------------------
# Lift antigo: corpo proprio de func_002C05C8. Lift novo: mesma leitura de
# loc_002C05F8, discriminada por _smpd_from05C8.
SITE_B_BLOCK_FUNC = _blk("B", "gate EOS de func_002C05C8 (retry do estado 1) */\n") + (
    "        " + ON +
    "          if(_on){ static int _n=0; if(_n++<400){\n"
    "            fprintf(stderr,\"[EOSGATE] site=002C05C8 obj=0x%08X ea=0x%08X val=%u branch=%s\\n\",\n"
    "              (uint32_t)ctx->gpr[30],(uint32_t)(ctx->gpr[30]+0x744),(unsigned)ctx->gpr[0],\n"
    "              ((unsigned)ctx->gpr[0]!=0)?\"adv\":\"wait\");\n"
    "            fflush(stderr); } } }\n"
)
SITE_B_BLOCK_LABEL = _blk("B", "gate 0x002C05F8 alcancado pelo retry 0x002C05C8 (estado 1) */\n") + (
    "        " + ON +
    "          if(_on && _smpd_from05C8){ static int _n=0; if(_n++<400){\n"
    "            fprintf(stderr,\"[EOSGATE] site=002C05C8 obj=0x%08X ea=0x%08X val=%u branch=%s\\n\",\n"
    "              (uint32_t)ctx->gpr[30],(uint32_t)(ctx->gpr[30]+0x744),(unsigned)ctx->gpr[0],\n"
    "              ((unsigned)ctx->gpr[0]!=0)?\"adv\":\"wait\");\n"
    "            fflush(stderr); } } }\n"
)

# ---- Site C: func_002BFF88 (MovieStop), antes do write st620=0 ---------------
SITE_C_FUNC = "func_002BFF88"
SITE_C_ANCHOR = "        vm_write32(ctx->gpr[31] + 0x620, ctx->gpr[0]);\n"
SITE_C_BLOCK = _blk("C", "MovieStop antes do write st620=0 (r3/r4 ja setados p/ broadcast SMPD/0x1C) */\n") + (
    "        " + ON +
    "          if(_on){ static int _n=0; if(_n++<400){\n"
    "            uint32_t _prev_st620 = (uint32_t)vm_read32(ctx->gpr[31] + 0x620);\n"
    "            fprintf(stderr,\"[STOP] SMPD obj=0x%08X prev_st620=%u r3=0x%08X r4=0x%08X ->0\\n\",\n"
    "              (uint32_t)ctx->gpr[31], _prev_st620, (uint32_t)ctx->gpr[3], (uint32_t)ctx->gpr[4]);\n"
    "            fflush(stderr); } } }\n"
)

# ---- Site D: func_002BFF88, entrada (ANY branch) -----------------------------
SITE_D_ANCHOR = "        ctx->gpr[0] = vm_read32(ctx->gpr[31] + 0x620);\n"
SITE_D_BLOCK = _blk("D", "entrada do MovieStop (func_002BFF88), TODOS os ramos (<1 / 1..0xA / 0xB) */\n") + (
    "        " + ON +
    "          if(_on){ static int _n=0; if(_n++<400){\n"
    "            fprintf(stderr,\"[STOPENTRY] func_002BFF88 obj=0x%08X st620=%u\\n\",\n"
    "              (uint32_t)ctx->gpr[31],(unsigned)ctx->gpr[0]);\n"
    "            fflush(stderr); } } }\n"
)

# ---- Site E: estado 4 (guest 0x002C069C) -------------------------------------
SITE_E_BLOCK = _blk("E", "gate EOS do estado 4 (0x002C069C), 1o a jusante do estado 3 */\n") + (
    "        " + ON +
    "          if(_on){ static int _n=0; if(_n++<400){\n"
    "            fprintf(stderr,\"[EOSGATE] site=002C069C obj=0x%08X ea=0x%08X val=%u branch=%s\\n\",\n"
    "              (uint32_t)ctx->gpr[30],(uint32_t)(ctx->gpr[30]+0x744),(unsigned)ctx->gpr[0],\n"
    "              ((unsigned)ctx->gpr[0]!=0)?\"adv\":\"wait\");\n"
    "            fflush(stderr); } } }\n"
)

# ---- Site F: estado 3 (guest 0x002C05F8), SO pos-arme ------------------------
_F_TAIL = (
    "        " + ON +
    "          if(_on%s){ extern uint32_t g_movie_eos_ea;\n"
    "            if(g_movie_eos_ea){ static int _n=0; if(_n++<400){\n"
    "              fprintf(stderr,\"[EOSGATE] site=002C05F8 obj=0x%%08X ea=0x%%08X val=%%u branch=%%s armed_ea=0x%%08X\\n\",\n"
    "                (uint32_t)ctx->gpr[30],(uint32_t)(ctx->gpr[30]+0x744),(unsigned)ctx->gpr[0],\n"
    "                ((unsigned)ctx->gpr[0]!=0)?\"adv\":\"wait\",g_movie_eos_ea);\n"
    "              fflush(stderr); } } } }\n"
)
SITE_F_BLOCK_FUNC = _blk("F", "gate EOS do estado 3 (func_002C05F8), SO pos-arme; convive com ST3-PROBE */\n") + (_F_TAIL % "")
SITE_F_BLOCK_LABEL = _blk("F", "gate EOS do estado 3 (0x002C05F8), SO pos-arme e SO nao vindo do retry 05C8 */\n") + (_F_TAIL % " && !_smpd_from05C8")

# ---- Site G: estado 10 (guest 0x002C0788), SO pos-arme -----------------------
SITE_G_BLOCK = _blk("G", "gate EOS do estado 10 (0x002C0788), SO pos-arme */\n") + (
    "        " + ON +
    "          if(_on){ extern uint32_t g_movie_eos_ea;\n"
    "            if(g_movie_eos_ea){ static int _n=0; if(_n++<400){\n"
    "              fprintf(stderr,\"[EOSGATE] site=002C0788 obj=0x%08X ea=0x%08X val=%u branch=%s armed_ea=0x%08X\\n\",\n"
    "                (uint32_t)ctx->gpr[30],(uint32_t)(ctx->gpr[30]+0x744),(unsigned)ctx->gpr[0],\n"
    "                ((unsigned)ctx->gpr[0]!=0)?\"adv\":\"wait\",g_movie_eos_ea);\n"
    "              fflush(stderr); } } } }\n"
)

# Ancora antiga do Site B (corpo standalone de func_002C05C8), so' usada quando
# essa funcao ainda existe (lift antigo).
SITE_B_ANCHOR_FUNC = (
    "        ctx->gpr[0] = (int64_t)(int32_t)(3);\n"
    "        vm_write32(ctx->gpr[30] + 0x620, ctx->gpr[0]);\n"
    "        ctx->gpr[0] = vm_read8(ctx->gpr[30] + 0x744);\n"
)

_LABEL_RX = re.compile(r"^loc_[0-9A-Fa-f]+:$", re.M)


def _func_span(t, func):
    sig = "void %s(ppu_context* ctx) {\n" % func
    i = t.find(sig)
    if i < 0:
        return None
    j = t.find("\nvoid func_", i + len(sig))
    return i, (j if j > i else len(t)), len(sig)


def _label_span(t, container, label):
    sp = _func_span(t, container)
    if sp is None:
        return None
    i, end, _ = sp
    k = t.find("\n%s:\n" % label, i, end)
    if k < 0:
        return None
    k += 1
    m = _LABEL_RX.search(t, k + len(label) + 2, end)
    return k, (m.start() if m else end)


def _scope(t, legacy_func, label):
    """(start, end, modo) do sitio -- aceita lift antigo (funcao) e novo (label)."""
    sp = _func_span(t, legacy_func)
    if sp is not None:
        return sp[0], sp[1], "func"
    sp = _label_span(t, CONTAINER, label)
    if sp is not None:
        return sp[0], sp[1], "label"
    return None


def _edit(t, span, anchor, block, tag, before=False):
    """Insere block antes/depois de anchor dentro de span. Idempotente por tag."""
    i, end = span
    region = t[i:end]
    uniq = "/* " + MARKER + "#" + tag + ":"
    if uniq in region:
        return t, "%s: ALREADY" % tag
    c = region.count(anchor)
    if c != 1:
        raise SystemExit(
            "patch_smpd_probe: ancora de %s aparece %dx no seu escopo (esperado 1) -- "
            "shape do lift mudou; reveja a needle antes de forcar" % (tag, c))
    region = region.replace(anchor, (block + anchor) if before else (anchor + block), 1)
    return t[:i] + region + t[end:], "%s: APPLIED" % tag


def patch_file(p: Path, seen: dict):
    t = p.read_text(encoding="utf-8", errors="replace")
    orig = t
    notes = []

    # --- Sites C/D: func_002BFF88 (nao afectada pelo re-shape) ---------------
    sp = _func_span(t, SITE_C_FUNC)
    if sp is not None:
        seen[SITE_C_FUNC] = True
        t, s = _edit(t, (sp[0], sp[1]), SITE_C_ANCHOR, SITE_C_BLOCK, "C", before=True)
        notes.append(s)
        sp = _func_span(t, SITE_C_FUNC)
        t, s = _edit(t, (sp[0], sp[1]), SITE_D_ANCHOR, SITE_D_BLOCK, "D")
        notes.append(s)

    # --- Sites A/B/E/F/G: funcoes (lift antigo) ou labels em func_002C0508 ---
    plan = [
        ("A", "func_002C07D8", "loc_002C07D8", READ744, SITE_A_BLOCK, SITE_A_BLOCK),
        ("E", "func_002C069C", "loc_002C069C", READ744, SITE_E_BLOCK, SITE_E_BLOCK),
        ("G", "func_002C0788", "loc_002C0788", READ744, SITE_G_BLOCK, SITE_G_BLOCK),
        ("F", "func_002C05F8", "loc_002C05F8", READ744, SITE_F_BLOCK_FUNC, SITE_F_BLOCK_LABEL),
        ("B", "func_002C05C8", "loc_002C05F8", None, SITE_B_BLOCK_FUNC, SITE_B_BLOCK_LABEL),
    ]
    need_flag = False
    for tag, legacy, label, anchor, blk_func, blk_label in plan:
        sc = _scope(t, legacy, label)
        if sc is None:
            continue
        start, end, mode = sc
        seen[legacy] = True
        if mode == "func":
            a = anchor if anchor is not None else SITE_B_ANCHOR_FUNC
            blk = blk_func
        else:
            a = READ744
            blk = blk_label
            need_flag = True
        t, s = _edit(t, (start, end), a, blk, tag)
        notes.append(s + ("(label)" if mode == "label" else ""))

    # --- flag de caminho (so' no shape novo) ---------------------------------
    if need_flag:
        sp = _func_span(t, CONTAINER)
        if sp is not None:
            i, end, siglen = sp
            if ("/* " + MARKER + "#FLAG:") not in t[i:end]:
                t = t[:i + siglen] + FLAG_DECL + t[i + siglen:]
                notes.append("FLAG: APPLIED")
            else:
                notes.append("FLAG: ALREADY")
        sp = _label_span(t, CONTAINER, "loc_002C05C8")
        if sp is not None:
            region = t[sp[0]:sp[1]]
            if ("/* " + MARKER + "#FLAGSET:") in region:
                notes.append("FLAGSET: ALREADY")
            else:
                head = "loc_002C05C8:\n"
                if not region.startswith(head):
                    raise SystemExit("patch_smpd_probe: loc_002C05C8 com forma inesperada")
                t = t[:sp[0]] + head + FLAG_SET + region[len(head):] + t[sp[1]:]
                notes.append("FLAGSET: APPLIED")
        else:
            raise SystemExit("patch_smpd_probe: loc_002C05C8 nao encontrado em %s "
                             "-- sem ele o Site B nao e' distinguivel do Site F" % CONTAINER)

    if not notes:
        return "SKIP", t != orig
    if t != orig:
        p.write_text(t, encoding="utf-8", newline="\n")
        return "APPLIED | " + " ; ".join(notes), True
    return "ALREADY | " + " ; ".join(notes), False


def main() -> int:
    files = [p for p in resolve_lift_paths(sys.argv[1:], ROOT_DEFAULT) if p.is_file()]
    if not files:
        print("nenhum ppu_recomp_*.cpp em %s" % (sys.argv[1:] or ROOT_DEFAULT))
        return 1
    seen = {}
    any_hit = False
    for p in files:
        r, _ = patch_file(p, seen)
        if r != "SKIP":
            any_hit = True
            print("%s: %s" % (p.name, r))
    missing = [n for n in ("func_002C07D8", "func_002C05C8", SITE_C_FUNC,
                           "func_002C069C", "func_002C05F8", "func_002C0788")
               if n not in seen]
    if missing:
        raise SystemExit(
            "patch_smpd_probe: sitio(s) nunca encontrado(s) nem como funcao nem "
            "como label em %s: %s (shape do lift mudou?) -- reveja antes de forcar"
            % (CONTAINER, ", ".join(missing)))
    if not any_hit:
        print("SKIP: nada para aplicar")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
