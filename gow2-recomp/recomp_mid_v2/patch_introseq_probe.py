#!/usr/bin/env python3
"""Probe gated da CADEIA DE ARRANQUE ate' a FSM do player de intro.

Porque: `st620` (`[[0x540054]+0x620]`) fica a 0 no boot. A leitura estatica do
lift mostra que existe UM UNICO sitio que escreve 0->1 -- a cauda de
`func_002C00DC` (`Play`, guest 0x2C00DC..0x2C042C) -- e que a sua unica guarda
de entrada e' `st620 == 0`, que HOJE se verifica. Logo `Play` simplesmente nao
e' chamado, e a pergunta passa a ser ONDE e' que a cadeia para.

Cadeia estatica (toda ela linear, nao ha eventos pelo meio):

    func_00010230 (entry) -> func_00010354 -> func_0025C838 -> func_002B2E74
      -> func_000B71B8            (sequencia de boot; chama por ordem:)
           func_002BA4B4
           func_002B6C40
           func_002B7644
           func_0004476C
           func_000CE0A0          <- LOOP DA INTRO
             |  guarda de saida: u8[[TOC-0x5E34] + 4] != 0  => salta a intro
             |  por iteracao: func_00194D3C ; func_000CDE3C ; func_000CDBA4
             +-> func_000CDE3C    <- TICK da FSM da intro (objecto S=[TOC-0x5E70])
                   S[0x00] idx do filme (o loop sai quando == 4)
                   S[0x04] sub-estado (0=play, 1=fade-in, 2=hold, 3=fade-out)
                   S[0x08] alpha do fade (float)
                   S[0x10] flag "timer arrancado"
                   S[0x18] objecto de timer (u64 = tick de partida)
                 -> func_000CE03C / func_000CDF50 -> func_002C00DC(Play "SmLogo_v2")
                                                  -> func_002C0508 (pump 1..11)
           func_00040090

A FSM da intro avanca por TEMPO: `func_00194898` devolve
(now - S[0x18]) / freq, com now = mftb (`func_00195D48`) e freq = u64 em
[TOC-0x35D4] (0x6FF4A8), preenchido por syscall 147 dentro de `func_00195D58`.
Se `freq` for 0, ou se o delta de ticks nao crescer, o fade nunca fecha e o
loop roda para sempre a desenhar -- que e' exactamente o sintoma observado
(SetFlipCommand + "Invalid shader combination" em ciclo, zero conteudo pedido).

Esta probe MEDE qual dos ramos e' o verdadeiro, sem adivinhar:

  * beacons de entrada (one-shot) em cada elo da cadeia de boot -- se aparecer
    `func_0004476C` mas nunca `func_000CE0A0`, o bloqueio e' ANTES da intro;
  * na entrada de `func_000CE0A0`, o valor da guarda de salto da intro;
  * em cada tick de `func_000CDE3C`, o estado completo de S + o `st620` do
    player + o timebase (`now` e `freq`), edge-triggered nas transicoes MAIS um
    heartbeat a cada 64 ticks. O heartbeat nao e' decoracao: sem ele um
    sub-estado que nunca muda deixa de imprimir, e nao se distingue "o fade
    progride devagar" de "o loop parou de tickar" -- que foi precisamente a
    ambiguidade que a 1a versao desta probe deixou em aberto.

RESULTADO DA MEDICAO (2026-07-20, 60 s de boot, PS3_NO_RSX=1):

    enter func_000B71B8 / 002BA4B4 / 002B6C40 / 002B7644 / 0004476C / 000CE0A0
    guardslot=0x006FF480 guardobj=0x4306AA40 u8+4=0   (a intro NAO e' saltada)
    tick #0  idx=0 sub=0 started=0
    tick #1  idx=1 sub=1 started=1  timer=0x518114201CEA
    tick #64 idx=1 sub=1 fade=0.05238  st620=0
    (nunca ha tick #128; nunca ha "enter func_002C00DC" nem "enter func_00040090")

Ou seja: a cadeia chega toda a intro, o timebase esta correcto (freq=0x04C1A6C0
= 79.8 MHz, `now` avanca), o fade progride -- e o loop simplesmente PARA de
tickar ao fim de ~65 iteracoes (~36 ms), sem que `func_000CE0A0` retorne.
`sample(1)` do processo bloqueado da o resto (100% das amostras no mesmo sitio):

    func_000CE0A0 -> func_000CDDE4 (render da intro, bloco de func_000CDBA4)
      -> func_001596F4 -> func_00156D10 -> func_00165B44 -> func_0017EA08
        -> func_0014F7E4 -> func_004B8D98 (import cellSpurs NID 0x8A85674D)
          -> ps3_import_thunk -> ps3_hle_call -> _cellSpursLFQueuePushBody
            -> spurs_task_kick_all -> spu_workload_dispatch_task
              -> spu_job_run_isolated -> spu_run_lifted_job_ex
                -> spu0_spu_func_00003070 ...  (job spu0 a correr, sem terminar)

A parede NAO e' um evento de host em falta a alimentar a FSM do filme: e' o
`cellSpursLFQueuePushBody` do HLE a correr o job spu0 SINCRONAMENTE na thread
principal do PPU, dentro do render da intro. O job nao termina, a thread
principal nunca volta ao loop, a FSM deixa de receber ticks e por isso nunca
chega ao sub-estado 2 -> `Play("SmLogo_v2")` -> `st620 = 1`. Os flips que se
veem no log continuam porque vem de outra thread.

So' LE memoria do guest (vm_read8/32 tem guarda de out-of-bounds e devolvem 0);
nunca escreve. Com PS3_TRACE_INTROSEQ desligada e' um no-op exacto sobre o
baseline (regra 6 do CLAUDE.md -- probes gated por env, OFF por default).

Uso:  patch_introseq_probe.py [DIR_DE_LIFT]    (default: o dir deste ficheiro)
      PS3_TRACE_INTROSEQ=1 ./boot_gow2 EBOOT.ELF 2>&1 | grep INTROSEQ

Reaplicado por ../apply_all_patches.sh apos cada re-lift. Idempotente: a 2a
corrida nao reescreve nada (fica ALREADY-APPLIED no catalogo).

Nota de implementacao: a substituicao e' `str.replace()` textual exacta sobre a
regiao da funcao, NAO `re.sub` com string de substituicao -- essa interpreta
escapes e ja' meteu uma quebra de linha REAL dentro de um literal C, gerando
fonte que nao compila.

Ficheiros/marcadores dos outros patches (nao colidir): `patch_2b3d1c_probe.py`
usa [AREAD] em func_002B3D1C; `patch_oob_ra_probe.py` usa [OOBARG] em
func_00380124/func_00372740. Esta usa [INTROSEQ] e nenhuma dessas funcoes.
"""
from __future__ import annotations

