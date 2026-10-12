# RE do rig/animação do GoW2 HD — esqueleto, skinning e pacote ANMX (r_hero01)

Data: 2026-07-22. Fonte: `wad/r_hero01.wad_ps3` **de dentro do `gow2.psarc`**
(extração nativa provada md5-idêntica, `psarc_read.c`). Todos os offsets abaixo
são do WAD do herói (3.418.720 bytes); MODL do corpo do Kratos em `0x2d29cb`.
Método: arqueologia de hex clean-room (scripts em scratchpad, sessão 2026-07-22);
validações são somas/invariantes exatas, não impressão visual.

## 1. Malha (recap, já commitado em ps3recomp)

- Diretório de atributos 16B `TAG|type|off|sz` (MODL/POS0/NRM0/COL0/TEX0/BONI),
  offsets relativos ao MODL; **todo stream de vértice começa em off+12**
  (12B de header/sentinela).
- `POS0` = (x,y,z,w) f32 **BE** stride 16, w = peso de skin 0..1.
- `TEX0` = (u,v) half BE stride 4. `BONI` = (b0,b1,0,0) u8 — 2 bones/vértice,
  índices **locais da paleta do segmento**.
- Descritor MODL: `index_count` u32 BE em +12, depois lista de triângulos
  u32 BE (janelas de 3 alinhadas). Kratos: 4378 verts, 15321 índices.

## 2. Esqueleto (123 bones) — região ~0x33b7f4..0x33ea30

| Bloco | Offset | Formato |
|---|---|---|
| Hierarquia | `0x33b7f4` | 124 records × 16B: `{u32le flags, u16le self, u16le link, u16le parentRec, u16le ?, u32le 0xd1000000}`. **parent(bone) = parentRec + 1** (records indexam a partir de template.0; 0xFFFF = filho de main). Validado: pelvis→vertebrae2→3→4→neck→head. |
| Nomes | `0x33bfb4` | 124 × 24B ASCII (o 124º é lixo/terminador → 123 bones reais). `main(0), template.0(1), pelvis(2) … joint0(0x42)..joint47(0x6b) [saia] … linkJoint(122)` |
| Bind local | `0x33cb70` | 123 × matriz 4×4 f32 **LE** (column-major, coluna de translação em [12..14]). LOCAL (relativa ao pai). Ex.: pelvis t=(0, 21.654, −0.570); neck t=(0, 4.453, 0); head t=(0, 2.313, 0). |

Nota de endianness: streams de vértice são BE (PS3), mas hierarquia/bind/
segmentos usam campos LE ou BE conforme o bloco (herança de tooling PS2) —
seguir exatamente as tabelas acima.

## 3. Segmentos de skinning (matrix palette) — PROVADO

Após o index buffer do descritor MODL: `{u32be 5, u32be 16}` (significado do 5
em aberto; 16 = tamanho máx. de paleta) + bbox global 6×f32be, depois **16
records** contíguos:

```
record = { head 3B (00 SUB 00, SUB = id de submesh/grupo),
           u32be first_vert, num_verts, first_index, num_indices, nbones,
           u32be bones_globais[nbones],      # ids na tabela de nomes (§2)
           f32be bbox[6] }
```

Kratos (16 segmentos): 7 patches da saia (joint0..joint47, 15–23 verts cada),
corpo pelvis..rWrist (1205 v), 4 segmentos de rosto (head..lip joints), mão
esq. (lRadius..lThumbF3), mão dir., pernas (lTibia..rMetatarsal), baixo corpo
(pelvis..rPhalanges).

**Validação exata (script diag_segfinal.py):**
- (a) Σ num_verts = **4378** ✓ e Σ num_indices = **15321** ✓ (sem gaps);
- (b) todo vértice do range tem `b0,b1 < nbones` do seu segmento ✓;
- (c) **auto-contenção**: os índices de cada segmento só referenciam vértices
  do próprio range ✓ (16/16).

⇒ **bone global do vértice v = bones[seg(v)][b0(v)] / [b1(v)]**, peso = w do
POS0. Cadeia completa vértice→bone→hierarquia→bind fechada com dados reais.

Depois dos 16 records segue outro bloco (`41 00 00 00 …` @rel `0x35d58`) ainda
não decodificado (provável: mais tabelas por-mesh/LOD).

## 4. Pacote de animação ANM_hero — estrutura externa

