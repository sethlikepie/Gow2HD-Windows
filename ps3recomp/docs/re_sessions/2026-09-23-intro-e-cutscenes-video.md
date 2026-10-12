# 2026-09-23 — Intro e cutscenes: vídeo tocando 2x, fora de ordem e cortado

Resumo das correções da leva de vídeo no macOS (Metal + VideoToolbox), com o que **não pode voltar**.
Todas confirmadas pelo usuário na tela (jogo real, tela cheia).

## 1. Imagens fora de ordem (B-frames) — o "vídeo quebrando/travado"

- **Sintoma:** intro e cutscene "travando", com a cadência PERFEITA
  (`PS3_TRACE_VDEC_PICTIME=1`: 33,3 ± 0,9 ms, zero buracos).
- **Causa:** o VideoToolbox devolve MPEG-2 na ordem de **descodificação**. Os filmes do GoW2
  usam GOP `I0 P3 B1 B2 P6 B4 B5 …` (intro e `introhud.m2v`). Enquanto o overlay host
  (`PS3_MOVIE_HLE`) desenhava o filme, isso não aparecia. Com o overlay desligado, o próprio
  jogo mostra as imagens do `cellVdec` e o movimento ia e voltava.
- **Fix (`libs/codec/cellVdec_vt.m`, ps3recomp `4c7cc134`):** reordenação clássica do MPEG-2.
  Um B sai logo; uma âncora I/P fica retida até chegar a âncora seguinte. O tipo da imagem
  vai no `sourceFrameRefCon`. `PS3_VDEC_VT_DECODE_ORDER=1` repõe a ordem antiga (só para A/B).
- **Guarda de regressão:** `tools/vdec_order_check/` (instruções no cabeçalho). Roda contra o
  `.m2v` do próprio usuário e dá `exit 1` se voltar o serrilhado. Medido: OK = salto médio 0,26;
  ordem antiga = 5,17.
- **Lição:** cadência boa + usuário dizendo "travado" → olhar a ORDEM e o conteúdo das
  imagens, não o tempo.

## 2. Intro tocando duas vezes

- **Causa:** o hook midasm nosso `gow2_midasm_Ce03cWaitIdle` gerava um 2.º `cellVdecStartSeq`.
  O RPCS3 mostra um só.
- **Fix:** o hook fica OFF por padrão (`PS3_CE03C_WAIT_IDLE=1` repõe). `vdec_intro_seqs()` conta
  a intro como 1 sequência. Sem isso, a cutscene seguinte era tratada como intro e o timer da
  intro fechava-a após ~1 s.

## 3. Cutscene in-game cortada / fora de ordem (antes nesta leva)

- A fila de AUs cheia devolve `CELL_VDEC_ERROR_BUSY` (antes descartava AUs em silêncio e
  mandava AUDONE). `PS3_VDEC_AU_DROP=1` repõe o antigo.
- A imagem in-game espera o Play (st620 11). `PS3_MOVIE_START_AT=seq` repõe o antigo.
- `GetPicture` converte para o `CellVdecPicFormat` pedido (o GoW2 pede RGBA32 BT.709).

## 4. Apresentação

- `PS3_GCM_VSYNC=0` no `env_gow2.sh`: o vsync do Metal (`displaySyncEnabled`) já cadencia o
  present; o timer host de 60 Hz em cima dele criava buracos.
  Espaçamento: 33,5 ± 5,0 → 33,1 ± 2,2 ms.
- Diagnóstico `PS3_TRACE_PRESENTTIME=1` (OFF) no present do Metal.

## Não regredir

1. Não reativar o overlay host (`PS3_MOVIE_HLE=1`) nem a fila de logos (`PS3_BOOT_LOGO_QUEUE=1`)
   como default.
2. Não voltar a entregar as imagens do VT na ordem de descodificação.
3. Não descartar AUs com a fila cheia (é BUSY).
4. Não reativar o hook CE03C por padrão.
5. Ao mexer em `cellVdec_vt.m`, rodar `tools/vdec_order_check`.

## Aberto

No fim da intro (~7 s em diante), a entrega de imagens tem pausas de 120–400 ms, justo quando
começa o carregamento dos WADs. Visto em corrida oculta; na tela do usuário, foi aceitável.
Ainda sem causa.
