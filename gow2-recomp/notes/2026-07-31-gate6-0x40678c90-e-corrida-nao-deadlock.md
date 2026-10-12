# gate6: quem carrega ctr=0x40678C90 -- site estático achado, e é corrida (não deadlock)

**Data:** 2026-07-31 · Binário `boot_gow2.jtfix` (link contra
`recomp_macos_v2.jtfix`, 51 991 funções -- **nunca** `recomp_macos_v2` de
produção, que não tem `func_000B9354`). Recipe: `env_gow2.sh` +
`PS3_NO_RSX=1 PS3_AUTO_LOAD_RUN=1 PS3_PERF_FSM=1
PS3_VDEC_FORCE_SEQDONE_MS=1500`, timeout 70-90s, kill sempre por PID
(nunca `pkill -f boot_gow2`).

## Ferramenta nova (ps3recomp, commit `f4cd42d`)

O `[ICALL-BAD]` existente satura aos 12 primeiros hits do RUN INTEIRO e
nunca reaparece dentro de um streak preso -- exatamente onde eu precisava de
olhar. Adicionei ao `ps3_indirect_call` (`runtime/ppu/ppu_loader.cpp`), gated
e OFF por default:

- `PS3_TRACE_ICALL_TARGET=0xHEX` -- loga `ctr/lr/r2/r3/r9/r10/r11/r12` a cada
  hit de UM alvo específico (até 3x com dump de memória, depois heartbeat a
  cada 10000 e aviso quando o streak termina).
- `PS3_STUCK_ICALL_LIMIT` -- sobrepõe o limiar de abort do stuck-loop-guard
  (default inalterado: 2000). Só para discriminar corrida-lenta de deadlock
  real; não é fix.

## O site estático (MEDIDO, LR estável em toda corrida que bate lá)

`lr=0x002B2DE0` é o retorno da `bl func_002B2660` dentro de `func_002B2DD0`
(`recomp_macos_v2.jtfix/ppu_recomp_001.cpp:36390`). `func_002B2DD0` é chamada
em loop por `func_002B2E04` (mesmo ficheiro, ~linha 36402):

```c
// func_002B2E04 (paráfrase do lift)
flag_ea = *(TOC - 0x1488);
while (*flag_ea != 0) {
    func_002B2DD0(ctx);   // chama func_002B2660 -> func_000B951C -> func_002B7188
}
```

Isto é um **poll legítimo do jogo original** ("espera até a flag cair"), não
um bug introduzido pelo lift. `func_002B2DD0` chama sempre `func_002B2660`
primeiro, sem `bl` próprio antes do `bctr` interno -- por isso o LR que
aparece no `[ICALL-BAD]`/`[ICALL-TARGET]` continua a ser o do CALLER
(`0x002B2DE0`), não o site exacto -- mas isso já bastou para achar
`func_002B2660` por grep.

Dentro de `func_002B2660` (linhas 35841-35849), o cálculo do `ctr` é:

```c
obj      = *(TOC - 0x14A0);            // "current X" -- singleton global
slot_ea  = *(obj + 0x14);              // = 0x400C3D48 (fixo, medido)
sub      = *(slot_ea + 0x0);           // ESPERADO: ponteiro válido
vt       = *(sub + 0xC);
ctx->ctr = *(vt + 0x0);
ctx->gpr[2] = *(vt + 0x4);             // TOC do alvo
ps3_indirect_call(ctx);
```

Não há guarda de validade aqui (ao contrário do `[FACTORY] REPAIR` do B71) --
é lift "cru".

## A cadeia medida (3 corridas, valores IDÊNTICOS byte a byte)

```
[ICALL-TARGET] ctr=0x40678C90 lr=0x002B2DE0 r2=0x400C5048 r3=0x400C3D48
               r9=0x00000000 r10=0x400147E8 r11=0x400C3D48 r12=0x0A000018
[ICALL-TARGET]   r9(obj)=0x00000000 r10(slot)=0x400147E8
[ICALL-TARGET]   slot-0x10..+0x1C: 40672370 400C5048 00000000 00000000
                                    40678C90 400C5048 00000000 00000000
                                    40014808 00000000 00000000 00000000
```

