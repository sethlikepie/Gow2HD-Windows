# Parede de shaders: gated na progressão do 2º movie (2026-07-22)

## Medida in-boot (natural, FORCE seq-done 8s, ~50s, PS3_NO_RSX=1)
Log: `/tmp/shaderwall_h1.log` (8797 linhas). Probes já no binário
(A1CHAIN/TYMAP/LDRSH/SHADERSRC).

## Achados (ordem causal)

1. **Intro VENCIDO**: st620 `0→1→3→11` (sequência no log). Depois `11→0`
   (reset p/ próxima fase) e **fica preso em `0→0` (×5)**. O jogo passa o
   intro mas **estanca no movie/FSM SEGUINTE** (logo studio → título/menu).
   `overlay_done=1`, mas o st620 do 2º movie não arranca.

2. **SHADERSRC carrega como SOURCE**: `[SHADERSRC]` (func_003CC208) dispara
   **18×** durante R_LglScA/R_Perm, com contagens N=2,1,16,…,**776**,… — os
   shaders ESTÃO no heap como source (vector em `obj+0x4/0xC`).

3. **ICGLdrShader NÃO compila**: `[LDRSH]` (func_0032109C) = **0**. O corpo do
   ICGLdrShader tem 1 único entry (fallthrough de `func_0032108C`) e nunca
   corre. `[TYMAP-171]`/`[CMP-ENTER]` = 0. ⇒ source carregada, variantes
   nunca compiladas.

4. **R_Perm full OK**: bytes_read=20169344; WADLD processa todos os chunks
   (GFXX…WYPX). O catálogo de shaders + dados de cena estão carregados.

5. **Loop SPU steady-state**: pós-load, `[LFQ] push#` vai a 339000 (~6800/s),
   `[SPUJOB] returned cleanly` 1534× (~30/s = frame rate), ring com o
   componente ICG/typemap `4306ADF0`. Os jobs SPU correm, mas a FSM não
   avança à cena.

## Veredito (refina A1/H1/H2)

A parede de shaders **não é** um bug isolado do dispatcher. Cadeia causal:
intro vencido → FSM reseta → **preso em st620=0 no 2º movie** → nunca alcança
a fase de cena/menu → SHADERSRC fica como source (18×) mas ICGLdrShader nunca
compila → **sem draws reais no Metal (só resíduo do logo)**.

⇒ Os dois alvos ("walk correr" e "UI draws no Metal") estão **gated na mesma
coisa**: destravar o **2º movie/FSM** (st620=0 → progredir) para o jogo
chegar à cena/menu. É a mesma família do wall activo `4.EOS+`
(intro-vdec-open-force-wad), mas para o movie seguinte ao intro.

O dispatcher morto (`0032E200` `li r30,0`) continua morto por construção,
mas é **irrelevante enquanto o jogo não chega à fase que o exercitaria** —
o gargalo real subiu para a progressão do 2º movie.

## Próximo (discriminador barato)
- Ver se o 2º movie recebe `cellVdecOpen`/`StartSeq` (log: StartSeq=1,
  SEQDONE=2 no run — 1 só StartSeq ⇒ 2º movie pode não abrir vdec).
- Aplicar a lógica de arme-de-EOS-atrasado (do `4.EOS+`) ao 2º movie.

## CONFIRMADO: o 2º movie nunca abre vdec

No log inteiro (50s): **1 só `[cellVdec] StartSeq`** (handle=0, linha 2612) e
**1 só FORCE** (`FORCE SEQDONE watchdog after 8000 ms (skip intro)`, linha
2615) — ambos para o INTRO. Não há 2ª StartSeq. ⇒ o movie seguinte ao intro
**não chega ao estado 4 (cellVdecOpen)**, logo o vdec nunca abre, o movie não
toca, e a FSM (st620) fica presa em 0. Idêntico ao CRUX do `4.EOS+`
(o arme de EOS salta o estado 4), mas para o 2º movie — e o FORCE watchdog
só dispara 1× (intro), não re-arma.

**Próximo passo concreto (destrava walk + UI draws):** estender a lógica de
vdec-open (plano `4.EOS+`) aos movies APÓS o intro — re-armar o gate de
estado-4 / FORCE por-movie, não só uma vez. Com o 2º movie a progredir, o
jogo alcança a cena/menu → ICGLdrShader compila as 18 SHADERSRC já carregadas
→ walk/registry naturais → draws reais de UI no Metal.