import sys
from pathlib import Path

MARKER = "INTROSEQ-PROBE"
ENV = "PS3_TRACE_INTROSEQ"

# Prologo comum do gate por env. Repetido em cada probe porque cada uma vive
# num escopo de bloco proprio (os `static` sao locais a funcao instrumentada).
GATE = (
    "        { static int _on=-1; if(_on<0){extern char* getenv(const char*);\n"
    '            const char* _e=getenv("' + ENV + "\"); _on=(_e&&*_e&&*_e!='0')?1:0;}\n"
)

# --- elos da cadeia de boot: beacon one-shot de entrada ---------------------
# Ordem = ordem estatica de chamada dentro de func_000B71B8. O primeiro elo que
# NAO aparecer no log e' aquele onde o anterior nunca retornou.
BEACONS = [
    ("func_000B71B8", "sequencia de boot (chama a intro)"),
    ("func_002BA4B4", "boot[1] antes da intro"),
    ("func_002B6C40", "boot[2] antes da intro"),
    ("func_002B7644", "boot[3] antes da intro"),
    ("func_0004476C", "boot[4] ultimo antes da intro"),
    ("func_00040090", "boot[6] logo DEPOIS da intro"),
    ("func_002C00DC", "Play(): a cauda escreve st620=1"),
    ("func_002C0508", "pump da FSM do player (estados 1..11)"),
]


