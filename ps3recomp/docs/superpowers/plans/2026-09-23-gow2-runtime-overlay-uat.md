# Overlay de runtime do GoW2 — checklist de aceite do usuário (Task 7 Parte B)

Rode este checklist depois de uma sessão normal de jogo pelo
`gow2_launcher.py` (ou pelo `boot_gow2` direto), na sua própria máquina. A
evidência offline/unit das Tasks 1-7 Parte A já passou (ver
`.superpowers/sdd/2026-09-23-gow2-runtime-overlay/task-7-report.md`); esta é
a parte in-boot que um agente não pode executar (não há controle físico, e o
jogo não deve ser aberto por uma sessão de agente enquanto alguém está
jogando).

Para cada item: anote PASS/FAIL e, onde houver uma linha de log indicada,
confira exatamente onde ela é emitida (ver "Onde procurar cada log" abaixo —
algumas linhas só aparecem no `stderr`/log do launcher, outras só dentro do
próprio menu, na aba de Logs). O overlay aparecer na tela **não é prova** de
que o jogo (guest) está desenhando pixels reais — essa aceitação é separada
(ver as paredes A-G no `CLAUDE.md` do motor).

## Onde procurar cada log (auditado no código antes deste checklist)

- **`[movieio]`** (abertura/leitura/fechamento de WAD) — `libs/video/movie_hle.c`
  linhas 806, 850, 1046: `fprintf(stderr, ...)` direto, sempre visível no
  terminal/log do launcher quando `PS3_MOVIE_IO=1` (ligado por padrão em
  `env_gow2.sh`). Não depende do overlay estar aberto nem fechado.
- **`[MOVIEFSM]` / `[MOVIEEOS]`** (transições de estado do filme) —
  `games/gow2/movie_eos_arm.c` linhas 742-745, 755-758, 814-818:
  `fprintf(stderr, ...)` direto, visível no terminal/log sempre que o
  amostrador está ativo (por padrão via `PS3_MOVIE_HLE=1`/`PS3_MOVIE_EOS=1`,
  ligados por padrão em `env_gow2.sh`). Também não depende do overlay.
- **`WARN overlay settings were not saved` / `overlay ImGui context
  unavailable`** — `libs/video/rsx_overlay.cpp`, em `rsx_overlay_init`
  (`ERROR overlay ImGui context unavailable`), `rsx_overlay_shutdown` e
  `rsx_overlay_persist_settings` (`WARN overlay settings were not saved`):
  essas chamam **só** `rsx_overlay_log_write()`, que escreve num anel
  **em memória** (`libs/video/rsx_overlay_log.c`, `rsx_overlay_log_write`)
  e **nunca** dá `fprintf`/`printf`. A única forma de ver essas linhas é
  abrindo o menu e olhando a aba **Logs** (aba própria, ao lado de
  Diagnóstico; `rsx_overlay_build_draw_data`,
  `ImGui::TextUnformatted(logs)`). Exceção: `overlay disabled: ...`
  (renderer do backend indisponível) vai também para o `stderr`, porque o
  menu nunca aparece nesse caso. **Não é possível conferir essas linhas com
  o menu fechado** — por isso o item 1 abaixo não pede pressionar F1, e a
  checagem dessas mensagens específicas fica no item 2, quando o menu já está
  aberto.

## 0. Preparação

- Inicie com o launcher/env normal para o overlay inicializar:
  `python3 games/gow2/gow2_launcher.py` (GUI) ou o script de shell que ele
  monta. Hoje o overlay só inicializa no backend Metal do macOS (Task 6); no
  Windows ele inicializa dentro do caminho de bridge D3D12 do `cellGcmSys.c`.

## 1. Overlay fechado (linha de base)

- Inicie o jogo normalmente, sem pressionar F1/F2 nem o combo do controle.
- **Observar:** o jogo se comporta exatamente como antes do overlay existir —
  teclado/controle chegam ao guest sem alteração, sem engasgo extra de
  frame.
- Não há checagem de log neste item — as únicas mensagens de erro/aviso do
  overlay ficam só no anel interno, visível apenas com o menu aberto (ver
  item 2).

## 2. Abrir/navegar/fechar com F1

- Pressione F1 (ou F2, se `toggle_key=2` foi editado **no arquivo** de
  configurações — não há controle para isso no menu).
- **Observar:** o menu aparece; as setas do teclado (o primeiro toque mostra
  o cursor de navegação) e o D-pad do controle movem o item destacado;
  Enter ativa. **Esc** ou F1 de novo fecha o menu (com um combo aberto, o
  primeiro Esc fecha só o combo), e esse Esc não fecha a janela nem sai da
  tela cheia.
- **Observar:** com o menu aberto, nem o teclado (WASD/setas) nem o mouse
  nem os sticks movem o personagem/câmera do jogo (a entrada é consumida
  pelo overlay e os analógicos chegam ao jogo centrados).
- **Observar:** segure uma seta, feche com F1 ainda segurando, solte, e
  reabra — o destaque não fica andando sozinho.
