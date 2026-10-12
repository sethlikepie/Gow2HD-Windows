# Discriminador Boot A vs Boot B — Default.ps3fx combination "zerada" (2026-07-21)

Plano: ../ps3recomp/docs/superpowers/plans/2026-07-21-shader-combination-zeroed.md
Binário: boot_gow2 @ b51013e + sonda PS3_TRACE_COMBOPROP (101412c). Medido in-boot, PID-kill, 0 órfãos.

## Tabela medida

| Sinal | Boot A (WAD-skip) | Boot B (FORCE=3000) |
|-------|-------------------|---------------------|
| recipe | EOS+auto, sem FORCE | EOS+auto, FORCE_SEQDONE_MS=3000 |
| `Invalid shader combination` | 23367 | 24822 (±10%, igual) |
| `[COMBOPROP]` props | mat=0x0FEFFAF8 fmt=0x4C61B0 args=(0,1,0,0) | **idênticos** |
| `open R_PermA` / `R_LglScA` | 0 / 0 | 0 / 0 |
| `FORCE SEQDONE` | 0 | 0 (filme auto-close antes) |
| `[cellVdec] Open` | 1 | 1 |

Shaders que falham (Boot B): **49612× Default.ps3fx**, 24× FlashUI.ps3fx.
Mensagem explícita do guest: `Invalid shader combination: .../FlashUI.ps3fx ([Unknown combination string, disable USE_PRECALCULATED_SHADER_CRCS in CGOWShader.cpp])`.

## VEREDITO

- **H0 (ptr format zero) REFUTADA**: `fmt=0x004C61B0` (rodata correta), guard `[0x53D6AC]` nunca disparou.
- **H3 (resolve/leitura errada) REFUTADA neste sítio**: props do material `(0,1,0,0)` batem dígito-a-dígito com a string impressa — leitura fiel, sem corrupção.
- **H1 (WAD-skip) DESFAVORECIDA**: combination IDÊNTICA em A e B; Boot B tentou o caminho WAD e a taxa/props não mudaram. (Nota: R_PermA não abriu em B — o filme auto-completa antes do FORCE = parede operacional H4 separada, NÃO perseguida por escolha do user. Mas a evidência de combination-igual + o prior Windows (shader desacoplado do WAD; SHADERSRC N=0 mesmo COM R_PermA) tornam H1 improvável independentemente.)
- **H2 (registry/CRC precalc não populado) CONFIRMADA**: o jogo constrói combinations VÁLIDAS a partir de material residente, mas o lookup a jusante (`func_001655F0`, USE_PRECALCULATED_SHADER_CRCS) não tem entrada → "Unknown combination string". A tabela de CRCs precalculados / registry de variantes de shader está VAZIA no port.

## ROTA (próximo trabalho)

Popular o **registry de variantes de shader / tabela de CRC precalculada** pelo caminho NATURAL
(ICGLdrShader / SHADERSRC / nested walk do typemap = "Meta corrente" do ps3recomp). **NÃO** é
"só carregar o WAD de novo" (Windows mostra H2 mesmo com R_PermA full). **NÃO** CRC bypass /
`disable USE_PRECALCULATED_SHADER_CRCS` como fix (seria forja / mascarar o guard).
Âncoras do registry (plano §Guest-população): `func_0032109C` (ICGLdrShader, tag 0x283F6879)
`ppu_recomp_001.cpp:136228`; `func_003CC208` (SHADERSRC defs) `:291118`; `func_00171244` (typemap walk).