Record em `0x1d8640`: `{u16le 1, u16le 0x1d, u32le size=0xf3d80(998784),
nome[24]="ANM_hero"}`; campos em `base+0x20`:
`{u32le 3, 0, 0x3001, size, (u16le 2, u16le NCLIPS=189), u32le 0x4ae,
u32le offsets[189]}` — offsets **relativos a base+0x20**, ascendentes
(0x340, 0x500, 0x2d940 … 0xf3d40).

Cada clip começa com header 0x40: `{u32le a, b, hdr=0x40, c, d,
nome ASCII embutido}` — ex.: clip0 "hero" (64B, placeholder), clip2
**"AirAttacks"** (63KB), clip1 sem-nome? 185KB (maior), clip46 82KB,
clip43 67KB. Mediana 128B ⇒ maioria são stubs/eventos; a animação de
verdade está nos ~dezenas de clips grandes.

**Em aberto (próximo passo): codec dos tracks/keyframes** dentro do clip.
Pistas observadas no clip "AirAttacks": campos internos `{0, 0x80, 0x1000,
0x2180}` (sub-offsets), runs de bytes `01 01 01…` (flags por bone?), pares
u16 com valores < 123 (ids de bone). Chunk `0x428` do diretório top-level:
entry `ANMX_R_Hero` (type 1, esz 0x30).

## 5. Chunk directory (top-level) — parcial

Entries tipo `{u32 type, u32 entry_sz, nome 32B, …}`; walk simples quebrou
(WAD_R_Hero esz=8) — framing do diretório raiz ainda por fechar (não bloqueia:
localização por scan de records funciona).

## 6. O que já roda nativo (ps3recomp/libs/video/tests)

`mesh_spin_tex` (psarc→WAD→malha+textura+skin em memória, zero Python) +
`--skin-test` (deformação com BONI+pesos reais, bones procedurais por cluster).
Com §2+§3 deste doc, o próximo incremento é skinning com o esqueleto REAL
(FK pela hierarquia + bind local) e, com o codec do §4, reprodução das
animações do jogo — 100% dados do psarc.

## 7. Codec do clip (RE em curso, sessão 2026-07-22 tarde)

Sub-anim (ex.: attAirSlashH1 @clip+0x80): header 0x60 {u32le 0x3042, 0, 4,
f32 1.0, 0, f32 DURACAO_s (x30 = frames), u16 0xffff, u16 ANIM_ID,
u32 hash, nome[24], ..., u32 size}; descritores 16B em +0x60:
{u16 0, u16 b, u16 c, u16 d, u32 OFF, f32 1/30} ate dt!=1/30 — 3 secções:

- sec1 (b=5,c=2,d=2, maior): tracks de rotação dos bones. Header
  {u16 0, u16 N?, u16 ?, u16 1, u16 NFRAMES, u16 ?} + registos de track
  possivelmente VARIÁVEIS terminando em {u16 NKEYS=frames+1, u16 OFFSET}
  com offsets ascendentes (ex.: 0x6dc, 0x748, 0x944, 0xaac). Tamanho
  total ≈ frames*128 bytes (H1: 22f→0xb00; H2: 26f→0xcc0 ≈ 121-122/f).
  Dados nos offsets: streams de deltas s8 (fc/fa/fb/fd/fe...).
- sec2 (b=2..3,c=1..2,d=4): mesma estrutura de header {0, 0x62, 0, 1,
  NFRAMES, ...} — provável translação/root motion (elementos de 4B).
- sec3 (b=0,c=0,d=2, 0x40): DECODIFICADA: 3 registos
  {u32le bone, u32le 2, u16 a, u16 b} para bones 0x74/0x76/0x77
  (rWeapOH/lWeapOH/lChainW) + pad 01010101 — refs de arma/anexo por
  anim (a=0x2b em H1, 0x33 em H2; b=0x24/0x1c/0x14 fixos por bone).

Constantes: dt=1/30 em TODOS os descritores; NKEYS = frames+1;
ANIM_ID sequencial (H1=0x15, H2=0x16, H3=0x17). Falta: semântica exata
dos campos A/B dos registos de sec1 (canal/bone), formato do stream de
keys (delta s8 com base? escala?), e mapear bone→track.

### 7.1 Tracks (attAirSlashH1, dumps de referência)

