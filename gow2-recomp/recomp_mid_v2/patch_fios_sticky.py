#!/usr/bin/env python3
"""Re-materializa a palavra de conclusao `[op+0x90]` das ops FIOS quando ela e'
apagada por baixo do poll do estado 1 (o "sticky" do done-word).

METADE DE RUNTIME (JA' VERSIONADA -- nao e' este script)
--------------------------------------------------------
As tres funcoes host vivem em ../ps3recomp/runtime/ppu/ppu_loader.cpp (commit
a82c594):
    ps3_fios_sticky_publish(op)  -- o produtor grava aqui que [op+0x90] passou a 1
    ps3_fios_sticky_peek(op)     -- 1 se ja' publicamos done para esta op
    ps3_fios_sticky_consume(op)  -- limpa a marca quando o guest consome o done
Este script NAO lhes toca; so' as declara e chama a partir do lift.

METADE DE LIFT (ESTE SCRIPT)
----------------------------
Regra 4 do CLAUDE.md: fixes nos ppu_recomp_XXX.cpp (gitignored, regeneraveis)
tem de viver como patch_*.py idempotente reaplicado apos cada re-lift. As quatro
insercoes do sticky foram feitas a mao no lift e um re-lift apaga-as; e' isso
que este script recaptura.

PORQUE (a parede que isto contorna)
-----------------------------------
Medido em 2026-07-20 (ver patch_fios_open_probe.py / patch_fios_sched_probe.py):
o open FIOS do filme de intro NAO falha -- a op completa e o produtor
(func_00306534) escreve `[op+0x90]=1`. Mas sob o giant lock unico a thread do
escalonador FIOS corre todo o `complete` (e um clear posterior de [op+0x90])
antes de o poll do estado 1 (func_002B4224) conseguir observar done!=0. O poll
le `[op+0x90]` e ve 0 em 100% das amostras, apesar de a palavra ter estado a 1
por um instante. Resultado: a FSM da intro fica presa em st620==1.

O sticky fecha essa janela do lado do consumidor:
  * o produtor PUBLICA (na tabela host) que gravou done=1;
  * o poll, se le 0 mas ha' publicacao pendente, RE-MATERIALIZA done=1 (no gpr e
    de volta na memoria guest), de modo que a leitura racing deixa de importar;
  * o ramo 'done' CONSOME a publicacao para nao a reaplicar em opens seguintes.

AS QUATRO INSERCOES (todas em ppu_recomp_001.cpp neste port)
-----------------------------------------------------------
1. DECLS  -- as 3 declaracoes extern "C" das funcoes host, ao fim do bloco de
   decls FIOS-CANCEL-YIELD (que patch_fios_cancel_yield.py cria e que corre
   antes deste, por ordem alfabetica).
2. STICKY-RESTORE em func_002B4224, LOGO A SEGUIR a' linha do lifter
   `ctx->gpr[0] = vm_read32(ctx->gpr[9] + 0x90);` (a leitura do done-word): se
   gpr[0]==0 e ha' publicacao para a op, poe gpr[0]=1 e vm_write32(io+0x90,1).
3. STICKY-CONSUME em func_002B4274 (o ramo 'done'), ANTES da 1a linha do corpo
   `ctx->gpr[0] = vm_read32(ctx->gpr[31] + 0x8);`.
4. PUBLISH (caminho A) em func_00306534 (o produtor), LOGO A SEGUIR a' linha do
   lifter `vm_write32(ctx->gpr[9] + 0x0, ctx->gpr[11]);` (a gravacao real de
   [op+0x90]): se gpr[11]!=0 publica a op na tabela host.

O QUE NAO ENTRA (e porque)
--------------------------
* A 2a chamada a ps3_fios_sticky_publish em func_00306534 (~a seguir a` linha 4)
  vive DENTRO do bloco de probe [FIOSSCHED] done90-VERIFY, junto de uma chamada
  a ps3_fios_watch_done_ea. Esse "publish caminho B" e' DIAGNOSTICO (gated por
  PS3_TRACE_FIOSSCHED, OFF por default) e pertence a` probe do escalonador
  (patch_fios_sched_probe.py), NAO ao sticky. So' o publish caminho A (nu, fora
  de probe) e' load-bearing; e' o unico que este script instala.
* `extern "C" void ps3_fios_watch_done_ea(uint32_t ea);` fica no MESMO bloco de
  decls mas NAO e' do sticky -- e' o decl (feito a mao, ainda nao roteirizado)
  da probe done90-VERIFY. Nao o adicionamos nem o removemos.
* O bloco STICKY-RESTORE inclui uma linha de trace [FIOSSCHED] STICKY-RESTORE
  gated pela mesma env (OFF por default). E' parte estrutural do bloco (so'
  dispara quando a restauracao ocorre), por isso vai verbatim -- ao contrario do
  publish caminho B, que e' um statement separado no probe alheio.

ANCORAGEM (robusta a` ordem dos outros patches)
-----------------------------------------------
As needles dos sites 2/3/4 ancoram em linhas GERADAS PELO LIFTER (as leituras/
escritas de [op+0x90], [op+0x8], [op+0x0]), NUNCA em texto que outro patch
inseriu -- assim a ordem de aplicacao das probes (fios_open_probe,
fios_sched_probe, st3_probe, ...) nao pode partir isto. As insercoes 2 e 4 sao
"depois da linha X"; a needle da 2 e' AGNOSTICA A INDENTACAO (a linha do lifter
tem, no lift atual, uma indentacao anomala de 16 espacos por artefacto de edicao
a` mao; casamos so' o codigo + '\\n', preservando o que quer que a preceda, para
funcionar tambem num re-lift limpo com 8 espacos).

As DECLS (site 1) sao um caso a` parte: nao tem casa natural no lifter. Ancoram
ao fim do bloco FIOS-CANCEL-YIELD. Preferimos por a seguir ao decl
ps3_fios_watch_done_ea QUANDO ele existe (da' byte-identidade com o lift editado
a` mao); se ele nao existir (re-lift limpo -- ninguem o roteiriza), caimos para
a` ultima linha garantida do bloco (ps3recomp_giant_lock_yield_sleep1, criada
pelo cancel_yield), desambiguada por `\\n\\nvoid func_002B3F78` para nao apanhar
o bloco homonimo FIOS-DONE-YIELD (que precede func_00306534). Em qualquer dos
casos as decls ficam ANTES do 1o uso (func_002B4224).

Idempotente: a 2a corrida deixa a arvore identica (ALREADY). SystemExit so' se
uma needle load-bearing faltar (shape do lift mudou) -- nunca forcar.

Marcadores em uso: STICKY-RESTORE, STICKY-CONSUME (nao colidir com ST3-PROBE de
patch_st3_probe.py em func_002C05F8 -- nao tocada aqui).

Uso:  patch_fios_sticky.py [DIR_DE_LIFT]     (default: ../recomp_macos_v2)
Reaplicado por ../apply_all_patches.sh apos cada re-lift.

Nota de implementacao: substituicao textual exacta (str.replace sobre regioes
fatiadas), NAO re.sub com string de substituicao -- essa interpreta escapes e ja'
meteu quebras de linha reais dentro de literais C noutros patches.
"""
from pathlib import Path
import sys

