# Task 5 — grafo estático do caller chain acima do walk (A1), offline (2026-07-21)

Plano: `../ps3recomp/docs/superpowers/plans/2026-07-21-shader-registry-typemap-walk.md`
Brief: `.superpowers/sdd/reg-task-5-brief.md`. Antecedente: `notes/2026-07-21-typemap-walk-N1.md`
(veredito **A1**: `func_0032E200` — único candidato a chamar o walk `func_00171244` via vt
`0x5130B8+8`/OPD `0x522E70` — nunca é entrado, `CMP-ENTER=0` em 2 boots). Esta task: só
leitura estática do lift (`recomp_macos_v2/ppu_recomp_*.cpp`, gitignored) + cross-check no
runtime/docs do motor. **Nenhum ficheiro `.cpp` editado, nenhum build, nenhum boot.**

## Grafo de chamadas (verificado exaustivamente por `rg`, não pela stack)

```
func_00171244  (walk; ppu_recomp_000.cpp:317308; OPD 0x522E70; vt 0x5130B8+8)
  ^ SÓ alcançável pelo vcall dentro de func_0032E200 (8x sites [CMP-VCALL]); ZERO
    callers diretos em todo o lift (confirmado: só self-def + tabela ppu_recomp_030).

func_0032E200  (ppu_recomp_003.cpp:419639)
  ^ caller: func_0032DF98 :147773 — tail-call CONDICIONAL
    `if ((!((ctx->cr>>0)&2))) { g_trampoline_fn = func_0032E200; return; }`
    ESTE É O ÚNICO CALLER EM TODO O LIFT (confirmado por rg exaustivo: só
    def + tabela ppu_recomp_030 + este site).

func_0032DF98  (ppu_recomp_001.cpp:147742)
  ^ caller REAL: func_00330D54 :149679 — `func_0032DF98(ctx); DRAIN_TRAMPOLINE(ctx);`
    (chamada direta incondicional, nenhum goto/if a saltar por cima)
  ^ "caller" #2 do lead anterior — ppu_recomp_003.cpp:419636 — É CÓDIGO MORTO.
    Ver "Achado 0" abaixo. Corrige o lead de progress-registry.md ("2 callers").

func_00330D54  (ppu_recomp_001.cpp:149630)
  ^ 3 callers, TODOS chamada direta incondicional (sem goto/if a saltar), confirmados
    exaustivamente por rg (só def + tabela + estes 3):
    - func_00468C3C :438754  (ppu_recomp_001.cpp)
    - func_00468CEC :588238  (ppu_recomp_002.cpp)
    - func_00468D04 :588307  (ppu_recomp_002.cpp)

func_00468CEC (ppu_recomp_002.cpp:588222)  <- func_00468E3C :438856 (branch "if !EQ", REAL)
func_00468D04 (ppu_recomp_002.cpp:588298)  <- func_00468E3C :438857 (fallthrough "else", REAL)
  (ambos convergem no mesmo corpo-cauda de func_00330D54; são pares split de um único
   "entry rápido vs entry com spinlock" — não é o precedente 2550C8, ver Achado 3)

func_00468E3C (ppu_recomp_001.cpp:438834)  <- func_00468C3C :438729 (branch "if !EQ", REAL)
  ÚNICO caller (confirmado exaustivo).

func_00468C3C (ppu_recomp_001.cpp:438693)  <- 4 callers diretos incondicionais:
    - func_003294F8 (via func_00329650 :419256, ppu_recomp_003.cpp)
    - func_003296C0 (via func_00329818 :419262, ppu_recomp_003.cpp)
    - func_00329490 :142733  (ppu_recomp_001.cpp) — 1 de 6 sites deste caller
    - func_00329658 :142851  (ppu_recomp_001.cpp) — MAS este próprio func_00329658
      não tem NENHUM caller real (só def + 1 trampolim morto) → path morto, exclui-se.
  func_00329490 tem fan-in largo: 6 callers em 3 ficheiros (ppu_recomp_013/005/001),
  todos do padrão "aloca 0x24 bytes; chama func_00329490" — família de ~10+
  "micro-construtores" quase idênticos para tipos C++ distintos (scene/component).
```

