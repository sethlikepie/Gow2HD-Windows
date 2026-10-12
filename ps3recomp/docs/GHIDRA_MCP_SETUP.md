# GHIDRA_MCP_SETUP — MCP de Ghidra para agentes: opcional, de LEITURA, com versão fixada

> **Estado: OPCIONAL.** Esta página é a metade *soft* do `RE-03` (Fase 20, marco v1.3).
> O caminho duro e determinista da trilha de RE é outro e já está fechado:
> [`docs/RE_PLAYBOOK_GOW2.md`](RE_PLAYBOOK_GOW2.md) + o export estático
> `ghidra_out/` lido por `tools/ghidra_lookup.py`.
> **Se o MCP não subir, nada se perde** — o plano de origem diz isto por escrito
> ("Se MCP falhar/instável: M4.2 sozinho basta para fechar Track R").
>
> **Nada aqui pode entrar no CI.** O `verify_lift.sh`, o `accept_relift.sh` e o passo
> `VERIFY_ORACLE=1` **não** dependem de MCP, nunca. O oráculo continua a ser o export
> estático com `MANIFEST.json` (sha256 do ELF + data + versão do Ghidra).
>
> Veredicto desta máquina em 2026-08-02: **`wont`** — ver §9 e §10. Os motivos estão
> medidos, não presumidos.

---

## 1. Para que serve (e para que NÃO serve)

| serve para | **não** serve para |
|---|---|
| navegar o mapa guest ao vivo: "o que chama este EA?", "mostra-me este decompilado" | produzir código de produto (`runtime/`, `libs/`, `games/gow2/hooks/`) |
| anotar a base de dados do **Ghidra** (nomes, comentários) enquanto se lê | reescrever o binário, aplicar patches, exportar o programa por cima |
| poupar rodadas de `grep` numa sessão de wall | substituir o export estático como oráculo do `verify_lift` |
| dar ao agente xrefs **reais** (base de referências do Ghidra), que o `--callers` do CLI não tem | fechar uma parede — um veredicto continua a precisar de medição (playbook §5) |

A regra de uma frase, que é a parte que importa deste documento:

> **MCP = leitura e anotação. Código de produto só com diff revisto e teste.**
> **Nunca** um LLM a emitir ou a "corrigir" um `ppu_recomp_*.cpp` — anti-padrão 8 do
> playbook e meta explícita de **não**-fazer deste marco.

---

## 2. Escolha fixada (versão pinada)

