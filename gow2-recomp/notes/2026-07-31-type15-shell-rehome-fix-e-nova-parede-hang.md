# TYPE15: o "shell" nunca tinha sido pinado — corrigido, e a parede seguinte
# já não é NULL, é HANG na construção real do guest

**Data:** 2026-07-31 · **Medido 3/3 corridas** em `boot_gow2_type15_fix`
(clone de teste `recomp_macos_v2.type15fl_fix`, nunca `boot_gow2`/`recomp_macos_v2`
de produção). Recipe: `arm_menu_fast_recipe` (`lib_boot_chain_metrics.sh`) +
`PS3_TRACE_TYPE15_FL=1 PS3_TRACE_TYPE15_SHELL=1 PS3_TRACE_FACTORY=1`, timeout
90s, kill sempre por PID.

## Onde a nota anterior parou

`notes/2026-07-31-type15-fl-slot-medido-fix-e-nova-parede.md` fechou o fix do
REHOME da free-list (commit `5d5e5e0`): `head`/`mid` (o CONTROLO da free-list,
em `fl`/`fl+0x18`) ficam estáveis dentro da zona pinada 3/3. Mas a 2ª/3ª
chamada à fábrica continuavam a devolver `r3=NULL`, e ficou registada como
hipótese não verificada: "o slot (shell) tem uma flag de já-usado dentro do
próprio objecto" (H1) vs "o CB56C não devia despachar tipo 0x15 duas vezes"
(H2).

## A medição que distingue H1 de H2

Escrevi uma probe nova, dedicada (`recomp_mid_v2/patch_type15_shell_probe.py`,
gate `PS3_TRACE_TYPE15_SHELL=1`, read-only), que despeja 16 palavras do
"this" da SEGUNDA chamada interna de `func_0039E794` — a que despacha o
`vt[+0x14]` do objecto resolvido em `slot-4` (o "shell") — imediatamente
antes e depois de cada invocação. Aplicada só ao clone de teste
`recomp_macos_v2.type15fl_fix` (já com o fix do REHOME da free-list), nunca
em produção.

Resultado, 1ª vs 2ª chamada, MESMO endereço `this=0x40100840`:

```
pre#20/post#20 (1ª chamada, produto valido r3=0x42F85AE4):
  00516DD8 40150015 00000000 00000000 00000000 00000000 00000080 00000080
  FFFFFFFF 00000000 00000000 00000000 401002F0 000003E8 000003E8 80000011
  -- objecto vivo: vt=0x00516DD8, tag=0x40150015 (tipo 0x15).

pre#25/post#25 (2ª chamada, MESMO endereço, ~430 linhas de log depois,
r3=NULL):
  34343534 002F3A50 53325F34 34353661 002F3A50 53325F34 34353663 002F3A50
  53325F34 34353662 002F3A50 53325F34 34353762 002F3A50 53325F34 34353761
  -- ASCII puro: bytes 34 34 35 34='4454', 00 2F 3A 50 (NUL '/' ':' 'P'),
     53 32 5F 34='S2_4' repetido -- uma tabela de paths de assets tipo
     ".../S2_446a/S2_446c/S2_446b/..." escrita por cima do MESMO endereço.
```

**Resposta directa, sem inferência: nem H1 nem H2 como estavam formuladas.**
Não é uma flag interna consumida (H1) — é o objecto INTEIRO reescrito por
outra alocação do guest reusando o mesmo endereço. E não é o CB56C a
despachar tipo 0x15 indevidamente (H2) — a 2ª chamada é legítima, só que o
"shell" que ela deveria reusar já não existe: foi stream-stomped porque
**nunca tinha sido pinado**. O fix anterior (`5d5e5e0`) só protegeu o
CONTROLO da free-list (`fl`/`head`/`mid`); o PAYLOAD que o controlo aponta
(`*mid` = shell+4, endereço fora da janela `[fl, fl+0x200)` copiada por esse
fix) continuou a viver na arena antiga desprotegida — a mesma classe de
churn que já tinha corrompido o próprio controlo antes do primeiro fix.

## Fix aplicado (preserva o conteúdo capturado, não forja nada)

Depois de traduzir os auto-ponteiros do blob `[fl, fl+0x200)`
(`TYPE15-FL-REHOME-FIXUP`), se `*mid` apontar para fora da zona pinada, copia
o objecto real apontado (`mid_val - 4`, até 0x80 bytes) para o mesmo slot que
`ps3_type15_freelist_replenish` já usa para shells sintéticos
(`k_ty15_pin + 0x800`) e faz retarget de `*mid` para lá.