Todas as funções da cadeia estão presentes e linkadas na tabela OPD/nome
(`ppu_recomp_030.cpp`: `00171244:485045`, `0032DF98:493106`, `00330D54:493142`,
`00468C3C:499995`, `00468E3C:499997`, `00468CEC:510398`, `00468D04:510399`,
`0032E200:517111`) — **não é o caso de "função ausente/stub"** (descarta essa hipótese
do item 1 do brief). Também não há nenhuma referência hex-literal (dado/vtable) a
nenhum destes endereços fora dos sites de chamada/tabela já listados — dentro do que
`rg` consegue ver em texto `.cpp` gerado (não cobre vtables como dados binários em
rodata do EBOOT; ver "Próximo experimento").

## Achado 0 — correção de lead: o "2º caller" de `func_0032DF98` é código morto

`ppu_recomp_003.cpp:419606` define `func_0032DF28`, cuja cauda (`loc_0032DF90:`,
:419633-419637) é:

```cpp
loc_0032DF90:
        vm_write32(ctx->gpr[1] + 0x70, ctx->gpr[28]);
        { g_trampoline_fn = (void(*)(void*))func_0032DE4C; return; }   // :419635 — SEMPRE executa e retorna
        { g_trampoline_fn = (void(*)(void*))func_0032DF98; return; }   // :419636 — inalcançável (após return)
```

A instrução em `:419635` é um `return;` incondicional (sem `if` a envolvê-la); `:419636`
está no mesmo bloco, sem label próprio, sem `goto` que a alcance — é estaticamente morta
em C++. Confirmei que não há sibling/cópia de `loc_0032DF90` noutro ficheiro (`rg` só
encontra as 2 ocorrências dentro do próprio `func_0032DF28`), portanto **não** é o
precedente `func_002550C8` (aquele era um fallthrough que caía no alvo ERRADO — aqui o
alvo nominal está certo, só é morto). O lead de `progress-registry.md`
("func_0032E200 ... ÚNICO caller — func_0032DF98") continua correto (não mudou); o que
corrijo é o **outro** lado: `func_0032DF98` tem 1 caller real, não 2.

Encontrei o mesmo padrão de "2º trampolim morto" mais 2 vezes nesta cadeia (linhas
`:419636`→`func_0032DF98` dentro de `func_0032DF28`, e `:438858`→`func_00468E98` dentro
de `func_00468E3C`, este após um `if`/`else` real nas 2 linhas anteriores). Padrão
genérico deste lift: sempre que um bloco termina em tail-call incondicional E o endereço
seguinte coincide com o entry de outra função conhecida, o gerador acrescenta mais um
`{ g_trampoline_fn=next_func; return; }` "de cortesia" que fica morto. Regra prática para
ler este código: numa sequência de 2+ blocos `{...; return;}` seguidos sem `if`, só o
**primeiro** é vivo — a menos que esteja precedido por um `if` (aí é o "else" implícito,
esse sim vivo).

## Achado central — a origem do valor testado no gate `:147773`

Corpo de `func_0032DF98` (`ppu_recomp_001.cpp`), início até ao gate:

```cpp
147742  void func_0032DF98(ppu_context* ctx) {
...
147747          ctx->gpr[0] = vm_read32(ctx->gpr[5] + 0x0);     // lê *(r5+0)
...
147750          { ...cmp cr0 = (a=(int32)gpr0) vs 0... }        // cmpwi cr0, r0, 0
...
147772          vm_write32(ctx->gpr[1] + 0x1C8, ctx->gpr[8]);
147773          if ((!((ctx->cr >> 0) & 2))) { g_trampoline_fn = (void(*)(void*))func_0032E200; return; }
147774          { ...cmp cr(campo 4) = (a=(int32)gpr8) vs 0... }
147775          if ((!((ctx->cr >> 4) & 2))) { g_trampoline_fn = (void(*)(void*))func_0032E718; return; }
147776          ... (continua para um 3º caminho, "default", dentro do próprio func_0032DF98)
```