Secção = N registos de **12B** `{u16 a,b,c,d,e,off}`; os registos acabam
exactamente em `off` do 1º (sec1: 5 tracks, sec2: 2). Tamanho do track k =
off[k+1]-off[k].

sec1 t0 {0,0xc2,0,1,0x16} sz=1696, dados:
`01021400 01000100 4e000200 15000000 62051e41 8200007f 777f0000 00..00
 00609a0f 00000000 0000f7f9 ...` (sub-header `01 02 14 00`; 0x15=21=ANIM_ID;
 depois maioria zeros + rajadas de deltas s8)
sec2 t0 {0,0x62,0,1,0x16} sz=512:
`01021400 01000100 2a000200 15000000 92011e09 4a000007 ...` (mesmo shape)
sec1 t1 {0x80,0xa2,0,0,0x17} sz=108:
`8409e432 45e2f4c0 9bf5e928 01021400 02000100 ee060300 00000000 16071502
 32000400 ...`
sec1 t2 {0,2,2,0,0} sz=508: `07fb0300 03f505e6 06e503f0 d6259efd a5259ffd
 f423c0fd 1e724000 007f777f 07...` (deltas s8 densos)
sec1 t3 {0x70,2,0x17,0,0} sz=360: `2e0d00fc 84e1ee26 d111463e 00000000
 00007d51 cfeff3be c8e78027 00000000 00007d51 16073000 ...` (`7d51`
 recorrente ≈ w de quat quantizado 0.637?)
sec1 t4 {0xc9,0,0xd,0,0} sz=84: **6 keys de 14B (7×s16le)**:
`95e8 e5bd 2707 7b0b b209 1d11 78b4` → `95e8 b6be 2707 cd09 b209 1d11 78b4`
→ `95e8 b5bf 2707 0908 ...` — 5 componentes constantes, 2 variam
(quat(4)+trans(3) quantizados s16?). Termina `3cc1 3cc1 0000`.

Hipóteses vivas: tracks tipo-A (a=0,d=1) = stream comprimido c/ sub-header
`01 02 14 00` + ANIM_ID; tipo-B = arrays de keys 14B (7×s16); campos a/b/c
ainda sem semântica (0x80/0xa2/0x70/0xc9 nao sao ids de bone directos).
Próximo: decodificar t4 como 7×s16 (escala 1/32767?) e correlacionar com o
bind dos bones de arma (sec3 aponta rWeapOH/lWeapOH/lChainW).

### 7.2 Calibração dos quats + veredito do codec (fim da sessão 2026-07-22)

**CRACKEADO**: rotações quantizadas como quat s16le de norma constante
**20861 ≈ 32768×(2/π)** (verificado em grupos independentes:
(-7804,9966,4561,15942) e (-4145,-16653,-6200,10112) → norma 20859-20862;
identidade = `0000 0000 0000 7d51`). Decode: q = s16/20861.

**walkBlend** (clip 42): header de clip com hdrsize 0x220 (varia por clip!);
5 sub-anims: navWalkFast(20f)/Slow(40f)/FastL/FastR/Land; dt=1/15 nas
secções (não 1/30 — dt do descritor manda) e uma secção dt=1/60.

**Veredito**: varredura global (189 clipes) mostra que NENHUM sub-anim usa
arrays puros de quats (máx 2.9% de cobertura) — o grosso é um formato
COMPRIMIDO (streams com prefixos tipo `1e7c 4000 007f 7770`, rajadas de
deltas s8, quats-âncora esparsos). Data-only RE do comprimido esgotou o
custo/benefício.

**Próximo passo decisivo** (caminho garantido, e o metodológico do projeto:
usar o próprio código do jogo): rastrear o DECODER real no lift PPU —
âncora: string `tAnimSystem` (@0x69a4d no WAD; achar o uso no ELF),
correlacionar com acessos ao pacote ANM (offsets 0x340/0x500 do header) e
levantar a rotina de descompressão de track. Com ela, o player nativo do
viewer (GPU skinning já pronto, só trocar a fonte do poseL) toca
navWalkFast em loop.

### 7.3 DECODER LOCALIZADO no lift PPU (ataque ao código do jogo)

Rota: string "tAnimSystem" (VA 0x4c3148) → entrada de typemap @0x53b370 →
estrutura de tipo @0x538c98 = **vtable com 16 métodos** (0x390ef0..0x391d50,
rtoc 0x541178). Vizinhos no typemap: zeroJoint/synchJoint/linkJoint.

