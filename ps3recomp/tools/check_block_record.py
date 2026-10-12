#!/usr/bin/env python3
"""Verificador de formato dos registos de bloco (Fase 23 / UPST-02, marco v1.4).

Porque existe: o criterio 4 da Fase 23 obriga a separar, em DUAS seccoes
distintas, o que o portao de adopcao prova (nao-regressao da intro) do que ele
NAO prova (que o caminho alterado pelo bloco foi exercitado). Enquanto essa
separacao viver so' na prosa de `docs/upstream-blocks/README.md`, e' uma
promessa; aqui passa a ser um comando que REPROVA um registo que as junte.

Regras impostas (cada violacao nomeia o ficheiro e a linha):

  1. Existe EXACTAMENTE uma seccao `## O que este portao prova` e EXACTAMENTE
     uma `## O que este portao NAO prova`, e nenhuma delas fica vazia. Um
     registo com so' uma das duas -- ou com as duas fundidas num titulo so' --
     e' precisamente o modo de falha que o criterio 4 existe para impedir.
  2. Cada afirmacao (linha de lista) dessas seccoes traz uma marca de evidencia
     `[evidencia: X]`, com X no vocabulario do projecto:
     `in-boot`, `offline-unit`, `nao-exercitado`.
  3. Nenhuma afirmacao traz mais do que UMA marca: duas marcas na mesma linha
     sao duas afirmacoes disfarcadas de uma -- a forma tipica de superdeclarar.
  4. A seccao `## O que este portao NAO prova` nao pode trazer nenhuma
     afirmacao `[evidencia: in-boot]`: uma coisa que o portao NAO prova nao pode
     trazer o nivel de evidencia mais forte do projecto.
  5. O cabecalho tem as chaves `bloco`, `sha_upstream`, `ancora_nossa`, `fase` e
     `data`, cada uma numa linha `chave: valor` (o `- ` inicial do formato de
     lista do README e' aceite) antes da primeira seccao.

Uso:
    python3 tools/check_block_record.py docs/upstream-blocks/LV2S-01.md
    python3 tools/check_block_record.py docs/upstream-blocks/

rc=0 quando tudo passa, rc=1 quando ha' pelo menos uma violacao. A saida lista
SEMPRE todas as violacoes, nunca so' a primeira: quem corrige um registo tem de
ver a lista inteira numa corrida.

Seleccao de registos num DIRECTORIO (regra explicita, para o gate nao reprovar
documentacao que nao e' registo): entram os ficheiros cujo nome bate
`^[A-Z][A-Z0-9]*-[0-9]+\\.md$` (ex.: `LV2S-01.md`) e, alem desses, qualquer
`.md` que traga uma chave `bloco:` no cabecalho (antes da primeira seccao
`## `) -- para nao se poder fugir ao verificador so' mudando o nome do
ficheiro. Ficam de fora, por construcao, `README.md` e
`GATE-MUTATION-PROOF.md`. Um ficheiro passado EXPLICITAMENTE na linha de
comandos e' sempre tratado como registo.
"""
from __future__ import annotations

import os
import re
import sys

VOCABULARIO = ("in-boot", "offline-unit", "nao-exercitado")

TITULO_PROVA = "## O que este portao prova"
TITULO_NAO_PROVA = "## O que este portao NAO prova"

CHAVES_OBRIGATORIAS = ("bloco", "sha_upstream", "ancora_nossa", "fase", "data")

RE_NOME_REGISTO = re.compile(r"^[A-Z][A-Z0-9]*-[0-9]+\.md$")
RE_AFIRMACAO = re.compile(r"^\s*[-*]\s+\S")
RE_MARCA = re.compile(r"\[evidencia:\s*([^\]]*)\]")
RE_CABECALHO = re.compile(r"^(?:-\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*:\s*(\S.*)$")


def e_registo(caminho: str) -> bool:
    """Seleccao de registos dentro de um directorio (ver docstring do modulo)."""
    nome = os.path.basename(caminho)
    if RE_NOME_REGISTO.match(nome):
        return True
    # Um `bloco: <valor>` no cabecalho (antes da primeira seccao `## `) marca o
    # ficheiro como registo mesmo com nome livre: mudar o nome nao pode ser
    # forma de fugir ao verificador.
    try:
        with open(caminho, "r", encoding="utf-8") as fh:
            for linha in fh:
                if linha.startswith("## "):
                    return False
                m = RE_CABECALHO.match(linha.rstrip())
                if m and m.group(1).lower() == "bloco":
                    return True
    except OSError:
        return False
    return False


