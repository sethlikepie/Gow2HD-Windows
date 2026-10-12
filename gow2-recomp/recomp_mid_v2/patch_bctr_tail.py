#!/usr/bin/env python3
"""`bctr` (branch to CTR, SEM link) volta a ser um SALTO e nao uma chamada host.

O QUE ESTAVA ERRADO
-------------------
O lifter ja distingue as duas instrucoes:

    bctrl  ->  ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);   (~16k sitios)
    bctr   ->  ps3_indirect_call(ctx); return;                  (479 sitios)

mas AMBAS acabavam em `ps3_indirect_call`, que e' uma CHAMADA host (`fn(ctx)`
com uma frame nova). No CPU real `bctr` e' um salto: nao consome stack nenhuma.
Passar um salto do guest por uma chamada do host transforma qualquer ciclo de
maquina-de-estados por jump table em recursao host sem limite.

MEDIDO (2026-07-20, macOS/arm64, 16 boots de 25 s)
--------------------------------------------------
O pump da FSM do player da intro, `func_002C0508`, cicla despachando
`[obj+0x620]` pela jump table em guest 0x002C0594 -- que no EBOOT.ELF e'
literalmente `0x4E800420` = `bctr`, sem link. A tabela fica inline logo a
seguir (base 0x002C0598); a entrada do estado 1 e' +0x240 => 0x002C07D8.

Enquanto espera pelo open FIOS, o estado 1 sonda `func_002B4224`, que le
`[op+0x90]`. Quem POE `[op+0x90]=1` e' `func_0030644C` (chamado com r4&0xFF==1
-> `func_00306534`), e quem o chama no caminho de sucesso e' a thread PPU
"fios scheduler" (`[SYS] sys_ppu_thread_create name="fios scheduler"`,
OPD 0x00534370 -> trampolim generico `func_00314E2C` -> OPD 0x005341A8 ->
`func_0030ED68`/`func_0030EDD0`), que faz pop da lista lock-free em
`[media+0x204]` onde `func_0030B058` empurra a op.

Como cada iteracao da sonda empilhava uma frame host, o ciclo batia SEMPRE no
tecto de recursao de 4000 do `ps3_indirect_call`:

    16/16 corridas: "[ppu] recursion cap @0x002C07D8" (ainda em st620=1, 11/16)
                 ou "[ppu] recursion cap @0x002C05F8" (ja' em st620=3,  5/16)

e o `return` do tecto desenrola o pump -- que nunca mais e' reentrado (a linha
do tecto aparece 1x por corrida, apesar de o guard imprimir ate' 8). Ou seja: a
intro sair ou nao do estado 1 era uma CORRIDA entre a "fios scheduler" e o
orcamento de 4000 frames, ganha em 5/16.

O FIX
-----
Despachar em cauda: por `g_trampoline_fn` e voltar -- exactamente o modelo que
o lift ja usa para `b target`. O `DRAIN_TRAMPOLINE` do chamador corre o alvo
iterativamente, com crescimento de stack ZERO, que e' o que o hardware faz.

Pre-condicao verificada antes de escrever isto: os 116314 sitios de chamada
directa a funcoes liftadas neste lift sao TODOS seguidos de DRAIN_TRAMPOLINE
(0 excepcoes), e toda a entrada host em codigo guest (`ppu_run`,
`ps3_indirect_call`, o thread proc do boot_macos) tambem drena. Logo um
trampolim posto aqui e' sempre executado.

`ps3_indirect_tail` (runtime/ppu/ppu_loader.cpp) so' faz o salto quando o alvo
E' uma funcao liftada; para tudo o resto (sentinelas 0 / 0xC0DE1111, thunks de
import, alvos por resolver) delega em `ps3_indirect_call` e o comportamento
fica igual ao anterior. `PS3_BCTR_HOSTCALL=1` repoe o comportamento antigo.

Idempotente. Marcador: BCTR-TAIL.
"""
from pathlib import Path
import sys

MARKER = "BCTR-TAIL"

DECL_NEEDLE = 'extern "C" void ps3_indirect_call(ppu_context* ctx);'
DECL_NEW = (
    'extern "C" void ps3_indirect_call(ppu_context* ctx);\n'
    '/* BCTR-TAIL: despacho de `bctr` (salto, SEM link). Ver\n'
    ' * recomp_mid_v2/patch_bctr_tail.py e ps3_indirect_tail em\n'
    ' * ps3recomp/runtime/ppu/ppu_loader.cpp. */\n'
    'extern "C" void ps3_indirect_tail(ppu_context* ctx);'
)

OLD_CALL = "ps3_indirect_call(ctx); return;"
NEW_CALL = "ps3_indirect_tail(ctx); return;"

ROOT = (Path(sys.argv[1]) if len(sys.argv) > 1
        else Path(__file__).resolve().parent.parent / "recomp_macos_v2")


def patch_file(p: Path) -> str:
    t = p.read_text(encoding="utf-8", errors="replace")
    if MARKER in t:
        return "ALREADY"
    if OLD_CALL not in t:
        return "SKIP"          # chunk sem nenhum sitio de bctr
    if DECL_NEEDLE not in t:
        return "FAIL-NEEDLE"   # preambulo do lift mudou de forma

    # 1) declaracao: so' a PRIMEIRA ocorrencia, no preambulo.
    head_end = t.index(DECL_NEEDLE) + len(DECL_NEEDLE)
    head, tail = t[:head_end], t[head_end:]
    head2 = head.replace(DECL_NEEDLE, DECL_NEW, 1)
    if head2 == head:
        return "FAIL-DECL"

    # 2) sitios de bctr: str.replace na regiao do corpo (nunca re.sub -- o
    #    texto de substituicao passaria por escapes de backreference).
    n = tail.count(OLD_CALL)
    tail2 = tail.replace(OLD_CALL, NEW_CALL)
    if n == 0 or tail2 == tail:
        return "FAIL-REPLACE"

    p.write_text(head2 + tail2, encoding="utf-8", newline="\n")
    return "APPLIED(%d)" % n


def main() -> int:
    files = sorted(ROOT.glob("ppu_recomp_*.cpp"))
    if not files:
        print("nenhum ppu_recomp_*.cpp em %s" % ROOT)
        return 1
    any_app = False
    for p in files:
        r = patch_file(p)
        if r != "SKIP":
            print("%s: %s" % (p.name, r))
        if r.startswith("APPLIED"):
            any_app = True
        if r.startswith("FAIL"):
            return 1
    if any_app:
        return 0
    ok = any(MARKER in q.read_text(encoding="utf-8", errors="replace")
             for q in files)
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