Varredura global de assinatura (casts s16/s8 + densidade float + loops) nas
56k funções lifted apontou a família:

- **`func_0022AD7C`** (ppu_recomp_003.cpp:372725) e gêmea **`func_0022BF14`**
  (~700 linhas cada; variantes com/sem interpolação?) + continuações
  `func_0022B870/0022BB10/0022BB30/0022B08C` e `func_00229828` (setup).
- CONFIRMAÇÃO estrutural: lê os u16 em +0/+2/+4 do registo de track (12B),
  soma-os como CONTAGENS DE KEYS (a+b+(c<<2 via rlwinm(...,2,...)?)),
  converte p/ float e multiplica por fpr[8]=dt → duração; depois clamp de
  índice de key por frame (gpr[6/7] = frame − campo(+2), comparações com
  campo(+0)) e leitura de bytes de config numa tabela global TOC
  (r2−0x29EC). ⇒ campos do registo {a,b,c} = nº de keys por CLASSE
  (absoluta/delta?), e=?; stride de leitura +2 no stream.
- Falso-positivo eliminado: os 2 usos de 2/π no ELF (0x2edc7c/0x2ee8bc via
  TOC r2−0x96C) são LIBM (redução de argumento de sin/cos), não o decoder.
- Família secundária `func_0026EFxx` (fp≈399): blending/slerp de pose.

Próximo: transcrever func_0022AD7C+continuações (clean-room, ler o lifted
C++) para wad_anim.c nativo; validar contra navWalkFast e tocar no viewer.

### 7.4 Verificação cruzada dos dados + navegação nativa (paralelo à análise do lift)

Navegação portada para C (ps3recomp/libs/video/tests/wad_anim.c) e validada:
navWalkFast dur=0.667s, sec0/sec1 = 2 tracks de rotação cada (t0≈3KB, t1≈276B),
sec2/sec3 = refs de arma (bones 116/118/119/122). Records de 12B confirmados.

Dequant (verificação cruzada no ELF, independente do subagente que lê o lift):
- 1/32768 (0x38000000) = escala de dequant s16 mais comum no jogo (7012×).
- Norma dos quats crus = 20861 ≈ 65536/π = **32768 × (2/π)**. Logo: após ÷32768
  os componentes têm norma 2/π (0.6366, constante 0x3f22f983 presente no ELF);
  dividir os s16 crus por 20861 dá quats UNITÁRIOS directos (verificado:
  1º quat da walk = (-0.198, 0.004, 0.026, 0.980), |q|=1.000). ⇒ decode:
  q_unit = s16_cru / 20861 (ou /32768 e normalizar).
- Os 2 usos de 2/π no código (0x54080c/0x5409bc) são LIBM (redução p/ sin/cos).

Estrutura do stream (t0): header bit-packed (palavras `7f 77 70`/`f0 f7 f9 77`
recorrentes = controlo de canais/deltas), quats-âncora esparsos (233 no track),
runs de deltas s8. NÃO há directório de bones em texto claro — o mapeamento
bone→canal está codificado no header bit-packed (parte do decode do lift).

### 7.5 Decoder TRANSCRITO até à parede da tabela-G (fim honesto do RE estático)

Análise completa do lift (subagente, HIGH confidence no algoritmo):
- **Entry real = func_0022A540** (não AD7C); AD7C≡BF14 = clones de fase da
  mesma função, partilham o frame. Args: r3=descritor, f1/f2=tempo início/fim
  em SEGUNDOS, dt=float @ *(r3+0x3C)+0x6C.
- **NÃO é normalizador de quat**: é um DESCOMPRESSOR base+residual.
  1. âncoras s16 absolutas -> LERP linear das 2 âncoras que cercam o frame
     (nlerp, **ZERO slerp**: sem sqrt/sin/cos em toda a família).
  2. integra streams de delta s8: `acc[p] += (w >> exp) * delta_s8` por bit
     de uma máscara de classe; componente em out+p*4, linhas de classe 0x40.
  - Norma 20861 (=32768×2/π) é aplicada pelo CONSUMIDOR, não aqui (0x517D
    não aparece no código, grep-verificado em 1.2MB).
- **Constantes do TOC (0x541178) LIDAS**: K=1.0, EPS=1/4096 (0x39800000),
  WSCALE=16384=2¹⁴ (0x46800000). Batem com a mecânica de peso fixed-point.