- **Log (agora, com o menu aberto):** abra a aba **Logs** e confira —
  **não deve haver** `WARN overlay settings were not saved`
  nem `overlay ImGui context unavailable` vindas de uma sessão normal (ver
  "Onde procurar cada log" acima).
- Feche com F1/F2 e confirme que a entrada do guest volta a funcionar
  imediatamente.

## 3. Combo Select+Start no controle

- Com o menu fechado, **segure Select e aperte Start** no controle (apertar
  o Start antes do Select, mesmo um instante, manda um Start solto para o
  jogo, que pausa — isso é esperado, não é defeito).
- **Observar:** o menu abre no toque (não repete enquanto o combo fica
  segurado — só a borda de subida conta). Soltar um botão antes do outro não
  deve deixar escapar um Start ou Select "solto" para o jogo.
- Feche do mesmo jeito (Select+Start de novo) — o texto de ajuda dentro do
  menu diz *"F1, Esc or Select+Start closes this menu"*.
- **Depois, separadamente, com o menu fechado:** pressione **só o Start**
  (sem Select). **Observar:** o GoW2 pausa (aparece o próprio menu de pausa
  do jogo) — um Start solto precisa continuar chegando ao jogo; só o combo é
  capturado pelo overlay.

## 4. Filme rodando com o overlay aberto

- Durante uma cena/filme de introdução, pressione F1 para abrir o overlay.
- **Observar:** o filme continua tocando/apresentando por baixo do menu (não
  pausa, não trava, não reinicia) — a apresentação do filme roda na thread de
  decodificação do Metal e não é dona do overlay.
- **Log:** o estado do filme continua avançando no terminal/log do launcher
  — linhas `[movieio]` de abertura/leitura/fechamento e linhas de transição
  `[MOVIEFSM]` / `[MOVIEEOS]` continuam aparecendo enquanto o menu está
  aberto (ver "Onde procurar cada log" acima — essas ficam no `stderr`, não
  no anel do overlay, então continuam visíveis independente do menu).
- Feche o overlay; confirme que o filme está numa posição mais adiante do
  que quando você abriu (não congelado).

## 5. Contadores de FPS/draws excluem o overlay

- Abra a aba de Diagnóstico.
- **Observar:** "guest draws" é só a contagem de draw calls do próprio jogo
  (não deve saltar só por abrir/fechar o menu sem mudança de cena do guest);
  FPS/tempo de frame é a contagem própria de apresentação do backend.
- Duas ressalvas já documentadas (não são defeito): FPS/tempo de frame
  contam só a apresentação do guest (as apresentações do filme, na thread de
  decodificação, ficam fora dessa mesma contagem), e no D3D12 "guest draws"
  é limitado por `MAX_DRAWS=256` registros reproduzidos por frame (ver
  `rsx_d3d12_backend.c:72`).
- Os contadores de shader ("hits"/"misses"/"compiles"/"failures") têm
  definição diferente por backend (ver a seção de Diagnósticos do README) —
  confirme que aparecem como "unavailable" só quando de fato não foram
  medidos, nunca mostrando `0` silenciosamente no lugar de "unavailable".

## 6. Trocar um remapeamento + uma opção gráfica, reiniciar, confirmar persistência

- Abra a aba de Controles e remapeie um botão do host para um botão
  diferente do guest. (A zona morta analógica começa em 0 — eixos sem
  alteração; só sticks de controle são afetados se você aumentá-la.)
- Abra a aba de Gráficos e troque uma configuração cujo rótulo termine em
  **"(restart required)"** (tamanho de janela ou tela cheia) e uma que não
  termine assim (VSync).
- Feche o menu, saia do jogo normalmente.
- **Observar:** a troca de VSync aplica na hora (ao vivo); a configuração
  que exige reinício não muda nada visivelmente até relançar o jogo.
- Relance (mesmo arquivo de configurações — não passe um
  `PS3_OVERLAY_SETTINGS` diferente). **Observar:** o remapeamento e as duas
  trocas gráficas voltam exatamente como você deixou, e **o menu em si
  inicia fechado** independente de você ter fechado ou não antes de sair.
- Local do arquivo de configurações (ver a seção "Onde fica o arquivo de
  configurações" do README) — confirme que a data de modificação do arquivo
  mudou depois das suas alterações.

## 7. Arquivo de configurações corrompido (opcional, próximo do offline)

- Com o jogo fechado, corrompa o arquivo de configurações (por exemplo,
  truncando-o ou substituindo o conteúdo por texto qualquer) e relance.
- **Observar:** o jogo inicia normalmente com as configurações padrão
  (janela 1280x720, VSync ligado, mapeamento padrão) em vez de travar ou se
  recusar a iniciar; o menu continua fechado ao ligar.

## Relatório

Para cada item numerado acima, registre PASS/FAIL/NÃO EXECUTADO e qualquer
trecho de log que sustente a resposta. Adicione o resultado a
`.superpowers/sdd/2026-09-23-gow2-runtime-overlay/task-7-report.md` (seção
Parte B) ou a uma nota de acompanhamento — o que a sessão que fizer o
checklist estiver usando.
