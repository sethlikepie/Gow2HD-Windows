#!/usr/bin/env python3
"""Repoe o bounds-check de func_0002F3F0 (hash de nomes do registry 393E0).

Porque existe
-------------
`func_0002F3F0` e' o hash de strings que o registry `func_000393E0` chama
centenas de vezes (via `func_0002F43C` @0x2F454 e via 0x31EC4). O corpo lifted
e' um `for(;;)` que so' termina quando le' um byte ZERO a partir de `r4`:

        loc_0002F404:
        ...
        ctx->gpr[11] = vm_read8(ctx->gpr[4] + 0x1); ctx->gpr[4] += 0x1;
        ...
        if ((!((ctx->cr >> 0) & 2))) goto loc_0002F404;   <-- SEM tecto

No lift de 20 jul (`recomp_macos_v2.pre_v4`, `func_0002F3F0`) esse corpo estava
reescrito A MAO com duas proteccoes -- e NENHUM patch_*.py as repunha. O lift
regenerado de 26 jul perdeu-as (corpo 2467 -> 1425 bytes) sem que nada se
queixasse: este ficheiro e' o ESCRITOR que faltava.

O que se perde sem isto (MEDIDO, nao teorico)
---------------------------------------------
Nao basta dizer "o vm_read8 ja' se defende". Defende-se pela metade:
`vm_read8` devolve 0 para memoria NAO COMITADA (ppu_memory.h:60-64 ->
ppu_guest_range_committed -> ppu_loader.cpp:586-598), portanto um ponteiro
lixo fora das regioes comitadas termina o loop a' primeira leitura. Mas o host
macOS comita (boot_macos.cpp:288-318, com as constantes de runtime/memory/vm.h):

    #1  0x00000000..0x51000000   (1296 MB: imagem + heap + TLS + mmapper)
    #2  0xD0000000..0xE0000000   (256 MB banda de stacks, VM_STACK_BASE)
    #3  0xC0000000..0xD0000000   (256 MB memoria local do RSX, PS3_VM_RSX_MB)

Dentro destas bandas `vm_read8` devolve o byte REAL, logo o loop anda. Dois
casos que so' esta guarda apanha:

  * ponteiro para a banda alta comitada -- 0xC0000000..0xE0000000 sao 512 MB
    CONTIGUOS de framebuffer/stack, quase todo nao-nulo depois do primeiro
    render (os EAs de bind 0xC0FB0980 / 0xC1021180 / 0xC0F40180 estao la'
    dentro, ppu_loader.cpp:1943-1945). Sem tecto, UMA chamada percorre ate'
    512 M iteracoes, cada uma com a varredura de ranges do guard. Vezes as
    centenas de chamadas do 393E0 = paragem.
  * ponteiro lixo DENTRO do heap comitado (#1) a apontar para o meio do
    payload do WAD: 1,27 GB de bytes nao-nulos possiveis. Aqui o guard do
    `vm_read8` nao ajuda de todo -- e' memoria legitima.

O que instala (extraido VERBATIM de recomp_macos_v2.pre_v4)
-----------------------------------------------------------
1) prologo: `_sp = (uint32_t)r4`; se `_sp < 0x10000u || _sp >= 0x4F000000u`
   -> `r3 = 0` e RETORNA. (0x4F000000 = mesmo tecto de ponteiro-valido usado
   pelos blocos B71/TYPE15; fica abaixo do topo da regiao #1, 0x51000000.)
2) pre-scan com tecto de 256 chars que emite `[B71] 2F3F0 cap str=...`.
3) tecto `_nch < 256` no proprio loop do guest (`{ int _nch = 0; ... }`).

Fidelidade ao guest: para uma string legitima (rodata ~0x4C5FBF, heap < 0x51000000,
nomes de registry muito abaixo de 256 chars) a guarda e' NO-OP -- nenhum ramo
dispara e o hash devolvido e' bit-a-bit o do console. So' o caminho patologico
(ponteiro que na PS3 real nunca existiria) e' cortado.

Nao e' probe, nao e' cosmetico: nao ha' CRC bypass, nao ha' magic estampado,
nao ha' guard mascarado. E' um tecto de iteracao + validacao de ponteiro.

Gating por env var
------------------
NAO tem, e e' de proposito: a versao de referencia (pre_v4) tambem nao tinha.
E' um bounds-check FUNCIONAL, nao um probe -- deixa-lo OFF por default seria
repor exactamente o pendurar que ele existe para evitar. Os dois `fprintf` sao
os unicos efeitos observaveis e tem tecto de vida (16 e 8 linhas, `static int
_n`), portanto nao ha' spam no baseline; num boot saudavel nunca aparecem.

NAO CONFUNDIR com `patch_b71_cb56c_reuse_block.py`
--------------------------------------------------
Esse e' de OUTRO B71 -- `func_000B71B8`, chunk do CB56C -- e emite
`[POSTINTRO] B71 ...`. Este emite `[B71] 2F3F0 ...`. Alvos, agulhas e
marcadores sao disjuntos: os dois podem correr em qualquer ordem.

MIGRADO PARA WEAK OVERRIDE (Fase 19, plano 19-03) -- ler antes de mexer
------------------------------------------------------------------------
Este bloco deixou de ser a fonte de verdade. Vive agora em codigo host
VERSIONADO, `games/gow2/hooks/gow2_func_overrides.cpp` (`func_0002F3F0`), e o
`ppu_lifter.py` emite a funcao como o par `PPC_FUNC_IMPL(func_0002F3F0)`
(corpo, simbolo `__imp_`) + `PPC_FUNC(func_0002F3F0)` (wrapper FRACO) a partir
da entrada `[[functions_override]] address = 0x0002F3F0` de
`games/gow2/config/gow2_recomp.toml`. Um lift feito com `--config` +
`emit_weak_wrappers` ja' vem com o fix -- sem correr patch nenhum.

NAO e' mid-asm (o `alvo` do ledger foi corrigido de `midasm` para `weak` nesta
fase): o guard precisa de um RETURN ANTECIPADO e de um TECTO no proprio loop,
e um `[[midasm_hook]]` corre AO LADO de uma instrucao sem mudar o fluxo.

O ficheiro NAO foi apagado (mesma politica A do 17-03): a producao ainda corre
lifts ANTIGOS, gerados sem `--config`, e esses continuam a precisar do texto
injectado. Sem a deteccao WEAK abaixo, um lift novo daria "func_0002F3F0 nao
existe" e rc=1 -- vermelho FALSO, porque o fix esta' la', vindo do host.

Contrato de rc
--------------
  rc=0  weak override presente (WEAK, skip -- o host e' a fonte de verdade)
  rc=0  aplicado, ou ja' aplicado (ALREADY, no-op verificavel por hash)
  rc=1  `func_0002F3F0` nao existe em nenhum ficheiro dado (lift errado)
  rc=2  a funcao existe mas o corpo gerado NAO tem as 4 ancoras esperadas
        -> RECUSA. O lifter mudou de forma: revalidar contra um lift
        conhecido-bom e regerar este script. Nunca substituir as cegas.
  rc=3  nenhum ficheiro de lift legivel

Uso:  patch_b71_2f3f0_guard.py [LIFT_DIR_OU_FICHEIRO...]
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lift_paths import resolve_lift_paths            # noqa: E402

SIG = "void func_0002F3F0(ppu_context* ctx) {\n"

# Marcador de idempotencia: so' este bloco escreve esta string.
MARKER = "[B71] 2F3F0 bad str="

# Forma que o lifter emite quando a funcao esta' declarada em
# [[functions_override]] com [main].emit_weak_wrappers = true (Fase 19). E' ela
# propria o delimitador de inicio da funcao, por isso e' procurada no ficheiro
# inteiro e nao dentro de uma regiao (que so' existiria depois de a encontrar).
WEAK_IMPL = "PPC_FUNC_IMPL(func_0002F3F0)"

# --- ancoras (todas verificadas UMA unica vez dentro do corpo da funcao) -----
# A1: cabecalho + 1a instrucao do guest. O prologo entra entre as duas.
A1 = SIG + "        ctx->gpr[11] = vm_read8(ctx->gpr[4] + 0x0);\n"
# A2: ultima instrucao antes do topo do loop.
A2 = "        ctx->gpr[10] = (int64_t)(int32_t)(0);\nloc_0002F404:\n"
# A3: avanco do cursor da string dentro do loop.
A3 = "        ctx->gpr[11] = vm_read8(ctx->gpr[4] + 0x1); ctx->gpr[4] += 0x1;\n"
# A4: ramo de volta ao topo do loop (o que nao tem tecto).
A4 = "        if ((!((ctx->cr >> 0) & 2))) goto loc_0002F404;\n"

# --- bloco reposto (verbatim de recomp_macos_v2.pre_v4) ----------------------
GUARD = (
    "        /* String hash for 393E0 name registry. Guard bad/long strings so full\n"
    "         * 393E0 cannot hang forever on non-null garbage memory. */\n"
    "        uint32_t _sp = (uint32_t)ctx->gpr[4];\n"
    "        if (_sp < 0x10000u || _sp >= 0x4F000000u) {\n"
    "            ctx->gpr[3] = 0;\n"
    "            { static int _n=0; if(_n++<16)\n"
    "                fprintf(stderr,\"[B71] 2F3F0 bad str=0x%08X → 0\\n\", _sp); }\n"
    "            return;\n"
    "        }\n"
    "        uint32_t _h = 0;\n"
    "        int _len = 0;\n"
    "        for (;;) {\n"
    "            uint8_t _c = (uint8_t)vm_read8(_sp + (uint32_t)_len);\n"
    "            if (_c == 0) break;\n"
    "            _h = (_h * 31u) + (uint32_t)_c; /* temp; real hash below preserves guest */\n"
    "            _len++;\n"
    "            if (_len > 256) {\n"
    "                { static int _n=0; if(_n++<8)\n"
    "                    fprintf(stderr,\"[B71] 2F3F0 cap str=0x%08X\\n\", _sp); }\n"
    "                break;\n"
    "            }\n"
    "        }\n"
    "        /* Guest algorithm (identical to original lift): */\n"
    "        ctx->gpr[4] = _sp;\n"
)

R1 = SIG + GUARD + "        ctx->gpr[11] = vm_read8(ctx->gpr[4] + 0x0);\n"
R2 = ("        ctx->gpr[10] = (int64_t)(int32_t)(0);\n"
      "        { int _nch = 0;\n"
      "loc_0002F404:\n")
R3 = A3 + "        _nch++;\n"
R4 = ("        if ((!((ctx->cr >> 0) & 2)) && _nch < 256) goto loc_0002F404;\n"
      "        }\n")

# Simbolos de que o bloco depende e que este script NAO instala.
DEPS = (("fprintf", "#include <stdio.h>"),)


def region(text: str):
    """(inicio, fim) do corpo de func_0002F3F0, ou None se nao estiver aqui."""
    i = text.find(SIG)
    if i < 0:
        return None
    j = text.find("\nvoid func_", i + len(SIG))
    return (i, len(text) if j < 0 else j)


def write(path: Path, text: str) -> None:
    try:
        path.write_text(text, encoding="utf-8", newline="\n")
    except TypeError:                                  # Python < 3.10
        path.write_text(text, encoding="utf-8")


def do_file(path: Path) -> int:
    """0 aplicado | 1 ALREADY | 2 WEAK | -1 nao tem a funcao |
    -2 ancoras inesperadas."""
    t = path.read_text(encoding="utf-8", errors="replace")
    if WEAK_IMPL in t:
        # Lift com [[functions_override]]: a funcao inteira e' host. Injectar o
        # bloco aqui nao casaria (a forma `void func_0002F3F0(...)` ja' nao
        # existe) e, se casasse, duplicava o guard.
        print(f"  {path.name}: func_0002F3F0 WEAK (override host -- nada a injectar)")
        return 2
    span = region(t)
    if span is None:
        return -1
    b0, b1 = span
    if MARKER in t[b0:b1]:
        print(f"  {path.name}: func_0002F3F0 ALREADY")
        return 1

    body = t[b0:b1]
    missing = [n for n, a in (("A1", A1), ("A2", A2), ("A3", A3), ("A4", A4))
               if body.count(a) != 1]
    if missing:
        print(f"  {path.name}: ancoras nao-unicas/ausentes -> {', '.join(missing)}",
              file=sys.stderr)
        return -2

    new_body = body.replace(A1, R1, 1).replace(A2, R2, 1)
    new_body = new_body.replace(A3, R3, 1).replace(A4, R4, 1)
    write(path, t[:b0] + new_body + t[b1:])
    print(f"  {path.name}: func_0002F3F0 APPLIED "
          f"(+{new_body.count(chr(10)) - body.count(chr(10))} linhas, "
          f"bounds-check + tecto de 256)")
    return 0


def main() -> int:
    paths = [p for p in resolve_lift_paths(sys.argv[1:], "recomp_macos_v2")
             if p.is_file()]
    if not paths:
        print("ERRO: nenhum ficheiro de lift legivel", file=sys.stderr)
        return 3

    hits, host = [], None
    for p in paths:
        r = do_file(p)
        hits.append(r)
        if r in (0, 1):
            host = p

    if any(r == 2 for r in hits):
        print("[b71-2f3f0-guard] SKIP: weak override e' a fonte de verdade neste "
              "lift (games/gow2/hooks/gow2_func_overrides.cpp)")
        return 0
    if any(r == -2 for r in hits):
        print("ERRO: func_0002F3F0 existe mas o corpo gerado NAO e' o esperado.\n"
              "  O lifter mudou de forma. Nao substituo as cegas: revalida o\n"
              "  bloco contra recomp_macos_v2.pre_v4 e regenera este script.",
              file=sys.stderr)
        return 2
    if not any(r in (0, 1) for r in hits):
        print("ERRO: func_0002F3F0 nao existe em nenhum dos ficheiros dados.",
              file=sys.stderr)
        return 1

    if host is not None:
        t = host.read_text(encoding="utf-8", errors="replace")
        absent = [name for name, needle in DEPS if needle not in t]
        if absent:
            print(f"AVISO: {host.name} usa mas nao inclui -> " + ", ".join(absent))
            print("  (o preambulo do chunk e' injeccao com dono proprio; sem "
                  "<stdio.h> o chunk nao compila)")
    print("[b71-2f3f0-guard] ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
