# Baseline do wall no macOS/arm64 — reproducao do M2

Data: 2026-07-20 · `boot_gow2` arm64 nativo · Apple M5 · `PS3_NO_RSX=1`
Ferramenta: `./attach_mem_mac.sh` (porte do `attach_mem.sh` de gdb/Windows para lldb).

## Leitura da memoria do guest aos 12 s

| Endereco | Valor | Significado |
|---|---|---|
| `[0x541AD0]` | `0x0086E118` | gpr28 — ponteiro **valido** para a struct da flag |
| `[0x541AD4]` | `0x005728EC` | gpr27 — base do dispatch vtable, **valido** |
| `*0x86E118 +0` | `0x00000000` | **flag de saida presa em 0** |
| `*0x86E118 +4` | `0x00000000` | estrutura **zerada**, nao lixo |
| `*0x86E118 +8` | `0x00000000` | idem |
| `*0x881970 +0` | `0x58585858` | alloc-ctrl com o **fill `'XXXX'` nao sobrescrito** |
| `*0x6FF484` | `0x03001230` | byte alto `0x03` — o da hipotese M1 ja refutada |

**Bate exactamente com o `SPURS_M2_FINDINGS.md` do Windows.** Mac e Windows param
no mesmo sitio, pelo mesmo motivo. O port nao introduziu divergencia.

## Estado com o kernel SPURS HLE ligado

O kernel (Phase 2) e inicializado pelo guest — `[spurs kernel] initialize(spurs=0x0,
nSpus=2, flags=0x0)` x3 — mas **nenhum workload e registado no boot real**, logo
nada corre e nada escreve `0x86E118`. Isso e coerente, nao regressao.

## Dado novo: qual import decide a rota de init

O `GOW2_BOOT_STATE.md` diz que `func_00315EAC` escolhe o caminho de init pelo
retorno de um import no slot `[0x51940C]`. Resolvido em runtime:

- `[0x51940C]` = `0x0A000240` (OPD sintetico em scratch)
- `*0x0A000240` = TAG `0xF0000120` → indice de import **72**
- Pela ordem dos modulos no log (`sys_io` 0-4, `sys_fs` 5-24, `cellSpurs` 25-58,
  `cellSync` 59, `cellSysutil` 60-69, **`cellSysmodule` 70-72**), o indice 72 e o
  **terceiro import de `cellSysmodule`**.

Relacionado: o boot loga `[cellSysmodule] LoadModule(id=0x003E 'UNKNOWN')` seguido de
`UnloadModule` — o nosso `cellSysmodule` nao conhece o modulo `0x3E`.

**Isto NAO e o blocker.** O proprio `GOW2_BOOT_STATE.md` ja mediu que as duas rotas
(`retorno valido` e `retorno 0/erro`) convergem em `func_0031415C`, que corre em
ambos os casos e mesmo assim deixa `0x881970` por preencher. Corrigir o retorno do
import muda a rota, nao o resultado. Fica registado para nao ser re-investigado.