def beacon(name: str, what: str) -> str:
    return (
        "        /* " + MARKER + ": beacon de entrada (" + what + "). So leitura. */\n"
        + GATE
        + "          if(_on){ static int _n=0; if(_n++<3){\n"
        '            fprintf(stderr,"[INTROSEQ] enter ' + name + ' #%d\\n", _n);\n'
        "            fflush(stderr); } } }\n"
    )


# --- func_000CE0A0: beacon + valor da guarda de salto da intro --------------
# Guarda (guest): r30=[TOC-0x5E34]; r3=[r30]; if (u8[r3+4] != 0) return;
CE0A0_PROBE = (
    "        /* " + MARKER + ": entrada do loop da intro + guarda de salto.\n"
    "         * Guest: obj=[[TOC-0x5E34]]; se u8[obj+4]!=0 a intro e' SALTADA. */\n"
    + GATE
    + "          if(_on){ static int _n=0; if(_n++<3){\n"
    "            uint32_t _toc=(uint32_t)ctx->gpr[2];\n"
    "            uint32_t _gp=vm_read32(_toc - 0x5E34);\n"
    "            uint32_t _go=_gp?vm_read32(_gp):0u;\n"
    '            fprintf(stderr,"[INTROSEQ] enter func_000CE0A0 #%d guardslot=0x%08X'
    ' guardobj=0x%08X u8+4=%u (!=0 => intro saltada)\\n",\n'
    "              _n,_gp,_go,_go?vm_read8(_go+4):0u);\n"
    "            fflush(stderr); } } }\n"
)