**Primário: [`LaurieWired/GhidraMCP`](https://github.com/LaurieWired/GhidraMCP), tag `1.4`.**

| campo | valor medido em 2026-08-02 |
|---|---|
| tag / release | `1.4` — "GhidraMCP 1.4", publicado `2025-06-23T04:18:18Z` |
| commit da tag | `27f316f80139` |
| artefacto | `GhidraMCP-release-1-4.zip` (31 180 bytes) |
| `sha256` do artefacto | `b81ca5240fdde57ea4e899170dcd9fdee6ed29246280c74263a509bfcfc7e734` |
| Ghidra declarado pela extensão | `extension.properties` → `version=11.3.2`, `ghidraVersion=11.3.2` |
| conteúdo | `GhidraMCP-1-4.zip` (extensão Ghidra: `lib/GhidraMCP.jar` + `extension.properties` + `Module.manifest`) e `bridge_mcp_ghidra.py` (o servidor MCP em Python) |

Como se re-verifica o pin (leitura, sem instalar nada):

```bash
curl -sSL -o /tmp/GhidraMCP-release-1-4.zip \
  https://github.com/LaurieWired/GhidraMCP/releases/download/1.4/GhidraMCP-release-1-4.zip
shasum -a 256 /tmp/GhidraMCP-release-1-4.zip
# esperado: b81ca5240fdde57ea4e899170dcd9fdee6ed29246280c74263a509bfcfc7e734
unzip -p /tmp/GhidraMCP-release-1-4.zip '*/GhidraMCP-1-4.zip' > /tmp/ext.zip
unzip -p /tmp/ext.zip '*/extension.properties'      # a versão de Ghidra que a extensão exige
```

**Alternativas conhecidas — declaradas como NÃO-objectivo desta fase** (listadas para quem
retomar não ter de repetir a pesquisa; nenhuma foi instalada nem avaliada aqui):

| repo | nota |
|---|---|
| `bethington/ghidra-mcp` | ~200 tools; superfície muito maior, logo mais difícil de restringir à política de leitura |
| `cyberkaida/reverse-engineering-assistant` | extensão de Ghidra, activo |
| `symgraph/GhidrAssistMCP` | servidor MCP nativo como extensão |
| `clearbluejar/pyghidra-mcp` | via `pyghidra`, **headless** — o único candidato que evitaria os passos de GUI da §4; exigiria porém análise nova do EBOOT (ver §10, motivo 4) |

Trocar de primário é uma decisão para outra fase, **com este documento actualizado no mesmo
commit**. Um pin que não se mexe é o que torna "o MCP diz X" auditável.

---

## 3. Requisitos do host

| requisito | valor nesta máquina (medido) | conflito? |
|---|---|---|
| Ghidra | `12.1.2` (`/opt/homebrew/Cellar/ghidra/12.1.2`, `application.version=12.1.2`) | **SIM** — a extensão 1.4 declara `ghidraVersion=11.3.2` |
| JDK | Ghidra 12 exige **JDK 21**; o `openjdk@21` do brew é *keg-only* (fora do `PATH`) — o `analyze_eboot_ghidra.sh` já resolve isto sozinho | não |
| Python | `python3` + o SDK de MCP (`mcp`) para correr o `bridge_mcp_ghidra.py` | não |
| SO | macOS/arm64 é o alvo primário deste port | não |

Nota sobre o JDK: no macOS, `command -v java` **não** chega — existe um stub em `/usr/bin/java`
que responde e falha a correr. Testar com `java -version`.

---

## 4. Instalação (contorno, sem segredos)

### 4.1 Extensão no Ghidra — passos de **GUI** (é aqui que um agente autónomo pára)

1. Abrir o Ghidra (`ghidraRun`).
2. `File` → `Install Extensions`.
3. Botão `+`, escolher o `GhidraMCP-1-4.zip` **interior** (o que vem dentro do zip de release).
4. **Reiniciar** o Ghidra.
5. `File` → `Configure` → `Developer` → activar `GhidraMCPPlugin`.
6. Opcional: `Edit` → `Tool Options` → `GhidraMCP HTTP Server` para mudar a porta.

Não há caminho headless publicado na release 1.4. **Um agente sem interacção humana não
completa os passos 1–5** — e a regra desta casa é não aceitar prompts interactivos nem `sudo`.

### 4.2 Servidor MCP (ponte Python)

O `bridge_mcp_ghidra.py` fala com o *plugin* por HTTP simples:

| parâmetro | default medido no `bridge_mcp_ghidra.py` da 1.4 |
|---|---|
| `--ghidra-server` | `http://127.0.0.1:8080/` |
| `--transport` | `stdio` (alternativa: `sse`) |
| `--mcp-host` / `--mcp-port` | `127.0.0.1` / `8081` (só no modo `sse`) |

```bash
# modo sse (o modo stdio é lançado pelo próprio cliente)
python3 /CAMINHO/ABSOLUTO/bridge_mcp_ghidra.py \
  --transport sse --mcp-host 127.0.0.1 --mcp-port 8081 \
  --ghidra-server http://127.0.0.1:8080/
```

### 4.3 Registo no cliente — **config LOCAL, nunca no git**

Exemplo com **placeholders**. Nunca colar tokens, cookies, chaves nem caminhos com
credenciais:

```jsonc
{
  "mcpServers": {
    "ghidra": {
      "command": "python3",
      "args": [
        "<CAMINHO_ABSOLUTO_LOCAL>/bridge_mcp_ghidra.py",
        "--ghidra-server", "http://127.0.0.1:8080/"
      ]
    }
  }
}
```

Onde vive esta configuração (tudo **fora** deste repositório):

- Claude Desktop: `~/Library/Application Support/Claude/claude_desktop_config.json`
- Claude Code: `~/.claude.json` (ou `~/.claude/`, já ignorado pelo `.gitignore:105`)
- Grok / outros clientes: a config do próprio cliente, no `$HOME`

**Regras de segredo, não negociáveis:**

- **Nada de segredos no git.** Se algum dia existir um `.mcp.json` na raiz deste repo,
  acrescenta-o ao `.gitignore` **antes** de o criares — hoje ele não existe e o
  `.gitignore` só cobre `.claude/`.
- O servidor HTTP do plugin **não tem autenticação** e aceita `POST` que **renomeiam e
  comentam** na tua base de dados do Ghidra. Fica em `127.0.0.1`, nunca em `0.0.0.0`,
  e desliga-se quando não está a ser usado.
- Nenhuma variável de ambiente com credenciais entra em scripts commitados.

---

## 5. Tools PERMITIDAS (whitelist)

Nomes reais, extraídos do `bridge_mcp_ghidra.py` da release 1.4 (27 tools no total).

### 5.1 Leitura — sempre permitidas

`decompile_function`, `decompile_function_by_address`, `disassemble_function`,
`get_function_by_address`, `get_current_address`, `get_current_function`,
`list_functions`, `list_methods`, `list_classes`, `list_segments`, `list_imports`,
`list_exports`, `list_namespaces`, `list_data_items`, `list_strings`,
`search_functions_by_name`, `get_xrefs_to`, `get_xrefs_from`, `get_function_xrefs`

As três de xrefs são a razão de ser do MCP: respondem pela **base de referências** do
Ghidra, enquanto o `--callers` do CLI é uma procura de texto no C decompilado (ver §8.2).

### 5.2 Anotação — permitidas **só na base de dados do Ghidra**, e declaradas no ledger

`rename_function`, `rename_function_by_address`, `rename_data`, `rename_variable`,
`set_decompiler_comment`, `set_disassembly_comment`

Um nome ou comentário melhor no Ghidra é trabalho útil e reversível. **Não** é trabalho de
produto: nada disto toca o repositório.

### 5.3 Condicionais — mudam o que o decompilador emite

`set_function_prototype`, `set_local_variable_type`

Estas duas **alteram o C decompilado** e portanto alteram o que um export futuro vai dizer.
Permitidas apenas se, no mesmo ledger, ficar escrito o quê e porquê. **Proibidas** entre o
momento em que um export é usado como oráculo e o `MANIFEST.json` correspondente — senão
"o Ghidra diz X" passa a significar "o agente ensinou o Ghidra a dizer X".

---

## 6. Tools e comportamentos PROIBIDOS

1. **Exportar / reescrever o programa inteiro** por MCP — qualquer tool de *export*,
   *save program*, *write memory* ou equivalente. O export é feito pelo caminho
   determinista (`games/gow2/analyze_eboot_ghidra.sh`), que carimba `MANIFEST.json`.
2. **Patch cego do binário** — assemblar, escrever bytes, aplicar *patches* no programa.
   Um fix ao guest vive num `patch_*.py` idempotente, num `[[midasm_hook]]` ou num weak
   override; **nunca** num byte escrito por um modelo.
3. **Gerar código de produto** para `runtime/`, `libs/`, `games/gow2/hooks/` ou qualquer
   `ppu_recomp_*.cpp` a partir do que o MCP mostrou. O lift é saída determinista do
   `tools/ppu_lifter.py`. Anti-padrão 8 do playbook.
4. **Substituir o oráculo estático.** O `VERIFY_ORACLE=1` do `verify_lift.sh` lê o
   `ghidra_out/` com `MANIFEST.json`. MCP **nunca** entra nesse caminho, nem no
   `accept_relift.sh`, nem em CI.
5. **Escrever no repositório** a partir de uma tool de MCP. O que sai de uma sessão de MCP
   são **contagens e excertos** colados à mão num ledger — nunca dumps.
6. **Commitar o projecto do Ghidra**, os JSON do `ghidra_out/`, o `.gpr`/`.rep`, logs de
   boot ou dados de jogo. Regra 1 do `CLAUDE.md`.
7. **Expor a porta do plugin** fora de `127.0.0.1`.

---

## 7. A política, em quatro linhas

1. **Ler e anotar** — sim. **Produzir produto** — não.
2. Todo o código que entra no repo passa por **diff revisto + teste**, venha de onde vier.
3. O **oráculo** é o export estático com proveniência; o MCP é conveniência de navegação.
4. Toda a afirmação sai com **nível de evidência** (`in-boot` / `offline-unit` /
   `não-exercitado`), como manda o playbook §5. Uma resposta de MCP é, no melhor dos casos,
   `offline-unit`.

---

## 8. Procedimento de paridade (o aceite *soft* do `RE-03`)

A ideia: se o MCP e o oráculo estático discordam sobre um facto simples, um dos dois está a
mentir — e é melhor descobri-lo num EA conhecido do que a meio de uma parede.

### 8.1 Receita

```bash
cd /Users/.../ps3recomp
EA=0x0041F700                      # parede 4 da sessão de 20-02, já medida
GHOUT=../gow2-recomp/ghidra_out

# lado A — oráculo estático (determinista, é a referência)
.venv/bin/python tools/ghidra_lookup.py "$GHOUT" "$EA" --callers --no-c --max 50 | sort

# lado B — MCP, no cliente do humano, sobre o MESMO EBOOT
#   get_function_xrefs / get_xrefs_to para o mesmo EA  → ordenar a lista
```

Compara os **conjuntos**. Escreve os dois, ordenados, no apêndice desta página. Se
diferirem: **regista a diferença e o veredicto `falha`. Não forces o match.**

### 8.2 Aviso obrigatório antes de comparar: os dois lados respondem a perguntas DIFERENTES

Medido em 2026-08-02 sobre `ghidra_out/` (13 143 funções, 13 096 com C):

- o `--callers` do `ghidra_lookup.py` é **`grep` no C decompilado**, não uma consulta de
  xrefs. Só encontra quem *menciona* o nome ou o endereço em texto;
- `get_xrefs_to` do MCP responde pela **base de referências do Ghidra** — inclui chamadas
  que o texto do decompilado não mostra.

Logo, **é esperado que o MCP devolva um superconjunto**. Uma diferença nesse sentido é
informação, não falha do MCP. O que seria falha: o MCP **omitir** um caller que o texto
mostra, ou inventar um que não existe em lado nenhum.

O que o `--callers` de facto faz, medido: procura o **nome** da função no C **e** os 8 dígitos
hexadecimais do endereço, em minúsculas, por substring. Como os 13 096 nomes do export são
todos da forma `.opd.FUN_<próprio endereço>`, a segunda condição apanha sempre a primeira —
o que o torna um **superconjunto** das chamadas directas por nome, ao custo de também apanhar
menções de **dados** (`DAT_00420a44`, constantes). É por isso que o cabeçalho da saída diz
*"quem menciona"* e não *"quem chama"*.

**Confirma sempre que a referência está sã antes de acusar o MCP de discordar dela** — em
§9.3 está uma hipótese de bug nesta mesma função que parecia sólida e caiu numa sonda de
controlo. Se ela tivesse ido para o ledger sem a sonda, a paridade teria sido comparada
contra uma referência imaginária.

---

## 9. Apêndice — sessão de 2026-08-02 (plano 20-03)

Máquina: macOS/arm64, `ps3recomp` em `macos-arm64-port-f0-f2`.
**Resultado curto: paridade NÃO corrida — o lado do MCP não chegou a subir. Veredicto
`wont`, com os quatro bloqueios medidos abaixo. O lado do CLI ficou executado e escrito,
para que quem tenha o MCP a funcionar só tenha de correr metade.**

### 9.1 O que bloqueou o lado do MCP (medido, não presumido)

| # | bloqueio | medição |
|---|---|---|
| 1 | **Versão incompatível** | a única release do primário (`1.4`, 2025-06-23) declara `version=11.3.2` / `ghidraVersion=11.3.2` no `extension.properties`; o Ghidra desta máquina é `12.1.2` (`Ghidra/application.properties` → `application.version=12.1.2`) |
| 2 | **O Ghidra 12 valida isso ao instalar** | `Ghidra/Framework/Project/lib/Project.jar` → `ghidra/framework/project/extensions/ExtensionInstaller.class` contém o método `validateExtensionVersion`, a chamada a `getApplicationVersion` e o diálogo **"Extension Version Mismatch"** |
| 3 | **Instalação é só por GUI** | os passos 1–6 da §4.1 são `File → Install Extensions → + → reiniciar → Configure → Developer`. A release 1.4 não publica caminho headless. Reconstruir a extensão para o 12.1.2 exigiria Maven — `which mvn` → **ausente** — mais a cópia de 8 jars do Ghidra |
| 4 | **Custo do programa vivo** | não há projecto do Ghidra persistido em disco (`find ~ … -name '*.gpr' -o -name '*.rep'` → **nada**; o `analyze_eboot_ghidra.sh` usa projecto temporário). Um servidor MCP precisaria de auto-análise nova do EBOOT (5,6 MB, 13 143 funções) — *"dezenas de minutos"*, pelas palavras do cabeçalho do próprio script |

E um quinto, de natureza diferente e igualmente decisivo para "**o agente** devolve os mesmos
callers": **um agente já em execução não adquire tools a meio da sessão.** Medido:
`~/.claude.json` → `mcpServers: ['MCP_DOCKER']`; não existe `.mcp.json` neste repositório;
a lista de tools desta sessão não contém nenhuma `mcp__ghidra__*`. Registar o servidor exige
editar a config **local** do cliente e reiniciá-lo — trabalho do humano, por desenho.

Nada disto foi forçado: a §4.1 exige prompts de GUI e a reconstrução exigiria instalar um
toolchain. As restrições desta execução proíbem ambos.

### 9.2 Lado do CLI — executado e registado (a metade que fica pronta)

Comando, em ambos os casos:

```bash
.venv/bin/python tools/ghidra_lookup.py ../gow2-recomp/ghidra_out <EA> --callers --no-c --max 50
```

Export usado: `MANIFEST.json` → `elf_sha256 23cfd435…163b`, `ghidra_version 12.1.2`,
`exported_at 2026-07-25T19:26:53Z`, 13 143 funções / 13 096 com C.

**Âncora A — controlo negativo. `EA = 0x0041F700`** (a parede 4 da sessão de 20-02):

```
=== 0x0041F700  .opd.FUN_0041f700  size=296
--- quem menciona 0x0041F700 / .opd.FUN_0041f700 ---
(nenhum -- pode ser chamada indirecta / por tabela)
rc=0
```

Conjunto de callers: **∅**. É o resultado *esperado* e não uma falha: a sessão de 20-02 já
tinha medido que este ciclo fecha por `vtable+0x40` (`ps3_indirect_call`). Serve de controlo
porque um MCP são deve devolver aqui **xrefs indirectas que o CLI não tem** — a diferença
mais interessante da paridade está neste EA.

**Âncora B — controlo positivo. `EA = 0x00420A44`** (zona do typemap, escolhida por ter um
conjunto pequeno e exacto):

```
=== 0x00420A44  .opd.FUN_00420a44  size=2372
--- quem menciona 0x00420A44 / .opd.FUN_00420a44 ---
0x00254C40 .opd.FUN_00254c40
rc=0
```

Conjunto de callers: **{ `0x00254C40` }**, e é uma chamada directa a sério — linha 206 do C
de `_opd_FUN_00254c40`:

```c
_opd_FUN_00420a44(piVar23,1);
```

*(nota lateral, sem afirmação: `0x00254C40` é a função que contém `0x002550C8`, a do fix de
fallthrough cross-fragment. Aqui é só uma âncora conveniente.)*

**Como fechar a paridade quando o MCP existir:** pedir `get_function_xrefs` / `get_xrefs_to`
para os dois EA sobre o **mesmo** EBOOT (`sha256 23cfd435…163b`) e comparar com estes dois
conjuntos. Critério de aceitação honesto, dado o §8.2:

- âncora B: o MCP **tem** de conter `0x00254C40`. Se não contiver → **falha**.
- âncora A: o MCP pode devolver mais do que ∅ (xrefs indirectas) → **isso é sucesso**, não
  divergência. O que seria falha é devolver um caller directo que o C não mostra.

Nível de evidência do que está escrito acima: **`offline-unit`** (export estático, sem boot).
O lado do MCP: **`não-exercitado`**.

### 9.3 A hipótese de bug que caiu na sonda de controlo (fica escrita porque quase passou)

Durante a preparação da âncora, uma sonda minha contou os símbolos que o decompilador emite e
concluiu que o `--callers` teria um falso negativo estrutural: o `functions.json` chama à
função `.opd.FUN_0041f700` mas o C do Ghidra escreve `_opd_FUN_0041f700` (pontos → underscore),
e a comparação de nome do tool é literal. A sonda contava **5 681 funções e 24 570 menções**
que ficariam invisíveis. Parecia um achado sólido, do mesmo tipo do falso negativo do
`--writes` já registado (`WINDOWS` id 14).

**Está refutado.** A sonda de controlo foi correr o tool na âncora B *antes* de escrever o
achado: `0x00420A44` devolveu `0x00254C40` — exactamente o caller que a hipótese dizia estar
perdido. Razão: o tool tem uma **segunda** condição, `needle_addr in c.lower()`, e
`_opd_FUN_00420a44` **contém** a substring `00420a44`. Verificado depois em todo o export:
das 13 096 funções decompiladas, **13 096** chamam-se `.opd.FUN_<próprio endereço>` e
**nenhuma** tem nome de outra forma — logo a condição de endereço subsome sempre a de nome e
o falso negativo não existe.

O erro estava na **minha** sonda, que procurava `\b[0-9a-f]{8}\b`: em `_opd_FUN_00420a44` não
há fronteira de palavra antes do hexadecimal, porque `_` é carácter de palavra. O tool, que
usa substring simples, não tinha esse problema.

Fica aqui por duas razões: **nenhuma alteração foi feita ao `tools/ghidra_lookup.py`** (não
havia nada para corrigir), e porque é o playbook §4 a funcionar — *"sonda de CONTROLO em cada
bissecção"*. Sem ela, este documento teria acusado o oráculo de um defeito inexistente e a
paridade teria sido medida contra uma referência imaginária.

---

## 10. Modo de falha — o molde do `wont`

Se a instalação for só-GUI, bloqueada em headless, ou o servidor for instável, o resultado
correcto é **declarar** e fechar. `wont` medido é entrega; silêncio não é.

```
status: wont
reason: <uma frase medida, com o número/versão que a suporta>
attempted: <o que foi de facto tentado, com comandos>
date: YYYY-MM-DD
fallback: docs/RE_PLAYBOOK_GOW2.md + tools/ghidra_lookup.py (Track R fecha sem MCP)
```

### O `wont` desta máquina

```
status: wont
reason: A unica release do GhidraMCP (1.4, 2025-06-23) declara ghidraVersion=11.3.2 e o
        Ghidra desta maquina e' 12.1.2; o proprio instalador do Ghidra 12 valida a versao
        (ExtensionInstaller.validateExtensionVersion -> dialogo "Extension Version
        Mismatch"). Instalar exigiria passos de GUI e reconstruir a extensao exigiria
        Maven (ausente) -- ambos proibidos por esta execucao (sem prompts interactivos,
        sem instalar toolchains). Alem disso nao ha projecto Ghidra persistido, logo um
        servidor MCP obrigaria a nova auto-analise do EBOOT (dezenas de minutos), e um
        agente ja' em execucao nao adquire tools MCP a meio da sessao.
attempted: pin e verificacao do artefacto (sha256 b81ca524...c7e734); leitura do
        extension.properties (11.3.2); leitura da versao instalada
        (application.version=12.1.2); localizacao da validacao no Project.jar;
        `which mvn` (ausente); `find` por *.gpr/*.rep (nada); inventario de mcpServers
        em ~/.claude.json (so' MCP_DOCKER); lado do CLI da paridade corrido e escrito
        na 9.2 (duas ancoras, saida literal).
date: 2026-08-02
fallback: docs/RE_PLAYBOOK_GOW2.md + tools/ghidra_lookup.py sobre ghidra_out/ com
        MANIFEST.json (Track R fecha sem MCP -- o RE-02 ja' fechou com uma sessao real)
```

**O que este documento NÃO afirma:**

- **não** afirma que o GhidraMCP está partido — afirma que a release fixada não declara
  suporte para o Ghidra **desta** máquina, e que ninguém a instalou aqui;
- **não** afirma que a paridade falharia; afirma que **não foi corrida** do lado do MCP;
- **não** afirma que nenhum servidor MCP de Ghidra funciona com o Ghidra 12 — as
  alternativas da §2 não foram avaliadas;
- **não** mediu estabilidade, latência nem determinismo de nenhum servidor.

**Para quem retomar** (ordem por custo crescente):

1. Instalar o primário à mão pela §4.1 num Ghidra **11.3.2** e correr a §9.2 — é o teste
   mais barato da política, e não obriga a mexer no Ghidra 12 desta máquina.
2. Avaliar `clearbluejar/pyghidra-mcp` (§2), o único candidato headless — mas contar com o
   custo 4 da §9.1 (auto-análise nova) e **não** instalar nada no `.venv` deste repositório
   enquanto outro plano da fase estiver a correr.
3. Só depois, considerar reconstruir a extensão para o Ghidra 12 (Maven + 8 jars).

Enquanto nada disto acontecer, **o `RE-03` fecha na mesma**: o documento com versão fixada e
política existe, a paridade tem metade executada e escrita, e o caminho duro nunca dependeu
do MCP.

---

## 11. Actualização — 2026-08-03: o caminho de build foi corrido (o `wont` da §10 fica como proveniência)

> As §9 e §10 **não** são reescritas: eram medições correctas **sob as restrições daquela
> execução** (sem instalar toolchains, sem prompts). Esta secção acrescenta o que mudou numa
> execução **autorizada a instalar Maven** e a correr `analyzeHeadless`.

O bloqueio 1 da §9.1 (versão 11.3.2 vs 12.1.2) foi resolvido pelo **caminho fiel**, não pelo
remendo: `brew install maven`, copiar os 8 jars do Ghidra **12.1.2**, pôr `12.1.2` no
`extension.properties` **e** no `pom.xml`, e `mvn clean package` com **JDK 21** →
compilação **rc=0, sem quebras de API 11→12**. A extensão instalada declara **`12.1.2` a
sério** (não é a build de 11.3.2 com o campo mudado).

Também caíram os bloqueios 3 e 4:
- **instalação sem GUI** do *ficheiro*: a extensão foi descompactada em
  `~/Library/ghidra/ghidra_12.1.2_PUBLIC/Extensions/GhidraMCP/` (activar o plugin continua
  a ser GUI — passo humano);
- **projecto persistido**: `analyzeHeadless` (sem `-deleteProject`) importou e analisou o
  EBOOT para `~/ghidra_projects/gow2/gow2.{gpr,rep}` (17 MB, `PowerPC:BE:64:A2ALT:default`),
  eliminando a re-análise de dezenas de minutos.

O 5.º bloqueio (**agente já a correr não adquire tools a meio**) permanece por desenho: o
server `ghidra` foi **registado** no `~/.claude.json` e só uma **nova** sessão o carrega.

**Estado agora: montado, à espera de 2 passos humanos** (abrir a GUI + ligar o plugin com o
projecto/EBOOT aberto; reiniciar a sessão do Claude Code). O passo-a-passo com `rc`, shas e a
checklist copy-paste está em
[`games/gow2/notes/2026-08-03-ghidra-mcp-setup.md`](../games/gow2/notes/2026-08-03-ghidra-mcp-setup.md).
A paridade da §8/§9.2 continua por correr do lado do MCP até esses dois passos acontecerem.
