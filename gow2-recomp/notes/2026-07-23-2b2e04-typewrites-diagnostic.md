# 002B2E04 map corrigido + this+0x4 writer LOCALIZADO (CB56C auto-destrói)

## Goal
Executar o plano de 2ª consulta ao codex: (1) classificar o contador de
`func_002B2E04` (`PS3_TRACE_2B2E04`, probe já existente) e (2) achar quem
escreve `this+0x4`/`this+0x54` dos dois objectos `CC9D0` (`0x4066D798`,
`0x4066D804`) com um probe novo. Contexto: `2026-07-22-type15-cb56c-attach-
diagnostic.md` (refutou item 4 do wall original), `2026-07-22-wall-d-schedule-
map.md`, `2026-07-23-postthr-cc9d0-type15-wall.md`, `2026-07-23-menu-fast-
attempt.md`.

**Nota importante:** `func_000CB56C` **mudou** desde o diagnóstico de ontem —
outra sessão (concorrente, ver aviso da ronda anterior) já aplicou um fix real
para o hang de `func_002A4FE4` (a lista `product+0x70` NULL-terminated tratada
como circular, que eu tinha localizado): agora existe
`ps3_type15_product_list_reset()` que sanitiza a lista antes do attach, e o
`skip_icall2` deixou de ser default — hoje `PS3_TYPE15_SKIP_ATTACH=1` é que é
opt-in para o comportamento antigo. Por default, `icall2` + `2A4FE4` correm a
sério agora. Isto invalida parcialmente a minha conclusão de ontem ("forçar
icall2+2A4FE4 não muda nada") — o código testado ontem já não é o default de
hoje.

## Passo 1 — `PS3_TRACE_2B2E04` (sem rebuild, probe já existente)

Recipe: `env_gow2.sh` + `PS3_AUTO_LOAD_RUN=1 PS3_NO_RSX=1 PS3_TRACE_POSTINTRO=1
PS3_B71_FULL_393E0=1 PS3_TRACE_2B2E04=1`, 40s, kill por PID. **Zero linhas
`[2B2E04]`** — o probe nunca disparou, apesar de `func_002B7188` (o "dead end"
que o codex já tinha isolado) ter sido chamado **613.824×** no mesmo boot
(`grep -c "POSTINTRO.*func_002B7188"`).

**Mapa de chamadas corrigido** (o codex só tinha visto uma das duas):

```
func_002B2E74  (post-B71 chain root; chama func_000B71B8=B71 directamente)
  ├─ ...
  ├─ func_000B71B8   (B71)
  ├─ func_0023B654
  └─ func_002B7188   <- chamada DIRECTA, ppu_recomp_001.cpp:22729

func_002B2E04  (while *(TOC-0x1488) != 0)
  └─ func_002B2DD0
      ├─ func_002B2660
      ├─ func_000B951C
      └─ func_002B7188   <- chamada indirecta via 002B2DD0, ppu_recomp_001.cpp:22643
```

`func_002B7188` tem exactamente estes 2 chamadores estáticos. A contagem
observada (613k/40s ≈ 15kHz) só bate com a via `func_002B2E74` — se fosse via
`002B2E04`, o probe teria disparado (está dentro do corpo do loop). **Veredito:
o loop "enquanto contador>0" do `002B2E04` não é o culpado — ou nunca corre
neste boot, ou corre com o contador já ≤0 sempre (skip do corpo). O caminho
quente real é `func_002B2E74` a ser chamado ele próprio ~15k×/s por um
chamador ainda não identificado** (não investigado nesta sessão — 0 chamadores
estáticos de `func_002B2E74(ctx)` encontrados, deve ser indirecto/vtable como
o resto deste cluster). Nenhum dos 3 buckets do codex ("constante", "oscila",
"chega a zero sem sair") se aplica — a pergunta estava mal dirigida ao loop
errado.

## Passo 2 — `PS3_TRACE_TYPE15_WRITES` (novo probe, rebuild `ppu_loader.o` só)

Instrumentado `vm_write8`/`vm_write32` reais (`ps3recomp/runtime/ppu/
ppu_loader.cpp` — **não** o `ppu_memory.h` `static inline`, que é código morto
para o lift: os `ppu_recomp_XXX.cpp` só declaram `extern` e linkam contra as
implementações de `ppu_loader.cpp`; confirmado via `nm`/grep antes de editar o
ficheiro errado, revertido). Filtro: `0x4066D79C`/`0x4066D808` (this+0x4, w32)
e `0x4066D7EC`/`0x4066D858` (this+0x54, w8). Mesmo recipe do passo 1 +
`PS3_TRACE_TYPE15_WRITES=1`, 2 runs (1× kill-after-40s, 1× `atos -p <pid>`
enquanto o processo ainda vivo — endereços de retorno são do host, ASLR muda
por run, por isso resolvido ao vivo em vez de comparar entre runs).

```
[TYPE15-W32] [0x4066D79C]=0x40004020 ra0=func_00263178+2528 ra1=func_002A6608+632
[TYPE15-W8]  [0x4066D7EC]=0x00       ra0=func_000CB56C+1116 ra1=func_000CBB2C+512
[TYPE15-W32] [0x4066D79C]=0x00000000 ra0=func_000CB56C+1260 ra1=func_000CBB2C+512
[TYPE15-W8]  [0x4066D858]=0x00       ra0=func_000CB56C+1116 ra1=func_000CBB2C+512
[TYPE15-W32] [0x4066D808]=0x00000000 ra0=func_000CB56C+1260 ra1=func_000CBB2C+512
```
(`func_00263178` = função de ALLOC do heap, região já conhecida do probe
`PS3_TRACE_POOLCNT`; `func_002A6608` é o seu chamador aqui, não investigado
mais fundo.)

**Discriminador: writer observado — e depois um segundo writer apaga-o
(hipótese "produto substituído antes de CC9D0" confirmada, não "campo nunca
escrito").** Para `0x4066D798`: `func_002A6608` (via alloc `00263178`) escreve
`this+0x4=0x40004020` — um ponteiro plausível (`0x40004020` está na gama
heap/guest normal). **Depois**, `func_000CB56C` (chamado a partir de
`func_000CBB2C`) **apaga esse valor de volta para 0**, e também zera
`this+0x54`. Para `0x4066D804` só se observou o zero do CB56C (o write "real"
desse objecto não caiu na janela dos 40s, ou nunca acontece para ele).

### Localizado no source: CB56C tem um prólogo de reset INCONDICIONAL

Lido `func_000CB56C` completo (`ppu_recomp_000.cpp:161493-161743`). Logo a
seguir à entrada (linhas 161504-161540), **antes** de qualquer lookup de
factory ou detecção de "shell reuse"), corre incondicionalmente:

```c
ctx->gpr[8] = ctx->gpr[3] | ctx->gpr[3];      /* gpr8 = obj (arg original) */
...
ctx->gpr[10] = (int64_t)(int32_t)(0);
...
vm_write8(ctx->gpr[31] + 0x54, ctx->gpr[11]); /* +0x54 = 0   (linha 161536) */
...
ctx->gpr[9] = ctx->gpr[8] | ctx->gpr[8];      /* gpr9 = obj outra vez */
...
vm_write32(ctx->gpr[9] + 0x4, ctx->gpr[10]);  /* +0x4  = 0   (linha 161540) */
```

Isto corre **sempre**, para qualquer objecto que entre em `CB56C` — antes do
`icall1` (factory lookup), antes da decisão `ty15_reused`, antes do
`icall2`/`2A4FE4` (attach). **Nada mais tarde em `CB56C` volta a escrever
`+0x4`** (só `+0x8=product` é reposto, nas duas branches finais, linhas
161719/161729). Ou seja: `CB56C` assume que está sempre a inicializar um
objecto do zero — correcto para a 1ª construção real, mas **destrutivo se
`CB56C` for chamado de novo no MESMO objecto depois de `+0x4` já ter sido
preenchido por outra via** (aqui, `func_002A6608`).

`CB56C` e `CBB2C` (o seu único chamador estático) têm **0 chamadores estáticos
próprios** — tal como `func_000CCE34`/`func_000CD0F0` (os chamadores do
`CC9D0`) e `func_002B2E74`. Todo este cluster é despachado por vtable/icall, o
padrão dominante neste jogo. Consistente com um walker genérico de componentes
de cena a chamar, por tick, um slot "ensure-constructed" (→ `CBB2C`→`CB56C`) e
um slot "update" (→ `CCE34`/`CD0F0`→`CC9D0`) em cada objecto registado —
**se `CBB2C`/`CB56C` for chamado todos os ticks (não só uma vez), o reset
destrutivo de `+0x4`/`+0x54` explica por que nunca progridem: são reconstruídos
e imediatamente apagados, para sempre.** Não confirmado nesta sessão (ver
próximo passo).

## Verdict

**Causa mecânica de `f4=f54=0` eterno, agora provada, não só inferida:**
`func_000CB56C` zera incondicionalmente `this+0x4`/`this+0x54` no seu prólogo,
de cada vez que corre — e nada volta a preencher `this+0x4` depois disso
(quem o preenche, `func_002A6608`, só foi observado a correr UMA vez nesta
janela, presumivelmente como parte da construção real inicial, não de novo por
`CB56C`/`CBB2C`). Isto **não é** mais "campo nunca escrito" (framing de ontem)
— é "escrito uma vez certo, depois apagado por um reset incondicional que
corre de novo".

Isto não prova sozinho que a correcção seja em `CB56C` — pode ser
legitimamente correcto `CB56C` limpar tudo numa reconstrução completa **se**
`CBB2C`/`CB56C` só devessem correr uma vez por objecto e o bug real for outro
código a invocá-los repetidamente (todos os ticks) quando deviam correr só
uma vez. As duas hipóteses (A: `CB56C` deveria preservar `+0x4`/`+0x54` no
caminho de reuse; B: `CBB2C`/`CB56C` estão a ser invocados com uma frequência
errada) apontam para fixes diferentes — **não escolhida entre as duas nesta
sessão**, propositadamente (seria forçar um fix sem discriminar).

## Próximo experimento (não esta sessão)

1. Contador gated (`PS3_TRACE_CBB2C_FREQ` ou reusar `PS3_TRACE_FACTORY`) em
   `func_000CBB2C`/`func_000CB56C`, por objecto (`0x4066D798`/`0x4066D804`),
   com timestamp — confirmar se corre 1× (construção real) ou todos os ticks.
2. Se todos os ticks: localizar o vtable/icall que despacha `CBB2C` (grep de
   quem lê e chama o slot correspondente na tabela de vtable destes objectos,
   mesma técnica do `wall-d-schedule-map.md`) e decidir se o walker devia
   deixar de chamar o slot "construct" depois da 1ª vez (estado `f54` como
   guarda "already constructed"?), ou se `CB56C` devia checar `ty15_reused`
   ANTES do reset e preservar `+0x4`/`+0x54` nesse caso.
3. Só depois disso considerar tocar em `CB56C` — mudar o reset agora, sem
   saber qual das duas hipóteses é a real, arrisca mascarar o sintoma (regra
   do projecto: fix fiel ao comportamento original, não "consertar" a
   divergir).
4. Investigar separadamente quem chama `func_002B2E74` ~15k×/s (não achado
   nesta sessão) — provavelmente o main loop real, útil para perceber o
   orçamento de CPU disponível por tick e se há alguma pacing ausente
   (`PS3_NO_RSX=1` headless não tem vsync, pode estar a rodar sem throttle
   nenhum, o que por si só não é bug mas pode esconder um).

## Code sites
| Piece | Onde |
|-------|------|
| `PS3_TRACE_2B2E04` (pré-existente) | `recomp_macos_v2/ppu_recomp_001.cpp:22651-22690` (`func_002B2E04`) |
| Chamador real de `002B7188` (dominante) | `recomp_macos_v2/ppu_recomp_001.cpp:22704-22735` (`func_002B2E74`, chama B71+002B7188 direct) |
| Novo probe `PS3_TRACE_TYPE15_WRITES` | `ps3recomp/runtime/ppu/ppu_loader.cpp` (`vm_write8`/`vm_write32` reais — **não** `ppu_memory.h`, que é dead code para o lift) |
| Reset incondicional `+0x4`/`+0x54` | `recomp_macos_v2/ppu_recomp_000.cpp:161536` (+0x54), `:161540` (+0x4), dentro de `func_000CB56C:161493-161743` |
| Writer real de `+0x4` (uma vez, antes do reset) | `func_002A6608` → `func_00263178` (alloc), não localizados em detalhe nesta sessão |
| `CB56C` fix concorrente (2A4FE4 já resolvido) | `func_000CB56C:161670-161734` (comentário "2026-07-23 fix", `ps3_type15_product_list_reset`) |