- **Fonte versionada:** `host_gow2_factory.cpp` (patch manual).
- **Fix reproduzível no lift:** `recomp_mid_v2/patch_type15_fl_shell_rehome.py`
  (idempotente, marcador `TYPE15-FL-SHELL-REHOME`, depende do
  `TYPE15-FL-REHOME-FIXUP` já aplicado).
- **Probe usada para medir:** `recomp_mid_v2/patch_type15_shell_probe.py`
  (marcador `TYPE15-SHELL-PROBE`, gate `PS3_TRACE_TYPE15_SHELL=1`, OFF por
  default, read-only).

Verificado 3/3 corridas: `[TYPE15] REHOME shell old=0x40100840
pin=0x47D00800 vt=0x00516DD8` dispara sempre; `slot` da free-list passa a
`0x47D00804` (dentro do pin) já na 1ª leitura; `pre#25` (this=0x47D00800)
mostra os MESMOS 16 words válidos da 1ª chamada — **a corrupção ASCII
desaparece por completo, 3/3**.

## Mas abre-se uma parede nova: HANG em vez de NULL

Com o shell preservado, a 2ª chamada de `func_0039E794` entra de facto no
método real do guest (`vt[+0x14]`) em vez de derivar um vtable de lixo ASCII.
`pre#25` dispara nas 3 corridas — **`post#25` nunca dispara em nenhuma**, nem
o `[FACTORY] 39E794 leave this=0x47D00000` da 2ª chamada. O processo continua
vivo (outras threads seguem a tiquetaquear `[SPUJOB] spu job returned
cleanly` e `[MOVIEFSM] st620 0 -> 0`), mas a chamada de construct nunca
retorna dentro da janela de 90s, 3/3.

```
                         log_lines  startseq  thr_end  r_perma  nopic  st620
sem shell-fix (NULL):    34564      2         0        1        4      11
com shell-fix (HANG):     4286      2         0        1        4      11
                          4282      2         0        1        4      11
                          4270      2         0        1        4      11
```

Até ao ponto da 2ª chamada (`startseq`/`r_perma`/`nopic`/`st620` idênticos),
não há regressão nenhuma nas métricas que já eram medidas antes desta sessão.
`thr_end` continua em 0 em ambas as variantes — não piora nem melhora nesse
critério específico dentro da janela. A diferença real é **qualitativa**: sem
o shell-fix, o guest recebia NULL e martelava ~9831× chamadas indirectas por
resolver (nunca bloqueava, só desperdiçava a janela); com o shell-fix, o
guest entra no código real e **para de progredir visivelmente** — não é
crash, é ausência de retorno.

## Hipótese para a próxima medição (não verificada — é INFERÊNCIA)

O método real de construct (`vt[+0x14]`) provavelmente espera uma free-list
com MAIS do que uma entrada — por exemplo, um `count` que realmente reflicta
quantos slots livres existem, e um `head` que avance para um NÓ SEGUINTE após
o pop (que hoje não existe: a nossa free-list sintética tem exactamente uma
entrada, sempre a mesma, e nada relaciona "usar o slot" com "avançar head").
Se o método faz um loop "enquanto não há entrada livre, espera/tenta
realocar", ele pode ficar preso à espera de uma 2ª entrada que o nosso
REPLENISH nunca cria (só reescreve a mesma). Distinguir exige instrumentar o
próprio `vt[+0x14]` (endereço resolve-se em runtime; log do `[FACTORY]`
mostra `d0=0x80000015 d2=0x0015` nas duas chamadas — o mesmo descritor) ou
capturar onde exactamente o PC fica preso (sample do `ctx->gpr` /
`ps3_call_opd` em profundidade, ou um watchdog de tempo dentro do próprio
`ps3_call_opd` gated).

## Estado dos artefactos

- `host_gow2_factory.cpp`: fix aplicado e commitado.
- `recomp_mid_v2/patch_type15_fl_shell_rehome.py`: novo, idempotente,
  FUNCIONAL (fix de correctude, sempre activo), commitado.
- `recomp_mid_v2/patch_type15_shell_probe.py`: novo, idempotente, PROBE
  (gated, OFF por default), commitado.
- `recomp_macos_v2` (produção): **NÃO tocado.** Fix e probes só testados em
  `recomp_macos_v2.type15fl_fix` / `boot_gow2_type15_fix` (clones de teste).
- `smoke_chain_gate.sh` completo **NÃO corrido** — `thr_end` continua em 0
  nesta janela de 90s em todas as variantes medidas; a condição de disparar o
  gate completo ("thr_end passa") não se verificou. A parede do HANG precisa
  de ser fechada primeiro.
