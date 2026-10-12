#!/usr/bin/env python3
"""Fix na origem: o REHOME da free-list TYPE15 (TYPE15-FL-REHOME-FIXUP) so'
protege ponteiros AUTO-REFERENTES dentro do proprio blob [fl, fl+0x200) --
mas o "mid" (fl+0x18) guarda um ponteiro para um OBJECTO SEPARADO (o
"shell"/template; slot = shell+4) que fica FORA dessa janela e nunca foi
pinado.

Contexto (mede e fecha a parede aberta em
notes/2026-07-31-type15-fl-slot-medido-fix-e-nova-parede.md, secao
"Hipotese para a proxima medicao")
----------------------------------------------------------------------------
Com o TYPE15-FL-REHOME-FIXUP ja aplicado (head/mid ficam estaveis dentro da
zona pinada 3/3 corridas), a fabrica pinada `func_0039E794` ainda devolvia
`r3=NULL` na 2a e 3a chamada em vez de reusar produto valido. Medido com um
probe dedicado (PS3_TRACE_TYPE15_SHELL=1, dump de 16 palavras do "this" da
2a chamada interna -- o construct real via vt[+0x14] -- antes/depois de cada
invocacao):

    pre#20/post#20 (1a chamada, this=0x40100840):
      00516DD8 40150015 00000000 ... 401002F0 000003E8 000003E8 80000011
      -- objecto vivo (vt=0x00516DD8, tag tipo 0x0015), produto valido
         r3=0x42F85AE4.
    pre#25/post#25 (2a chamada, MESMO this=0x40100840, ~430 linhas de log
    depois):
      34343534 002F3A50 53325F34 34353661 002F3A50 53325F34 34353663 ...
      -- ASCII puro: bytes 34 34 35 34='4454', 00 2F 3A 50 (NUL '/' ':' 'P'),
         53 32 5F 34='S2_4' repetido -- uma tabela de paths de assets tipo
         ".../S2_446a/S2_446c/S2_446b/..." stream-stomped por cima do
         MESMO endereco. r3=0x00000000 (NULL).

Ou seja: NAO e' uma flag "ja usado" dentro do objecto (H1 da nota) -- e' o
objecto INTEIRO reescrito por outra alocacao do guest, porque o REHOME
nunca o copiou para a zona pinada. So' o CONTROLO da free-list (fl/head/
mid) ficou protegido; o PAYLOAD que o controlo aponta continuou a viver na
arena antiga desprotegida (0x401xxxxx), a mesma classe de churn que ja'
tinha corrompido o proprio controlo antes do primeiro fix.

Fix (na origem, preserva o CONTEUDO capturado do guest -- nao forja um
shell novo): depois de traduzir os auto-ponteiros do blob [fl, fl+0x200),
se `*mid` (fl+0x18) apontar para fora da zona pinada, copia o objecto real
apontado (mid_val - 4, ate 0x80 bytes) para o mesmo slot que
`ps3_type15_freelist_replenish` ja usa para shells sinteticos
(k_ty15_pin + 0x800) e retarget `*mid` para lá. Assim a 2a/3a chamada leem
um objecto que já não pode ser stream-stomped pela mesma classe de reuso de
heap.

Gate: nenhum -- e' FUNCIONAL (fix de correctude), sempre activo, so' corre
uma vez por processo dentro do bloco de REHOME ja existente (`g_ty15_rehomed`
so' se torna 1 no fim desta mesma chamada).

Fonte de verdade versionada: ../host_gow2_factory.cpp (mesmo fix, aplicado a
mao la' primeiro e extraido literalmente para este patch).

Idempotente: marcador presente -> ALREADY, rc=0.
rc: 0 aplicado ou já-aplicado; 2 nenhum chunk com a função; 3 agulha em falta
    ou forma inesperada (lift mudou) -- por exemplo se
    TYPE15-FL-REHOME-FIXUP ainda nao foi aplicado (este patch depende dele).
"""
from __future__ import annotations

import sys
from pathlib import Path

from lift_paths import resolve_lift_paths

MARKER = "TYPE15-FL-SHELL-REHOME"
DEFAULT = "recomp_macos_v2/ppu_recomp_001.cpp"
FN_SIG = "extern \"C\" void ps3_type15_note_resolve(uint32_t idx, uint32_t obj, uint32_t vt) {\n"

NEEDLE = ("                        for (uint32_t off = 0; off < 0x200u; off += 4u) {\n"
          "                            uint32_t w = vm_read32(k_fl_pin + off);\n"
          "                            if (w >= fl && w < fl + 0x200u)\n"
          "                                vm_write32(k_fl_pin + off, k_fl_pin + (w - fl));\n"
          "                        }\n"
          "                        vm_write32(k_ty15_pin + 0x24u, k_fl_pin);\n")