`cr0.EQ` (bit testado por `>>0 & 2`) vem do `cmpwi` sobre o valor lido em `*(r5+0)`.
`!EQ` = "valor != 0". **O branch para `func_0032E200` é o caminho de "há trabalho"
(pointer/contagem não-nulo) — é a via de "sucesso" do walk, não a de erro.** Seria
tomado se, no momento da chamada, a palavra de 32 bits no endereço apontado por `r5`
fosse não-zero.

Tracei `r5` para trás, hop a hop, por atribuição de registo (não há nenhuma leitura de
stream/WAD no percurso — só cópias de registo):

1. **`func_00468C3C`** (`ppu_recomp_001.cpp:438718`): `ctx->gpr[30] = (int64_t)(int32_t)(0);`
   — `li r30, 0`, **constante literal**, não depende de nenhum argumento de entrada.
   Confirmei por leitura integral de `:438693-438754` que `gpr30` não é reescrito em
   nenhum ponto entre `:438718` e `:438752` (as duas chamadas no meio, `func_00263B70` e
   o vcall dentro de `func_00468E3C`, não tocam `r30` — não-volátil por ABI PPC).
2. `:438750` `ctx->gpr[6] = (ctx->gpr[1] + 0x7C)` — `r6` = endereço de uma variável local
   na stack de `func_00468C3C` (`sp+0x7C`).
3. `:438752` `vm_write32(ctx->gpr[1] + 0x7C, ctx->gpr[30])` — escreve o `0` de `gpr30`
   NESSA mesma variável local, imediatamente antes da chamada.
4. `:438754` `func_00330D54(ctx)` — chamado com `r6` = esse endereço (aponta para `0`).
5. Dentro de `func_00330D54` (`:149656`): `ctx->gpr[28] = ctx->gpr[6]` — guarda o
   ponteiro recebido (não desreferencia).
6. `:149670` `ctx->gpr[5] = zext(ctx->gpr[28])` — reencaminha o MESMO ponteiro como `r5`
   para a chamada seguinte.
7. `:149679` `func_0032DF98(ctx)` — chamado com `r5` = o mesmo endereço.
8. Dentro de `func_0032DF98` (`:147747`) é a PRIMEIRA vez que esse ponteiro é
   desreferenciado — lê `0`.

**Conclusão verificada linha-a-linha: o valor testado em `:147773` não é derivado de
nenhum estado de runtime (WAD, stream, heap) — é o literal `0` escrito por
`func_00468C3C:438718`, encaminhado por ponteiro sem alteração através de 2 níveis de
chamada.** Isto vale para os 3 caminhos estaticamente alcançáveis até `func_00330D54`
(direto via `:438754`, ou via `func_00468E3C`→`func_00468CEC`/`func_00468D04`, que
herdam o mesmo `gpr30=0` por tail-call — confirmei que nenhuma das duas variantes
reescreve `r30` antes do respetivo `vm_write32(sp+0x7C, gpr30)`).

Nota lateral (não é o alvo desta task, mas registo por completude): o 2º gate
(`:147774-775`, testa `gpr8`, rota para `func_0032E718`) também traceia, nos 4 sites de
chamada concretos identificados para `func_00468C3C` (ver abaixo), a um `ctx->gpr[6]=0`
literal escrito 2 linhas antes de cada chamada (`:135100`, `:135192`, `:142732`,
`:142850`). Ou seja, nesses 4 sites, **nenhum dos dois gates dispara** — a execução cai
no 3º caminho "default" dentro do próprio `func_0032DF98` (`:147776+`), que não persegui
(seria descer, não subir — fora do escopo desta task). `func_0032E718` em si é uma
função real e não-trivial (`ppu_recomp_003.cpp:420007`, continua para
`func_0032E48C`/`func_0032E4B8`) — não é um stub morto, só não confirmei que corre
nestes caminhos.

## `func_00468C3C` é o único portão estático — e é caller-independente