**PAREDE (honesta, não contornável estaticamente):** o mapeamento
bit-stream→componente vive na tabela **G = *(r2-0x29EC) = 0x70082c**, que é
**endereço de RUNTIME** (fora dos LOAD do ELF, max VA ~0x576ea4). G é
populada na init do anim system. Sem G não dá para parsear deterministicamente
o stream comprimido — teste empírico confirmou: os "group descriptors" 8B
inferidos dão g0=13353 (absurdo p/ 20 frames) e as âncoras não são quats 8B
consecutivos. O kernel está certo; o ENDEREÇAMENTO dos bytes no stream depende
de G.

**Caminho faithful (metodologia do projeto: usar o código do jogo):** probe
gated no boot recompilado que, com o anim system já inicializado (G populada),
força a avaliação de navWalkFast e despeja as matrizes/quats por bone por
frame para ficheiro; o viewer nativo (skinning GPU já pronto) reproduz esses
poses. 100% fiel (saída do próprio decoder do jogo), sem forjar. Alternativa
mais pesada: RE da init de G (achar a fonte estática que a popula em 0x70082c).

Scaffold pronto para receber o resultado: ps3recomp/libs/video/tests/
wad_anim.c (navegação validada) + anim_decode_quat (stub, assinatura pronta).

### 7.6 Refino da extração de keyframes — estrutura cede, mapeamento não (conclusivo)

Tentativa a pedido de refinar sem o boot. Resultado:

**Estrutura de keyframe CEDEU (RE real):** o track t0 é keyframe-major —
header=40B, stride=248B, **12 keyframes (=record.c)**, 15 slots de quat reais
por keyframe (após dedup de janela deslizante). Track t1 é 100% estático
(0/60 bytes mudam entre os 3 keyframes). Isto não precisou da tabela G.

**Mapeamento slot→bone NÃO cede estaticamente (parede confirmada):** busca
exaustiva de convenção — 24 permutações de componentes × 16 sinais × conjugado
× {absoluto-local, delta} = 1536 combinações — contra os quaternions bind
(extraídos das matrizes bind por mat→quat). Melhor convenção: apenas **6/15**
slots casam <15° com o bind, e degenerado (slot0 e slot8 ambos→main [0°, bind
identidade casa qualquer quat pequeno]; rFemur casa 4×). Não é mapeamento
válido. Os quaternions extraídos são reais (norma 20861) mas o seu bone e
referencial não são recuperáveis por proximidade ao bind — dependem do
header bit-packed que a tabela G de runtime interpreta.

**Veredito final do RE estático:** o formato ANMX está decodificado ao nível
de ALGORITMO (descompressor base+residual, sem slerp, constantes do TOC
lidas) e de ESTRUTURA de keyframe (header/stride/count), mas o
endereçamento canal→bone vive na tabela G runtime (0x70082c, fora do ELF).
Caminho fiel restante: oráculo no boot (dump das poses quando r_hero01
carrega e G é populada). NÃO forjar o mapeamento.

### 7.7 Tabela G da animação: evidência de população por SPU (liga ao Plano 3)

Investigação do populador de G (=*(r2-0x29EC)=0x70082c, região BSS runtime):
- **Zero writes** ao slot TOC do ponteiro de G no PPU (só 20 reads).
- **TODOS os leitores do ponteiro** são a própria família do decoder de
  animação: func_0022A540 (entry), func_0022AD7C/BF14 (blocos de decode),
  func_0022A030/A62C/A670/A730/A7B4… (drivers). Todos CONSOMEM G (leem as
  máscaras de classe); os vm_write que têm são para o frame de stack
  (r1-rel) e o buffer de saída, não populam G.
- Conclusão: nada no PPU popula o CONTEÚDO de G → forte evidência de que G é
  preenchida por **DMA de um job SPU** (escreve direto em memória principal,
  invisível como vm_write do PPU). Consistente com o GoW2 fazer o setup de
  canais de animação no SPU.

Implicação para a Camada 2 (Plano 3 SPU deep): a animação de personagens
depende de G estar populada, e G provavelmente vem de um job SPU. Logo o
caminho "personagens animam" passa por jobs SPU reais (DMA) funcionando — não
só pelo decoder PPU. Reforça a ordem: SPU sólido → animação → gameplay.