# --- func_000CDE3C: dump edge-triggered do tick da FSM da intro -------------
# Enderecos vem TODOS do TOC em runtime (r2), como o proprio guest faz -- nada
# de constantes de imagem hardcoded aqui.
CDE3C_PROBE = (
    "        /* " + MARKER + ": tick da FSM da intro, edge-triggered.\n"
    "         * S=[TOC-0x5E70]: +00 idx filme, +04 sub-estado, +08 fade(float),\n"
    "         * +10 flag timer-arrancado, +18 timer(u64 tick de partida).\n"
    "         * st620=[[TOC-0x1124]+0x620]; freq=u64[[TOC-0x35D4]] (syscall 147). */\n"
    + GATE
    + "          if(_on){\n"
    "            uint32_t _toc=(uint32_t)ctx->gpr[2];\n"
    "            uint32_t _S =vm_read32(_toc - 0x5E70);\n"
    "            uint32_t _gp=vm_read32(_toc - 0x5E34);\n"
    "            uint32_t _go=_gp?vm_read32(_gp):0u;\n"
    "            uint32_t _mv=vm_read32(_toc - 0x1124);\n"
    "            uint32_t _fp=vm_read32(_toc - 0x35D4);\n"
    "            uint32_t _s0=_S?vm_read32(_S+0x00):0u;\n"
    "            uint32_t _s4=_S?vm_read32(_S+0x04):0u;\n"
    "            uint32_t _s8=_S?vm_read32(_S+0x08):0u;\n"
    "            uint32_t _sA=_S?vm_read8 (_S+0x10):0u;\n"
    "            uint32_t _t0=_S?vm_read32(_S+0x18):0u;\n"
    "            uint32_t _t1=_S?vm_read32(_S+0x1C):0u;\n"
    "            uint32_t _st=_mv?vm_read32(_mv+0x620):0u;\n"
    "            uint32_t _fh=_fp?vm_read32(_fp):0u;\n"
    "            uint32_t _fl=_fp?vm_read32(_fp+4):0u;\n"
    "            uint32_t _g4=_go?vm_read8(_go+4):0u;\n"
    "            float _fd; memcpy(&_fd,&_s8,4);\n"
    "            static int _n=0;\n"
    "            static uint32_t _p0=0xFFFFFFFFu,_p4=0xFFFFFFFFu,_pA=0xFFFFFFFFu,_ps=0xFFFFFFFFu;\n"
    "            int _chg=(_s0!=_p0)||(_s4!=_p4)||(_sA!=_pA)||(_st!=_ps);\n"
    "            /* Edge-trigger para as transicoes + HEARTBEAT periodico: sem o\n"
    "             * heartbeat, um sub-estado que nunca muda deixa de imprimir e\n"
    "             * fica impossivel distinguir 'o fade progride devagar' de 'o\n"
    "             * fade esta congelado' -- que e' exactamente a pergunta. */\n"
    "            if(_n<6 || _chg || (_n%64)==0){\n"
    '              fprintf(stderr,"[INTROSEQ] tick #%d S=0x%08X idx=%u sub=%u fade=%.5f'
    ' started=%u timer=%08X%08X now=%08X%08X freq=%08X%08X st620=%u guard+4=%u%s\\n",\n'
    "                _n,_S,_s0,_s4,(double)_fd,_sA,_t0,_t1,\n"
    "                (uint32_t)(ps3_timebase_now()>>32),(uint32_t)ps3_timebase_now(),\n"
    "                _fh,_fl,_st,_g4,_chg?\"  <-- CHANGE\":\"\");\n"
    "              fflush(stderr);\n"
    "              _p0=_s0;_p4=_s4;_pA=_sA;_ps=_st;\n"
    "            }\n"
    "            _n++;\n"
    "          } }\n"
)


def sig(name: str) -> str:
    return "void " + name + "(ppu_context* ctx) {\n"


def install(root: Path, name: str, probe: str) -> str:
    """Insere `probe` logo a seguir a chave de abertura de `name`.

    Devolve uma linha de estado. Levanta SystemExit se a funcao nao existir --
    um lift em que um destes elos desapareceu invalida a leitura do plano, e
    falhar em silencio daria uma medicao com buracos que parece completa.
    """
    signature = sig(name)
    for path in sorted(root.glob("ppu_recomp_*.cpp")):
        src = path.read_text(encoding="utf-8", errors="replace")
        i = src.find(signature)
        if i < 0:
            continue
        # Regiao = corpo desta funcao ate' a definicao seguinte. O marcador so'
        # e' procurado AQUI: outra funcao pode ter uma probe do mesmo nome.
        j = src.find("void func_", i + len(signature))
        region = src[i:j] if j > i else src[i:]
        if MARKER in region:
            return f"{path.name}: {name} ja instrumentada"
        region = region.replace(signature, signature + probe, 1)
        path.write_text(src[:i] + region + (src[j:] if j > i else ""),
                        encoding="utf-8", newline="\n")
        return f"{path.name}: {name} instrumentada"
    raise SystemExit(
        f"{name} ausente no lift em {root} -- a cadeia de arranque mudou de "
        "forma; reveja a probe antes de forcar"
    )


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent
    if not sorted(root.glob("ppu_recomp_*.cpp")):
        print(f"FAIL: nenhum ppu_recomp_*.cpp em {root}", file=sys.stderr)
        return 1

    for name, what in BEACONS:
        print(install(root, name, beacon(name, what)))
    print(install(root, "func_000CE0A0", CE0A0_PROBE))
    print(install(root, "func_000CDE3C", CDE3C_PROBE))
    print(f"probe {MARKER} pronta -- corra com {ENV}=1")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