Os 3 callers de `func_00330D54` (`:438754`, `:588238`, `:588307`) resolvem-se, subindo,
a um único gateway: `func_00468CEC`/`func_00468D04` só são alcançados a partir de
`func_00468E3C:438729`, que por sua vez só é alcançado a partir de `func_00468C3C`
(confirmado exaustivo por `rg`, cada um com exatamente 1 caller real). **Logo
`func_00468C3C` é o único ponto de entrada estático, direto, para toda esta subárvore.**

Isto importa porque o `gpr30=0` que fecha o gate de `:147773` é escrito **dentro** de
`func_00468C3C`, não herdado de nenhum argumento — portanto é **independente de quem
chama `func_00468C3C`**. Encontrei 4 callers diretos de `func_00468C3C`
(`func_003294F8`, `func_003296C0`, `func_00329490`, e `func_00329658` — este último
morto, ver acima), e `func_00329490` sozinho tem mais 6 callers (fan-in largo, família de
~10 "micro-construtores" quase idênticos: `func_00328678`, `func_0032862C`,
`func_0032854C`, +3 não abertos individualmente). **Não interessa qual destes — nem
nenhum ainda não encontrado — chama `func_00468C3C`: o gate em `:147773` fica fechado de
qualquer forma**, porque a fonte do valor não é um argumento, é uma constante local.

Isto refina a classe A1 (que dizia "o caller nunca corre"): mais precisamente, para esta
subárvore específica, **mesmo que `func_00468C3C` corra (e o fan-in largo sugere que
provavelmente corre com frequência, como utilitário genérico de construção de
componentes durante carregamento de cena — não confirmado in-boot nesta task, só por
RE), o branch para `func_0032E200` está estruturalmente fechado por construção, não por
falta de execução.**

## Cross-reference — runtime e docs (citado, não repetido)

- `ps3recomp/runtime/ppu/ppu_loader.cpp:2071-2218` (`ps3_force_post_wad_icgldr`,
  GATE-FORCE, só referência/diagnóstico — nunca aceite como fix): confirma que o objeto
  typemap **já existe** naturalmente por esta altura do boot — lê
  `component = vm_read32(base + 0x130000 + 0x4980)`, `typemap = *(component+0xDC)`,
  `vt = *(typemap+0)`, com fallback num componente conhecido vivo `0x4306ADF0u`
  (comentário: "ICG-CTOR instala componente..."). Ou seja, a CONSTRUÇÃO do componente
  com a vtable certa (`0x5130B8`) não é o que falta — o que falta é uma chamada
  POSTERIOR que processe/walke um stream contra esse typemap. Isto é consistente com o
  meu achado: a família `func_00468C3C`/`func_00330D54`/`func_0032DF98` tem cheiro de
  "processa/regista item(ns) contra um registry já existente", não de "constrói o
  registry" — e é exatamente essa chamada de processamento que nunca abre o gate certo.
  **`ppu_loader.cpp` não nomeia nenhuma das funções desta cadeia** (`grep` por
  `00330D54|00468C3C|00468CEC|00468D04|00468E3C|0032E200` em `docs/` e `runtime/` do
  `ps3recomp`: zero resultados) — não havia atalho a tomar aqui, o levantamento estático
  desta task é novo.
- `docs/gow2-recomp-notes.md:97` (parede [D]): "`ICGLdrShader` 0 callers, fonte vazia
  N=-1 18/18" — consistente (mais grosseiro) com o achado desta task; não acrescenta
  granularidade de função.
- `docs/gow2-asset-pipeline-map.md:448,461`: "SHGX_/ICGLdrShader: stream completo mas
  CRC Invalid segue" / "ICGLdrShader: still register-only (`[LDRSH]=0` post-WAD)" —
  idem, sem nomes de função novos.
- `.superpowers/sdd/progress-registry.md` e `reg-task-3-report.md`: âncoras e veredito
  A1 já confirmados e citados no topo desta nota; a única correção que faço é o Achado 0
  (1 caller real de `func_0032DF98`, não 2).

