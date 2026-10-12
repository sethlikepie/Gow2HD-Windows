# Boot N1 discriminador natural — nested walk typemap (func_00171244) (2026-07-21)

Plano: `../ps3recomp/docs/superpowers/plans/2026-07-21-shader-registry-typemap-walk.md` (Task 3)
Binário: `boot_gow2` rebuilt sobre gow2-recomp `b99c8e2` + patches desta sessão (fix de ordem em
`patch_tymap_probes2.py` que desbloqueia o entry `[TYMAP-171]`, novo
`patch_ldrsh_shadersrc_entry.py` para `[LDRSH]`/`[SHADERSRC]`) — ver commit(s) desta sessão no
`git log` de `gow2-recomp` logo a seguir a `b99c8e2`. Medido in-boot, PID-kill (TERM→-9), 0
órfãos nas 2 corridas. **Sem `PS3_GATE_FORCE`** em nenhuma corrida — `[GATE-FORCE]=0`
confirmado nas duas.

## Corridas

- **N1** (recipe exacto do brief): `PS3_MOVIE_EOS=1 PS3_MOVIE_DONE_MS=auto PS3_VDEC_ASYNC=1` +
  `PS3_TRACE_TYMAP=1 PS3_TRACE_LDRSH=1 PS3_TRACE_SHADERSRC=1 PS3_TRACE_SHREG=1`, 85s.
- **N1b** (suplementar, mesmo recipe, 150s): só para verificar se dar mais tempo ao boot
  (R_PermA nunca abriu nem em 85s nem em 150s — ver nota) mudava o quadro. Não mudou.

## Tabela medida

| Sinal | N1 (85s) | N1b (150s) | Interpretação |
|-------|----------|------------|---------------|
| `[CMP-ENTER]` (entry func_0032E200) | 0 | 0 | o único caller estático candidato do walk **nunca corre** |
| `[CMP-VCALL]` opd=0x00522E70/code=0x00171244 | 0 | 0 | consequência directa do acima (0 chamadas dentro de uma função que nunca entra) |
| `[TYMAP-171]` (entry func_00171244) | 0 | 0 | o walk **nunca corre** — confirmado ao nível do próprio corpo da função (independente de por onde seria chamado) |
| `[TYMAP-VT]` type=0xF85F9B1E | 0 | 0 | está dentro do walk; não dispara porque o walk não corre |
| `[TYMAP-LK]` hit F85F | 0 | 0 | idem (dentro de func_0018E814, só alcançável via o walk) |
| `[LDRSH]` (entry func_0032109C, corpo ICGLdrShader) | 0 | 0 | o corpo do ICGLdrShader **nunca corre** |
| `[OPD-ICG]` (opd=522E70 ou code∈{171244,0032109C} via `ps3_call_opd`, sonda incondicional do runtime) | 0 | 0 | confirma, por um mecanismo INDEPENDENTE (a nível de resolução de OPD no runtime, não no lift), que nenhuma chamada indirecta alcança o walk ou o ICGLdr em todo o boot |
| literais `F85F9B1E` / `522E70` / `00171244` / `0032109C` em qualquer forma no log | 0/0/0/0 | 0/0/0/0 | não há NENHUM sinal, por nenhuma via instrumentada, de que o walk ou o tipo alvo apareçam |
| `[SHADERSRC]` N= (func_003CC208, defs residentes) | 18× todas **N=0** (obj 0x4306B664..0x4306D664) | 18× idêntico | um subsistema DIFERENTE corre (defs residentes na stack, não-WAD) mas sem records — não é o mesmo caminho do walk; consistente com achado histórico Windows ("18 streams, primeiro u32 sempre 0") |
| `[SHREG]`/`[TYMAP-REG]` | 0 | 0 | probe órfão (cauda de `patch_tymap_18e814.py`, needle exige bloco pré-existente que não existe em lift limpo — falha DOCUMENTADA e aceite em `apply_all_patches.sh`); não instalado nesta task por não ser necessário para a árvore de decisão A/B/C |
| `R_PermA` open/bytes | 0 (nunca abriu) | 0 (nunca abriu) | contexto WAD — **não decisivo sozinho** (o brief já antecipa isto); mesmo padrão já visto em `notes/2026-07-21-combo-discriminate.md` Boot B |
| `Invalid shader combination` | 23113 | 25841 | sintoma conhecido, confirma pipeline de shader activo e a bater na parede durante toda a janela medida |
| `Default.ps3fx` | 46210 | 51666 | idem |
| crash/access violation | 0 | 0 | boot limpo, sem crash — o "nunca corre" é arquitectural, não um crash a meio do caminho |

## VEREDITO: classe **A1**

`TYMAP-171 == 0` **e** `CMP-ENTER == 0` em 2 corridas independentes (85s e 150s, ~236k
linhas de log combinadas, 48954 falhas de combination combinadas) → **A1: o caller nunca
corre**. O único candidato identificado por RE estática para invocar o walk (`func_0032E200`,
que faz vcall a `this+0x8` = OPD `0x522E70` = `func_00171244`, ver `patch_32e200_opd.py`)
nunca é sequer *entrado* no boot natural — não é um caso de vcall errado, vtable errada, ou
stream vazio (isso seriam A2/A3/B1-B3/C1-C2); é um degrau anterior: **nada no grafo de
chamadas exercitado liga até `func_0032E200`**. A sonda `[OPD-ICG]` (incondicional, vive no
runtime `ps3_call_opd`, portanto independente do caminho estático instrumentado) corrobora a
zero por uma via completamente distinta. GATE-FORCE=0 em ambas as corridas confirma que N1
não está contaminado por nenhum caminho forçado.

**Não é a parede [D] vencida.** Isto é um diagnóstico de PARA ONDE olhar a seguir, não um fix.

## ROTA (próximo fix fiel candidato — uma frase)

Como `func_0032E200` é o único candidato estático conhecido e nunca corre, o próximo passo
fiel é localizar por RE estática **quem deveria chamá-lo** — auditar referências/instanciação
de objectos com vtable `0x5130B8` no grafo de chamadas realmente exercitado (xrefs ao
construtor do componente, não sondar `0x522E70` directamente nem forçar via GATE-FORCE).

## Âncoras usadas (confirmadas sem drift nesta sessão)

`func_00171244` `ppu_recomp_000.cpp:317308`; `func_001856A8` `:337827`; `func_0032109C`
`ppu_recomp_001.cpp:136228` (agora `:136229` após probe `[LDRSH]` inserido antes do corpo,
a própria linha `void func_0032109C` continua em `:136228`); `func_003CC208` `:291118`;
`func_0032E200` `ppu_recomp_003.cpp:419639`; vtable `0x5130B8+8` → OPD `0x522E70`; tipo
`0xF85F9B1E` → OPD `0x534838` (registado, confirmado em sessões anteriores).
