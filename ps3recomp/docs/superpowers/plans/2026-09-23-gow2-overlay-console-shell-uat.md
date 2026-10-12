# UAT — Central estilo PS5 do overlay (GoW2, macOS)

Quem roda: o usuário, no próprio Mac, com `./jogar_g2.sh` ou o `GoW2 Recomp.app`
(o agente não abre instâncias com o usuário jogando). Marque cada item com o
que **viu**; "não testado" é resposta válida. Nada aqui prova pixels naturais
do jogo — a central é UI do host.

Build testado: `git rev-parse --short HEAD` = ________  Data: ________

## Abrir e fechar
- [ ] Botão **PS/Home** abre a central; o macOS **não** abre Launchpad/Game Center junto; o próprio toque do PS/Home nunca chega ao jogo como botão (Kratos não reage a ele).
- [ ] PS/Home de novo fecha; segurar PS/Home não fica abrindo/fechando.
- [ ] **Select+Start** (segurar Select, apertar Start) abre e fecha; Start sozinho continua pausando o jogo.
- [ ] **F1** abre e fecha.
- [ ] Abre sempre em **"Voltar ao jogo"**.
- [ ] X em "Voltar ao jogo" fecha e o Kratos **não** pula; Enter fecha e o jogo **não** pausa.
- [ ] Fechar segurando um botão qualquer (Cruz, Círculo, Select, Start, uma seta): o jogo só recebe esse botão depois de solto (nenhum pulo/ação atrasada assim que a central some).

## Visual
- [ ] Jogo atrás **desfocado e escurecido**, e continua se mexendo (desfoque ao vivo).
- [ ] Faixa escura em degradê embaixo; cartões arredondados; cartão focado maior, com borda dourada e brilho dourado; vidro em pedra quente (paleta do GoW, não cinza neutro); chaves ligadas em vermelho-sangue (não azul).
- [ ] "Sair" fica sozinho à direita.
- [ ] Painel flutuante acima do cartão focado, com sombra; linha focada destacada; chaves azuis.
- [ ] Topo: "God of War II HD" à esquerda; hora à direita; ícone do controle e bateria **só** com controle que informa bateria.
- [ ] Texto nítido em tela Retina e em tela cheia (sem serrilhado nem borrão).
- [ ] Animação: a fileira sobe com mola ao abrir; o foco desliza; painel troca com fade curto; fechar faz o inverso.
- [ ] Janela pequena (1280×720), redimensionada e tela cheia: nada se sobrepõe, nada sai da tela.
- [ ] Arrastar a janela de uma tela Retina para uma tela comum (ou vice-versa) com a central aberta: texto e ícones continuam nítidos depois da troca (sem embaçar nem cortar).

## Cartões
- [ ] **Tela**: Tela cheia e VSync mudam na hora; Tamanho da janela abre a lista e aplica; linha "requer reiniciar" aparece só se o backend pedir.
- [ ] **Controles**: zona morta anda de 1 em 1 (0–30 %) segurando esquerda/direita; Vibração desliga o tremor do jogo; Restaurar padrão volta o mapa.
- [ ] **Remapear**: escolher "Cruz", apertar L3 → L3 passa a pular no jogo; o botão antigo troca de lugar; Esc cancela; 5 s sem apertar cancela; nada do que é apertado durante a captura chega ao jogo.
- [ ] **Som**: Som do jogo desliga/religa o áudio do jogo na hora; Sons da interface e Vibração da interface desligam cada um.
- [ ] Com "Som do jogo" desligado, uma cutscene/intro em andamento silencia na hora (sem esperar acabar ou reabrir); religar volta o áudio dela.
- [ ] **Desenvolvedor**: FPS/ms, draws, estado do WAD, shaders — ou "indisponível"; Logs abre a lista (mais recente primeiro) e rola.
- [ ] **Sair**: pede "Sair do God of War II HD?"; Cancelar volta; Sair fecha o jogo.

## Sons e háptica
- [ ] Sons curtos ao abrir, mover, confirmar, voltar, fechar; nenhum estalo.
- [ ] Tique no controle ao navegar (com "Vibração da interface" ligada).
- [ ] Nada disso engasga o jogo.
- [ ] Trocar a saída de áudio (ex.: ligar/tirar AirPods ou fone) com a central **aberta**: o jogo não trava nem fecha; os sons da interface continuam ou, na pior hipótese, ficam mudos pelo resto da sessão (sem travar ou repetir estalo).

## Persistência e launcher
- [ ] Mudar tela cheia/VSync no jogo, fechar, abrir pelo launcher: o launcher mostra o valor novo e o jogo abre assim.
- [ ] Mudar no launcher: o jogo abre com o valor do launcher.
- [ ] Mudanças da central continuam depois de reiniciar o jogo.
- [ ] Com `./jogar_g2.sh`: desligar Tela cheia e VSync na central, sair, rodar `./jogar_g2.sh` de novo: abre em janela e sem VSync (o script não força mais). `PS3_FULLSCREEN=1 ./jogar_g2.sh` abre em tela cheia só naquela vez.

## Com a central fechada
- [ ] Jogo idêntico a antes (FPS, controles, som, vibração).
- [ ] Cutscene in-game e intro tocam normalmente, com e sem abrir a central durante elas.

Observações / prints: ______________________________________________