## Raiz estreitada (deliverable)

**`func_00468C3C` (`ppu_recomp_001.cpp:438693`, linha-chave `:438718`) é o nó mais alto
da cadeia onde o mecanismo fica provadamente — e estaticamente, não só empiricamente —
fechado.** Não é "a próxima função acima que nunca corre" no sentido literal (o fan-in
largo de `func_00329490`, 6 callers em 3 ficheiros, sugere fortemente que
`func_00468C3C` roda como utilitário genérico de construção); é antes: **a partir de
`func_00468C3C` inclusive e para baixo, o branch que chamaria `func_0032E200` está morto
por construção — o argumento que o alimenta é sempre a constante `0`, nunca um valor
derivado de WAD/stream/heap, em qualquer um dos 3 caminhos estáticos conhecidos.** Para o
walk alguma vez correr por esta família de chamadas, seria preciso `func_00468C3C` (ou
algum caller acima dela) passar um valor não-zero nesse slot — o que não acontece em
nenhum dos ~10+ sites de chamada mapeados. A alternativa é que o caminho real do jogo
para `func_0032E200`/`func_00171244` **não passe por `func_00468C3C` nenhuma**, mas sim
por uma chamada indireta/vtable ainda não localizada por este método (busca textual em
`.cpp` gerado não vê vtables como dados binários em rodata do EBOOT).

## Próximo experimento recomendado (não implementado — só a apontar)

**Prioridade 1 (offline, retoma o "opcional" do brief original, agora justificado como
decisivo):** `nm`/`rg` no rodata do `EBOOT.ELF` (ou no binário `boot_gow2` linkado) por
qualquer ocorrência do OPD `0x00522E70` (endereço de `func_0032E200`) fora do slot já
conhecido `vt 0x5130B8+8` — e por qualquer outra vtable/tabela que também aponte para lá.
Como a análise estática do grafo de chamadas DIRETAS já é exaustiva e prova que nenhuma
delas pode abrir o gate (Achados acima), só uma chamada indireta/vtable não capturada por
`rg` em `.cpp` (porque vive como dado binário, não como símbolo C++) pode revelar um
caminho real diferente. Esta é agora a experiência com maior poder discriminador.

**Prioridade 2 (in-boot, barato, gated, NÃO implementado por mim — fora do escopo
read-only desta task):** um probe de entry count em `func_00468C3C` (`PS3_TRACE_*`
análogo ao `[CMP-ENTER]` já existente) para confirmar empiricamente se esta subárvore
"morre por construção" chega sequer a ser visitada no boot natural. Não muda o
diagnóstico acima (o gate está fechado mesmo que corra), mas fecha a questão de "será
que vale a pena instrumentar mais fundo aqui" antes de investir na Prioridade 1.

## Âncoras verificadas nesta sessão (sem drift)

`func_00171244` `ppu_recomp_000.cpp:317308` (0 callers diretos, só self+tabela);
`func_0032E200` `ppu_recomp_003.cpp:419639` (1 caller real: `func_0032DF98:147773`);
`func_0032DF98` `ppu_recomp_001.cpp:147742` (1 caller real: `func_00330D54:149679`; o
"2º caller" `ppu_recomp_003.cpp:419636` é código morto — Achado 0);
`func_00330D54` `ppu_recomp_001.cpp:149630` (3 callers reais: `:438754`, `:588238`,
`:588307`); `func_00468C3C` `ppu_recomp_001.cpp:438693` (gate-source `:438718`, 4
callers reais, 1 deles — `func_00329658` — ele próprio morto); `func_00468E3C`
`ppu_recomp_001.cpp:438834`; `func_00468CEC` `ppu_recomp_002.cpp:588222`; `func_00468D04`
`ppu_recomp_002.cpp:588298`; `func_00329490` `ppu_recomp_001.cpp:142629` (6 callers,
fan-in largo). Tabela OPD/nome: `ppu_recomp_030.cpp` linhas `485045, 493106, 493142,
499995, 499997, 510398, 510399, 517111`.
