# Task 3.3 — ligacao das imagens SPU do GoW2 (FEITO)

Parqueado em 2026-07-20 de manha por incompatibilidade de ABI; **desparqueado e
aplicado no mesmo dia**, depois do merge do `origin/master`. O patch abaixo esta
agora no `build_macos.sh` (etapa `3b`).

## O que desbloqueou

Os tres bloqueios da versao parqueada cairam:

| Bloqueio | Estado |
|---|---|
| `g_spu_trampoline_fn` / `SPU_DRAIN` nao existiam nesta branch | resolvido pelo merge |
| ABI da task mudou (`r3` = `CellSpursTaskArgument` de 128 bits, nao o EA) | os testes e o kernel passaram a ser os do master; `test_adapter`/`test_workload` corrigidos (suite 10/10) |
| Sem isolamento de falha de job (`SEH` e so Windows) | o `spu_lifted_job.h` do master usa `setjmp`/`longjmp`, portavel |

**Ressalva que se mantem:** `setjmp`/`longjmp` apanha a saida do job, **nao** um
SIGBUS/SIGSEGV. No macOS um job que falta continua a levar o processo inteiro —
e por isso que spu1/2/3 ficam opt-in e so o spu0 entra por default.

## Resultado medido (2026-07-20, Apple M5)

Build: `3b. imagens SPU liftadas do GoW2 -> .o` produz **5 objectos** (spu0..spu3
+ o registador). Binario 112M -> 114M.

Simbolos no `boot_gow2` (`nm`):

```
T _gow2_register_spu_workloads
T _spu0_spu_recomp_register   ... spu1/2/3 idem
spu0_ 450   spu1_ 530   spu2_ 527   spu3_ 433   funcoes liftadas
```

Registo no dispatcher, agora observavel no log (o `spu_workload_register` passou
a logar — ver o commit no motor):

```
default:        [spu_workload] registered 'gow2_spu0' fp=0xDE6DC3A5EA2BE487 (1 no total)
PS3_SPU_ALL=1:  ... 'gow2_spu1' 0x2A5C4E67A14505B8 (2) ... 'gow2_spu2' (3) ... 'gow2_spu3' (4)
```

Processo **vivo** nas duas configuracoes; `smoke_boot_mac.sh` PASS em ambas.

## O que NAO foi exercitado (honesto)

**Nenhum job SPU foi despachado.** O boot regista `AddWorkload = 0` e
`CreateTask = 0`: o titulo trava no wall `func_002B3CB0` antes de submeter
qualquer workload, portanto as imagens estao ligadas e registadas mas nunca
correm. O Step 4 do plano ("se SIGBUS em job, validar a ABI") **nao se aplicou**
— nao houve job, logo nao ha prova de que a ABI esta certa em execucao real
neste titulo. A prova que existe e a da suite (`test_adapter`, `test_workload`,
`spurs_job_dma`, `taskset`), que e offline/unit, nao in-boot.

Desbloquear o dispatch depende da Task 3.2 (destravar o wall M2), nao de mais
wiring.

## O patch (ja aplicado ao `build_macos.sh`)

Entre a etapa 3 (tabela de NIDs) e a 4 (boot host):

```bash
echo "=== 3b. imagens SPU liftadas do GoW2 -> .o ==="
SPU_OBJS=()
for d in "$HERE"/spu_lifted/spu?_v2; do
    [ -f "$d/spu_recomp.c" ] || continue
    n=$(basename "$d")
    o="$LIFT/${n}_spu_recomp.o"
    if [ ! -f "$o" ] || [ "$d/spu_recomp.c" -nt "$o" ]; then
        clang -std=c11 -O1 -w -c -I "$d" -I "$PS3/runtime/spu" -I "$PS3/include" \
              "$d/spu_recomp.c" -o "$o"
    fi
    SPU_OBJS+=("$o")
done
if [ ${#SPU_OBJS[@]} -gt 0 ]; then
    clang -std=c11 -O1 -w -c -I "$PS3/runtime/spu" -I "$PS3/include" \
          "$HERE/recomp_mid_v2/gow2_spu_register.c" -o "$LIFT/gow2_spu_register.o"
    SPU_OBJS+=("$LIFT/gow2_spu_register.o")
fi
```

E no link, a seguir a `boot_macos.o`:

```bash
    ${SPU_OBJS[@]+"${SPU_OBJS[@]}"} \
```