def verificar_ficheiro(caminho: str) -> list[str]:
    """Devolve a lista de violacoes (vazia = registo valido)."""
    problemas: list[str] = []
    try:
        with open(caminho, "r", encoding="utf-8") as fh:
            linhas = fh.read().splitlines()
    except OSError as exc:
        return [f"{caminho}: ILEGIVEL {exc}"]

    # --- indice das seccoes ------------------------------------------------
    idx_prova: list[int] = []
    idx_nao_prova: list[int] = []
    idx_titulos: list[int] = []
    for n, linha in enumerate(linhas, start=1):
        crua = linha.rstrip()
        if crua.startswith("## "):
            idx_titulos.append(n)
            if crua.strip() == TITULO_PROVA:
                idx_prova.append(n)
            elif crua.strip() == TITULO_NAO_PROVA:
                idx_nao_prova.append(n)

    # --- REGRA 5: cabecalho ------------------------------------------------
    fim_cabecalho = min(idx_titulos) if idx_titulos else len(linhas) + 1
    vistas = set()
    for n, linha in enumerate(linhas, start=1):
        if n >= fim_cabecalho:
            break
        m = RE_CABECALHO.match(linha.rstrip())
        if m:
            vistas.add(m.group(1).lower())
    for chave in CHAVES_OBRIGATORIAS:
        if chave not in vistas:
            problemas.append(
                f"{caminho}:1: CABECALHO_INCOMPLETO falta a chave obrigatoria "
                f"'{chave}: <valor>' antes da primeira seccao (regra 5)")

    # --- REGRA 1: as duas seccoes, uma de cada ------------------------------
    for titulo, ocorrencias in ((TITULO_PROVA, idx_prova),
                                (TITULO_NAO_PROVA, idx_nao_prova)):
        if len(ocorrencias) == 0:
            problemas.append(
                f"{caminho}:1: SECCAO_EM_FALTA nao existe nenhuma seccao "
                f"'{titulo}' -- as duas afirmacoes tem de ficar SEPARADAS "
                f"(regra 1)")
        elif len(ocorrencias) > 1:
            problemas.append(
                f"{caminho}:{ocorrencias[1]}: SECCAO_DUPLICADA a seccao "
                f"'{titulo}' aparece {len(ocorrencias)} vezes (linhas "
                f"{','.join(str(i) for i in ocorrencias)}) (regra 1)")

    # --- afirmacoes de cada seccao -----------------------------------------
    def afirmacoes(inicio: int) -> list[tuple[int, str]]:
        """Linhas de lista entre `inicio` (exclusive) e o proximo `## `."""
        saida = []
        for n in range(inicio + 1, len(linhas) + 1):
            crua = linhas[n - 1].rstrip()
            if crua.startswith("## "):
                break
            if RE_AFIRMACAO.match(crua):
                saida.append((n, crua))
        return saida

    for titulo, ocorrencias in ((TITULO_PROVA, idx_prova),
                                (TITULO_NAO_PROVA, idx_nao_prova)):
        for inicio in ocorrencias:
            itens = afirmacoes(inicio)
            if not itens:
                problemas.append(
                    f"{caminho}:{inicio}: SECCAO_VAZIA a seccao '{titulo}' nao "
                    f"tem nenhuma afirmacao -- uma seccao vazia satisfaria o "
                    f"formato sem dizer nada (regra 1)")
            for n, texto in itens:
                marcas = RE_MARCA.findall(texto)
                # --- REGRA 2: marca presente e no vocabulario ---------------
                if not marcas:
                    problemas.append(
                        f"{caminho}:{n}: SEM_EVIDENCIA a afirmacao nao traz "
                        f"marca '[evidencia: X]' (regra 2). Vocabulario "
                        f"admitido: {', '.join(VOCABULARIO)}")
                    continue
                # --- REGRA 3: uma marca por afirmacao -----------------------
                if len(marcas) > 1:
                    problemas.append(
                        f"{caminho}:{n}: EVIDENCIA_DUPLICADA {len(marcas)} "
                        f"marcas na mesma afirmacao ({', '.join(marcas)}) -- "
                        f"sao duas afirmacoes disfarcadas de uma (regra 3)")
                    continue
                nivel = marcas[0].strip()
                if nivel not in VOCABULARIO:
                    problemas.append(
                        f"{caminho}:{n}: EVIDENCIA_INVALIDA '{nivel}' fora do "
                        f"vocabulario do projecto (regra 2). Vocabulario "
                        f"admitido: {', '.join(VOCABULARIO)}")
                    continue
                # --- REGRA 4: nada `in-boot` no que NAO se prova ------------
                if titulo == TITULO_NAO_PROVA and nivel == "in-boot":
                    problemas.append(
                        f"{caminho}:{n}: IN_BOOT_NO_QUE_NAO_SE_PROVA uma "
                        f"afirmacao da seccao '{TITULO_NAO_PROVA}' esta' "
                        f"marcada '[evidencia: in-boot]' -- o que o portao NAO "
                        f"prova nao pode trazer o nivel de evidencia mais forte "
                        f"do projecto (regra 4)")

    return problemas


def recolher(alvo: str) -> tuple[list[str], list[str]]:
    """-> (registos a verificar, erros de seleccao)."""
    if os.path.isfile(alvo):
        return [alvo], []
    if os.path.isdir(alvo):
        registos = sorted(
            os.path.join(alvo, n) for n in os.listdir(alvo)
            if n.endswith(".md") and e_registo(os.path.join(alvo, n)))
        if not registos:
            return [], [f"{alvo}: SEM_REGISTOS nenhum ficheiro do directorio "
                        f"bate a regra de seleccao de registos de bloco -- um "
                        f"gate que nao ve nada nao e' um gate"]
        return registos, []
    return [], [f"{alvo}: INEXISTENTE nao e' ficheiro nem directorio"]


def main(argv: list[str]) -> int:
    alvos = argv[1:]
    if not alvos:
        print(__doc__ or "", file=sys.stderr)
        print("ERRO: falta o caminho do registo (ficheiro ou directorio)",
              file=sys.stderr)
        return 2

    problemas: list[str] = []
    verificados: list[str] = []
    for alvo in alvos:
        registos, erros = recolher(alvo)
        problemas.extend(erros)
        for reg in registos:
            verificados.append(reg)
            problemas.extend(verificar_ficheiro(reg))

    for p in problemas:
        print(f"[bloco] {p}")

    print(f"[bloco] resumo registos={len(verificados)} "
          f"violacoes={len(problemas)} "
          f"({', '.join(os.path.basename(v) for v in verificados) or 'nenhum'})")
    return 1 if problemas else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