`sub` (= `r9` = `*(0x400C3D48)`) é **NULL**. `vt` (= `r10`) sai de
`*(NULL + 0xC)` = `*(0xC)` -- uma leitura de memória guest baixa mas
COMMITTED (`[vm] committed range #1: 0x00000000..0x51000000`, não é OOB),
que por coincidência devolve `0x400147E8`. Esse endereço, por sua vez, TEM
conteúdo real e estruturado -- um array de stride 16 bytes onde o campo [0]
de cada entrada é um ponteiro da MESMA família de heap que os nós da lista
de "product" do TYPE15 (`0x4067xxxx`: `40672370`, `40678C90`, ...) e o campo
[4] repete sempre `0x400C5048` (o próprio TOC). Isto não é ruído -- é uma
tabela real, só que **não é a tabela de callbacks que `func_002B2660`
espera**: é (ou aliasa) o pool de nós da lista de produtos do B71/TYPE15.

**Correlação temporal (não still prova causalidade):** nas duas corridas
onde capturei o log completo, a linha `[POSTINTRO] B71 skip icallB (null
product)` aparece poucas linhas antes deste hit -- essa é a skip do
registo/attach que, por hipótese, seria quem escreveria um ponteiro real em
`*(0x400C3D48)`. **Não tracei quem escreve nesse endereço** -- é o próximo
passo, não uma conclusão fechada.

## O achado que muda o diagnóstico: NÃO é deadlock

Rodei 4 corridas com `PS3_TRACE_ICALL_TARGET=0x40678C90` e limiar default
(2000): 2 bateram FATAL (streak de 2000 hits idênticos), 1 completou
`thr_auto_load() end` **com os MESMOS 3 hits amostrados** (byte a byte iguais
aos das corridas que morreram), 1 nem chegou lá (parede de intro, sem
relação).

Depois rodei 11 corridas com `PS3_STUCK_ICALL_LIMIT=2000000` (1000x o
default): **0/11 bateram FATAL**, `thr_auto_load() end` sempre que chegaram
ao B71/AUTO_LOAD (algumas nem entraram no ramo mau -- a corrida decidiu-se
antes de `func_002B2660` correr pela primeira vez). Total: **11 corridas
sem deadlock real observado, contra 2/2 FATAL confirmadas só com o limiar
apertado (2000)**.

**Conclusão honesta:** `ctr=0x40678C90` por si só não é fatal -- o
`ps3_indirect_call` já limpa `gpr[3]=0` e devolve, e o poll de
`func_002B2E04` simplesmente tenta outra vez. O que decide sucesso/fracasso
é se **outra coisa** (thread concorrente -- FIOS mediathread, SPU job, ou o
próprio registo B71 atrasado) escreve a flag de saída em `*(TOC-0x1488)`
(ou popula `*(0x400C3D48)`, parando o ramo mau) **antes** do nosso PRÓPRIO
guard de segurança (`streak==2000`, diagnóstico nosso, não do jogo) abortar
o processo. É uma corrida de inicialização real, mas o `FATAL: stuck
calling` de 4/6 é em parte um **falso positivo do nosso limiar diagnóstico**
sendo baixo demais para essa janela específica -- não necessariamente um
deadlock inerente ao jogo.

## O que NÃO fiz (fica para a próxima)

- Não tracei quem escreve (ou devia escrever) em `0x400C3D48` -- falta
  `PS3_WATCH_W32=0x400C3D48` num run e correlacionar com `B71 icallA`/
  `FACTORY REPAIR`.
- Não confirmei se subir `PS3_STUCK_ICALL_LIMIT` em permanente (ou trocar
  por um guard "está a fazer progresso noutro thread?") é o fix certo --
  é só o experimento que separou corrida de deadlock. Subir o limiar sem
  entender a flag de saída seria mascarar, não corrigir (proibido pelo
  CLAUDE.md).
- Não medi ainda `cellPadGetData` nem `st620` pós-fix -- o critério do menu
  continua em aberto mesmo com `thr_auto_load` mais estável.

## Números do gate (honesto, nem todas as amostras no mesmo binário/limiar)

| Config | Corridas | `thr_auto_load end` | FATAL 0x40678C90 |
|---|---|---|---|
| `PS3_STUCK_ICALL_LIMIT` default (2000) + probe | 4 | 1 | 2 (1 nem chegou lá) |
| `PS3_STUCK_ICALL_LIMIT=2000000` + probe | 11 | 10 | 0 (1 timeout de intro, sem relação) |