SHELL_REHOME = (
    "                        /* {marker} (2026-07-31): a traducao acima so'\n"
    "                         * protege ponteiros AUTO-REFERENTES dentro do proprio\n"
    "                         * blob [fl, fl+0x200). O \"mid\" (fl+0x18) guarda um\n"
    "                         * ponteiro para um OBJECTO SEPARADO (o \"shell\"; slot =\n"
    "                         * shell+4) que fica FORA dessa janela e nunca foi pinado.\n"
    "                         * Medido (PS3_TRACE_TYPE15_SHELL=1): esse objecto\n"
    "                         * sobrevive a' 1a chamada (produto valido) mas e'\n"
    "                         * stream-stomped por outra alocacao do guest (uma\n"
    "                         * tabela de paths tipo \"S2_446a/...\") antes da 2a\n"
    "                         * chamada, que devolve NULL. Copia o objecto real\n"
    "                         * (preserva o conteudo capturado, nao forja um novo)\n"
    "                         * para o mesmo slot que ps3_type15_freelist_replenish\n"
    "                         * usa para shells sinteticos e retarget o slot. */\n"
    "                        {{\n"
    "                            uint32_t mid_val = vm_read32(k_fl_pin + 0x18u);\n"
    "                            if (mid_val >= 0x10000u && mid_val < 0x4F000000u &&\n"
    "                                (mid_val < k_ty15_pin || mid_val >= k_ty15_pin + 0x1000u)) {{\n"
    "                                uint32_t old_shell = mid_val - 4u;\n"
    "                                uint32_t new_shell = k_ty15_pin + 0x800u;\n"
    "                                for (uint32_t o = 0; o < 0x80u; o += 4u)\n"
    "                                    vm_write32(new_shell + o, vm_read32(old_shell + o));\n"
    "                                vm_write32(k_fl_pin + 0x18u, new_shell + 4u);\n"
    "                                {{ static int _n=0; if(_n++<8)\n"
    "                                    fprintf(stderr,\"[TYPE15] REHOME shell old=0x%08X pin=0x%08X vt=0x%08X\\n\",\n"
    "                                      old_shell, new_shell, vm_read32(new_shell)); }}\n"
    "                            }}\n"
    "                        }}\n"
).format(marker=MARKER)


def patch(path: Path) -> tuple[bool, int]:
    t = path.read_text(encoding="utf-8", errors="replace")

    if f"/* {MARKER} " in t:
        print(f"  {path.name}: ALREADY (marcador presente)")
        return False, 0

    if FN_SIG not in t:
        print(f"  {path.name}: skip (sem {FN_SIG.strip()})")
        return False, -1

    if t.count(FN_SIG) != 1:
        print(f"ERRO: {FN_SIG.strip()} aparece {t.count(FN_SIG)}x em {path.name} "
              f"(esperado 1)", file=sys.stderr)
        return False, 3

    body_start = t.find(FN_SIG) + len(FN_SIG)
    body_end = t.find("\n}\n", body_start)
    if body_end < 0:
        print(f"ERRO: {path.name} nao tem fim de ps3_type15_note_resolve",
              file=sys.stderr)
        return False, 3
    body = t[body_start:body_end]

    n = body.count(NEEDLE)
    if n != 1:
        print(f"ERRO: agulha (pos-FIXUP) aparece {n}x no corpo de "
              f"ps3_type15_note_resolve em {path.name} (esperado 1) -- "
              f"TYPE15-FL-REHOME-FIXUP ainda nao aplicado, ou lift mudou de "
              f"forma; NAO aplicado", file=sys.stderr)
        return False, 3

    at = body.find(NEEDLE)
    insert_at = at + len(NEEDLE)
    body = body[:insert_at] + SHELL_REHOME + body[insert_at:]

    t = t[:body_start] + body + t[body_end:]

    try:
        path.write_text(t, encoding="utf-8", newline="\n")
    except TypeError:                                # macOS system python3 (3.9.6)
        path.write_text(t, encoding="utf-8")
    print(f"  {path.name}: APPLIED (shell rehome: copia o payload real para o pin)")
    return True, 0


def main() -> int:
    paths = resolve_lift_paths(sys.argv[1:], DEFAULT)
    print(f"[{MARKER}] alvos: {[str(p) for p in paths]}")

    applied_any = False
    already_any = False
    hard_fail = 0
    n_missing = 0
    for p in paths:
        if not p.exists():
            print(f"  {p}: MISSING")
            n_missing += 1
            continue
        changed, rc = patch(p)
        if rc == 3:
            hard_fail += 1
        elif rc == 0 and changed:
            applied_any = True
        elif rc == 0 and not changed:
            already_any = True
        # rc == -1: funcao nao esta' neste chunk, ignora

    if hard_fail:
        print(f"[{MARKER}] rc=3 ({hard_fail} falha(s) de forma)")
        return 3
    if n_missing == len(paths):
        print(f"[{MARKER}] rc=2 (nenhum chunk de lift encontrado)")
        return 2
    if applied_any or already_any:
        print(f"[{MARKER}] rc=0")
        return 0
    print(f"[{MARKER}] rc=2 (ps3_type15_note_resolve nao encontrada em nenhum chunk)")
    return 2


if __name__ == "__main__":
    sys.exit(main())
