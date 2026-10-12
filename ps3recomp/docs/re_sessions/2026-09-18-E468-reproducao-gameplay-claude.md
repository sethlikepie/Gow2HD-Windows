# E468 — reprodução verificável do gameplay nativo no macOS

**Data:** 2026-09-18  
**Escopo:** God of War II HD (NPUA80491), host macOS/arm64 e backend Metal.

## Resultado provado

Gameplay real foi observado. Isto não é o overlay de prompts do host: a imagem abaixo contém HUD, personagens, geometria e pós-processamento produzidos pelo comando RSX do jogo.

O artefato local `../gow2-recomp/claude_runs/frames/sampoff_frame_gp0_f4093.png` mostra a cena. Foi escrito pelo backend Metal com:

```
[RSX metal] gameplay shot frame_gp0_f4093.bmp (2560x1440 draws=749 show=S1 by_last=0)
```

O mesmo ensaio manteve seis capturas, com `draws` entre 659 e 749:

| captura | frame | draws |
| --- | ---: | ---: |
| `gp0` | 4093 | 749 |
| `gp1` | 4993 | 686 |
| `gp2` | 5893 | 730 |
| `gp3` | 6793 | 690 |
| `gp4` | 7693 | 659 |
| `gp5` | 8593 | 685 |

O critério operacional é três linhas `[FPS]` consecutivas com `draws >= 400`; uma linha isolada não basta. Os logs `play1.log` e `alu_play_1.log` atingem o critério no segundo 58. Nas 20 s seguintes, têm mediana de 769 e 744 draws, respectivamente.

## Artefato de reprodução

No checkout irmão, o binário de referência é:

```
../gow2-recomp/boot_gow2_play
SHA-256: 6a86564da8a4caaa48c05862d6d89a97059614b3b2943e4a4b2e50c54772e172
```

Ele não deve ser versionado nem substituído sem conservar o hash. Os assets (`EBOOT.ELF`, `extracted/`, `movie_cache/`) continuam locais e nunca entram no Git.

O teste limpo de 2026-09-18 copiou esse executável para `/tmp`, conferiu o SHA-256 antes e depois da cópia e iniciou-o com `env -i`. Antes de 120 s, `../gow2-recomp/claude_runs/reproduction_play.log` já registrava 890, 918, 915, 932, 960, 946, 936 e 912 draws em sequência. Isso reproduz o marco do binário arquivado; a captura acima é a prova visual do conteúdo do jogo.

## Receita canônica

Pré-requisitos: macOS arm64, os checkouts irmãos `ps3recomp` e `gow2-recomp`, SDK Metal e os assets locais já extraídos.

```bash
cd ../gow2-recomp
shasum -a 256 boot_gow2_play
# deve imprimir 6a86564da8a4caaa48c05862d6d89a97059614b3b2943e4a4b2e50c54772e172

bash claude_runs/run_for.sh \
  boot_gow2_play \
  6a86564da8a4caaa48c05862d6d89a97059614b3b2943e4a4b2e50c54772e172 \
  reproducao_gameplay 180

python3 claude_runs/fps_aligned.py claude_runs/reproducao_gameplay.log 0 20 400
```

`claude_runs/run_for.sh` isola o processo e fixa:

```
PS3_VFS_ROOT=extracted/USRDIR
PS3_MOVIE_CACHE=movie_cache
PS3_MOVIE_HLE=1 PS3_MOVIE_IO=1 PS3_MOVIE_EOS=1 PS3_VDEC_ASYNC=1
PS3_PAD_AUTOSTART=1 PS3_SPU1=1
PS3_RSX_BACKEND=metal PS3_RSX_FIFO=1 PS3_GCM_CB=1
PS3_CELLSYS_REORDER=1 PS3_LWMUTEX_REAL=1 PS3_FIOS_STICKY_OWNER=1
PS3_MOVIE_DONE_MS=3000 PS3_METAL_PER_DRAW_RT=1
PS3_METAL_DEBUG_DEPTH=rsx PS3_TRACE_FPS=1
```

`PS3_PAD_AUTOSTART=1` só atravessa as telas até o nível. Para testar controle humano, confirme primeiro o carregamento com ele ligado; depois desligue-o e use o dispositivo físico.

Para nova prova visual, use `claude_runs/run_until_shots.sh` com o mesmo nome e SHA. Ele espera `gameplay shot frame_gp*` e converte as imagens para `claude_runs/frames/`. Essas capturas são locais e não devem ser commitadas por conterem conteúdo do jogo.

## O que destravou o caminho

Não foi uma variável milagrosa, mas a composição de correções reais:

1. O filme termina por EOS/VideoToolbox assíncrono e o fluxo segue para o loader.
2. A thread guest `AUTO_LOAD` volta a executar; ela não pode ficar stubada, pois o FSM espera seu término. Ver [E93–E97](2026-09-02-E93-a-parede-G-e-uma-cadeia-de-paliativos-nossos.md).
3. FIOS/SPURS funciona com reorder, dono sticky, mutex real, SPU1 e callback GCM. Nos logs há `dispatch HIT` dos dois workloads SPU e `SPUJOB ... returned cleanly`.
4. FIFO GCM, salto de wrap, render targets por draw, vertex constants, texturas, blend e composição Metal passaram a conservar os comandos RSX. O histórico está em [E412–E415](2026-09-14-E412-E415-sticky-owner-and-giant-handoff.md), sobretudo E439–E463.

`PS3_METAL_PER_DRAW_RT=1` faz parte da receita porque o frame usa vários targets intermediários; removê-lo pode apresentar uma superfície preta mesmo com muitos draws guest.

## Limite para rebuilds

O binário `boot_gow2_play` é reproduzível por hash. O *rebuild* idêntico ainda não é: a worktree do motor contém alterações locais não commitadas, inclusive em runtime/PPU e GCM. Não se deve prometer que `build_macos.sh` produzirá o mesmo SHA antes de essas alterações serem revisadas, testadas e agrupadas em commits explícitos.

Antes de promover outro binário, sempre:

1. registre `git status --short`, o SHA e as variáveis;
2. exija três amostras consecutivas acima do limiar;
3. gere e inspecione ao menos uma captura `gameplay shot`;
4. só então atualize este runbook, sem apagar o binário anterior antes de uma reprodução independente.

Isso separa futuras regressões entre binário/ambiente diferente, boot que não chega ao nível, e renderer que chega ao nível mas não compõe a imagem.