ROOT = (Path(sys.argv[1]) if len(sys.argv) > 1
        else Path(__file__).resolve().parent.parent / "recomp_macos_v2")

# ---- textos das 4 insercoes (verbatim do lift) ------------------------------

STICKY_DECLS = (
    'extern "C" void ps3_fios_sticky_publish(uint32_t op);\n'
    'extern "C" int ps3_fios_sticky_peek(uint32_t op);\n'
    'extern "C" void ps3_fios_sticky_consume(uint32_t op);\n'
)
DECLS_FIRST = 'extern "C" void ps3_fios_sticky_publish(uint32_t op);\n'

RESTORE_BLOCK = (
    "        /* STICKY-RESTORE: if guest done word was cleared after the producer\n"
    "         * published it (observed RAWMEM 1 at CBret, 0 at first poll with no\n"
    "         * write32 to the EA), re-materialize from the host sticky table. */\n"
    "        { uint32_t _io=(uint32_t)ctx->gpr[9];\n"
    "          if ((uint32_t)ctx->gpr[0]==0u && _io && ps3_fios_sticky_peek(_io)) {\n"
    "            ctx->gpr[0] = 1;\n"
    "            vm_write32(_io + 0x90u, 1u);\n"
    "            { static int _on=-1; if(_on<0){extern char* getenv(const char*);\n"
    '                const char* _e=getenv("PS3_TRACE_FIOSSCHED"); _on=(_e&&*_e&&*_e!=\'0\')?1:0;}\n'
    "              if(_on){ static int _n=0; if(_n++<8){\n"
    '                fprintf(stderr,"[FIOSSCHED] STICKY-RESTORE #%d io=0x%08X\\n",_n,_io);\n'
    "                fflush(stderr); } } }\n"
    "          } }\n"
)

CONSUME_BLOCK = (
    "        /* STICKY-CONSUME */\n"
    "        { uint32_t _io=vm_read32((uint32_t)ctx->gpr[31]+8u);\n"
    "          if(_io) ps3_fios_sticky_consume(_io); }\n"
)

PUBLISH_BLOCK = (
    "        if (((uint32_t)ctx->gpr[11]) != 0u)\n"
    "            ps3_fios_sticky_publish((uint32_t)ctx->gpr[31]);\n"
)

