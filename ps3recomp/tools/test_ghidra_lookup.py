#!/usr/bin/env python3
"""Testa o tools/ghidra_lookup.py com um export sintetico do Ghidra.

Sem Ghidra, sem Java, sem EBOOT: escreve functions.json/decompiled.json num
tmpdir no formato que o tools/ghidra/ExportAnalysisJson.java produz e verifica
os tres modos que interessam ao debugging (EA -> funcao, --writes, --callers).
"""
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "ghidra_lookup.py")

FUNCS = [
    {"addr": "0x002B2DD0", "size": 256, "name": "FUN_002b2dd0", "thunk": False},
    {"addr": "0x000B951C", "size": 128, "name": "Product__attach", "thunk": False},
]
DECOMP = [
    {"addr": "0x002B2DD0", "name": "FUN_002b2dd0",
     "c": "void FUN_002b2dd0(int *p)\n{\n  if (p[3] != 0) {\n"
          "    *(int *)(p + 0xf4) = 1;\n  }\n  Product__attach(p);\n  return;\n}\n"},
    {"addr": "0x000B951C", "name": "Product__attach",
     "c": "void Product__attach(int *p)\n{\n  work(p);\n  return;\n}\n"},
]

FAILS = []


def check(name, cond, detail=""):
    if cond:
        print(f"[ghidra-lookup] ok   {name}")
    else:
        print(f"[ghidra-lookup] FAIL {name} {detail}")
        FAILS.append(name)


def run(d, *args):
    cp = subprocess.run([sys.executable, TOOL, d, *args],
                        capture_output=True, text=True)
    return cp.returncode, cp.stdout + cp.stderr


def main():
    with tempfile.TemporaryDirectory() as d:
        with open(os.path.join(d, "functions.json"), "w") as f:
            json.dump(FUNCS, f)
        with open(os.path.join(d, "decompiled.json"), "w") as f:
            json.dump(DECOMP, f)

        rc, out = run(d, "0x002B2DE0")
        check("EA no meio da funcao resolve a funcao contentora",
              rc == 0 and "0x002B2DD0" in out, out[:200])
        check("imprime o C decompilado", "Product__attach(p);" in out, out[:200])
        check("diz o offset dentro da funcao", "16 bytes dentro" in out, out[:200])

        rc, out = run(d, "--writes", "0xF4")
        check("--writes encontra a escrita ao campo",
              rc == 0 and "0x002B2DD0" in out, out[:200])
        rc, out = run(d, "--writes", "0xAB")
        check("--writes nao inventa resultados", "nenhum resultado" in out, out[:200])

        rc, out = run(d, "0x000B951C", "--callers", "--no-c")
        check("--callers encontra quem menciona", "0x002B2DD0" in out, out[:200])
        check("--no-c suprime o corpo", "work(p);" not in out, out[:200])

        rc, out = run(d, "--grep", "Product__attach")
        check("--grep varre o C decompilado", rc == 0 and "0x002B2DD0" in out, out[:200])

        rc, out = run(d, "0xDEADBEEF")
        check("EA desconhecido falha com rc!=0", rc != 0, out[:120])

    with tempfile.TemporaryDirectory() as empty:
        rc, out = run(empty, "0x1000")
        check("sem export do Ghidra da erro util",
              rc != 0 and "ghidra_analyze" in out, out[:160])

    print(f"\n[ghidra-lookup] {'FAILED: ' + ', '.join(FAILS) if FAILS else 'todos os casos passaram'}")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