# ---- ancoras (linhas geradas pelo lifter) -----------------------------------
# sites 2 e 4: insert-after, needle AGNOSTICA A' INDENTACAO (sem espacos a`
# esquerda) -- casa o codigo + '\n', preservando a indentacao existente.
A_RESTORE = "ctx->gpr[0] = vm_read32(ctx->gpr[9] + 0x90);\n"
A_PUBLISH = "vm_write32(ctx->gpr[9] + 0x0, ctx->gpr[11]);\n"
# site 3: insert-before, needle com a indentacao normal (8 espacos) do lifter.
A_CONSUME = "        ctx->gpr[0] = vm_read32(ctx->gpr[31] + 0x8);\n"

# site 1: ancoras do bloco de decls FIOS-CANCEL-YIELD.
A_WATCH = 'extern "C" void ps3_fios_watch_done_ea(uint32_t ea);\n'
A_YIELD = 'extern "C" void ps3recomp_giant_lock_yield_sleep1(void);\n'
A_FUNC3F78 = "\nvoid func_002B3F78(ppu_context* ctx) {\n"

# a funcao cuja presenca marca "este chunk tem os sites do sticky".
GATE_FUNC = "void func_002B4224(ppu_context* ctx) {\n"


def _region(t, func):
    """(i, end, region) da funcao; region vai da assinatura ao proximo 'void func_'."""
    sig = "void " + func + "(ppu_context* ctx) {\n"
    i = t.find(sig)
    if i < 0:
        return -1, -1, ""
    j = t.find("\nvoid func_", i + len(sig))
    end = j if j > i else len(t)
    return i, end, t[i:end]


def _edit_after(t, func, anchor, block, tag):
    i, end, region = _region(t, func)
    if i < 0:
        return t, "%s: SKIP(func)" % tag
    if anchor + block in region:
        return t, "%s: ALREADY" % tag
    c = region.count(anchor)
    if c != 1:
        raise SystemExit(
            "patch_fios_sticky: ancora de %s aparece %dx em %s (esperado 1) -- "
            "shape do lift mudou; reveja a needle antes de forcar" % (tag, c, func))
    region = region.replace(anchor, anchor + block, 1)
    return t[:i] + region + t[end:], "%s: APLICADO" % tag


def _edit_before(t, func, anchor, block, tag):
    i, end, region = _region(t, func)
    if i < 0:
        return t, "%s: SKIP(func)" % tag
    if block + anchor in region:
        return t, "%s: ALREADY" % tag
    c = region.count(anchor)
    if c != 1:
        raise SystemExit(
            "patch_fios_sticky: ancora de %s aparece %dx em %s (esperado 1) -- "
            "shape do lift mudou; reveja a needle antes de forcar" % (tag, c, func))
    region = region.replace(anchor, block + anchor, 1)
    return t[:i] + region + t[end:], "%s: APLICADO" % tag


def _edit_decls(t):
    if DECLS_FIRST in t:
        return t, "decls: ALREADY"
    if A_WATCH in t:                       # lift editado a` mao -> byte-identico
        return t.replace(A_WATCH, A_WATCH + STICKY_DECLS, 1), "decls: apos watch"
    block = A_YIELD + A_FUNC3F78           # re-lift limpo -> fim do bloco cancel-yield
    if block in t:
        return t.replace(block, A_YIELD + STICKY_DECLS + A_FUNC3F78, 1), \
            "decls: fim do bloco cancel-yield"
    raise SystemExit(
        "patch_fios_sticky: bloco de decls FIOS-CANCEL-YIELD ausente -- "
        "patch_fios_cancel_yield.py correu antes? Reveja antes de forcar")


def patch_file(p):
    t = p.read_text(encoding="utf-8", errors="replace")
    if GATE_FUNC not in t:
        return "SKIP"
    orig = t
    notes = []
    t, s = _edit_decls(t);                                               notes.append(s)
    t, s = _edit_after(t, "func_002B4224", A_RESTORE, RESTORE_BLOCK, "restore"); notes.append(s)
    t, s = _edit_before(t, "func_002B4274", A_CONSUME, CONSUME_BLOCK, "consume"); notes.append(s)
    t, s = _edit_after(t, "func_00306534", A_PUBLISH, PUBLISH_BLOCK, "publish"); notes.append(s)
    if t != orig:
        p.write_text(t, encoding="utf-8", newline="\n")
        return "APLICADO | " + " ; ".join(notes)
    return "ALREADY | " + " ; ".join(notes)


def main():
    files = sorted(ROOT.glob("ppu_recomp_*.cpp"))
    if not files:
        print("nenhum ppu_recomp_*.cpp em %s" % ROOT)
        return 1
    any_hit = False
    for p in files:
        r = patch_file(p)
        if r != "SKIP":
            any_hit = True
            print("%s: %s" % (p.name, r))
    if not any_hit:
        print("SKIP: nenhum chunk com func_002B4224 (sites do sticky ausentes)")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
