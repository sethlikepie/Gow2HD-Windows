# O padrão do dia: quatro funções a receber argumentos que não são objectos

**Data:** 2026-08-01 · Tudo medido, com o número de corridas indicado em cada caso.

Passei o dia a caçar quem estraga memória. Encontrei **um** caso real e corrigi-o (o
`F2B-STREAM-PUMP`, ver a nota da causa raiz). Mas os sintomas que sobraram têm outra
natureza, e o `PS3_WATCH_STORE` mostrou-o **quatro vezes**: ninguém estraga bytes. A
memória escrita está toda correcta onde foi escrita.

O que há é **despacho**: funções a correr com `this`/argumentos que não são o que elas
assumem.

| # | função | o que devia ser | o que lá está | corridas |
|---|---|---|---|---|
| 1 | `func_002545D4` | `rec`, payload do nó | **`0`** → lê "tipo" do endereço guest 2 | 4/4 |
| 2 | `func_002545B0` (2.ª chamada) | objecto com lista em `+0x7C` | objecto cuja `+0x7C` é `matriz[0][3]` | 1/1, com contra-exemplo são ao lado |
| 3 | `func_00263554` | `pool` | **`5`** → lê `*(9)` | 1/1 |
| 4 | `func_00220284` | `this` | **`0`** → lê os campos de memória baixa | 1/1 |

## O #4 é o mais limpo, e prova a direcção

A sonda `PS3_TRACE_CPY284` mostra **12 chamadas perfeitamente sãs** e depois uma má:

```
[CPY284] #11 this=0x407D44F0 n=8 p50=0x407CFD20 dst60=0x407CE840 p5C=0x407CE7C0
[CPY284] #12 this=0x407D46E8 n=3 p50=0x407CFF54 dst60=0x00000000 p5C=0x407E4E50
[CPY284] #13 this=0x00000000 <THIS-MAU> n=4 p50=0x4077CAB8 dst60=0x00000000 ...
```

E a correlação com as escritas más é **linha a linha, no mesmo log**:

```
4391: [CPY284] #13 this=0x00000000 <THIS-MAU>
4392: [vm] UNCOMMITTED write32 access 0x726D5B32 ra=func_00220284+0x1244
4393: [vm] UNCOMMITTED write32 access 0x726D5B3A ra=func_00220284+0x1274
4394: [vm] UNCOMMITTED write32 access 0x726D5B2E ra=func_00220284+0x12A4
4395: [vm] UNCOMMITTED write32 access 0x726D5B36 ra=func_00220284+0x12D4
4396: [vm] UNCOMMITTED write32 access 0x726D5B42 ra=func_00220284+0x1244   (ciclo)
```

Com `this = 0`, a função lê os seus próprios campos da base da memória guest
(`*(0x50)`, `*(0x60)`, `*(0x68)`…), apanha valores que *parecem* ponteiros, e escreve
quatro floats por iteração através deles. **As escritas por ponteiro-texto que persegui
duas rondas são consequência, não causa.**

## O que isto invalida do que escrevi hoje

- *"A free-list do allocator tem uma string lá dentro"* — **errado**. O `pool` é que vale
  `5`; não há lista nenhuma envolvida.
- *"É corrupção de memória a montante"* — **errado** para estes quatro. Há uma corrupção
  real e corrigida (o pump), mas não é esta.

## Onde isto põe o trabalho

Deixa de fazer sentido caçar escritores. O que interessa agora é **quem escolhe o alvo e
os argumentos da chamada**, e para isso existe agora a ferramenta certa:

- `PS3_TRACE_ICALL_TO=0xEA` — apanha o despacho no ponto de resolução, com `r3/r4/r5` e a
  cadeia host simbolizada. Foi assim que se apanharam as duas únicas chamadas a
  `0x002545B0` de uma corrida inteira.
- Sonda na **entrada** da função chamada — o único método que se provou fiável neste lift
  para atribuir uma chamada indirecta. Nem o `lr` do guest nem o rbp frame walk servem
  (ambos me mandaram para funções erradas hoje, cada um custou uma ronda).

**Próxima medição:** `PS3_TRACE_ICALL_TO=0x00220284` — quem despacha para lá com `r3=0`,
e com que `this` as 12 boas foram despachadas. Se as boas vierem de um sítio e a má de
outro, o defeito fica reduzido a um único call site.

## O estado do objectivo

**O menu continua por alcançar.** Gate 0/6, elo `AUTO_LOAD`. O que mudou hoje é que a
investigação deixou de ser "o boot pára algures" e passou a ter quatro sintomas do mesmo
mecanismo, nomeados à função e ao argumento, com contra-exemplos sãos medidos ao lado de
cada um.

---

## Correcção ao enquadramento: o #4 não é despacho indirecto

`PS3_TRACE_ICALL_TO=0x00220284` deu **zero** — `func_00220284` nunca é alcançada por
chamada indirecta. E não tem um único chamador por `bl` no lift. É alcançada por
**tail-call**, de quatro sítios, todos com a mesma forma:

```c
ctx->gpr[3] = ctx->gpr[26];                              // this = r26
{ g_trampoline_fn = (void(*)(void*))func_00220284; return; }
```

Confirmado no PPC original (`0x00221040: mr r3, r26` … `0x00221064: b -> 0x00220284`), e
`0x00220284` é uma entrada de função legítima (`stdu r1,-144(r1)` / `mflr` / `mr r31,r3`) —
não é fragmento mal cortado, ao contrário do que aconteceu com `0x002545D4`.

Logo, para o #4, **o `this=0` é um `r26` que já vinha a zero** dentro de
`func_00220F88`/`func_002210BC`. O `r26` é callee-saved e é reposto da pilha
(`0x00221048: ld r26, 112(r1)`), portanto o valor vem da entrada dessa função.

Isso corrige o enquadramento que escrevi acima: *"é despacho"* é verdade para o #1–#3, mas
**o #4 é um nulo que se propaga por chamadas directas**. A pergunta certa para ele passa a
ser "quem chamou `func_00220F88` com um objecto nulo", não "quem escolheu o alvo".

**Próxima medição para o #4:** sonda na entrada da função de que
`func_00220F88`/`func_002210BC` são fragmentos, registando `r3` — o mesmo padrão do
`[E545B0]` e do `[CPY284]`, que foi o método fiável o dia inteiro.

---

## A cadeia fechada: dois resultados não verificados, na mesma função

Seguindo o `this=0` para trás, verificando **cada passo contra o PPC original**, chega-se a
`func_002182A4`, e os dois sintomas nascem lá, a poucas instruções um do outro:

```c
r9 = (r28 * r9) / r0;  r9 = (r9 << 2) + r11;   // aritmética de índice numa tabela
r3 = *(r9 + 0);                                 // slot
func_00263554(r3);                              // <- SINTOMA #3: aqui r3 chega a valer 5
r28 = r3;                                       // resultado do pop, não verificado
r3  = r26;
func_00227788(r3);
r29 = r3;                                       // <- resultado, NÃO VERIFICADO
r3  = r29;
func_002210BC(r3, ...);                         // <- SINTOMA #4: this = 0 se devolveu 0
```

`func_002210BC` é entrada legítima (prólogo próprio, `rldicl r26,r3` a pôr `r26 = r3`), o
`bl` em `0x00218394` é real (`mr r3, r29` imediatamente antes), e o lift está fiel em todos
os passos — verifiquei um a um no `EBOOT.ELF`. **Nada disto é bug do lifter.**

O que há é um `func_00227788` que devolve `0` e um valor de índice que faz o `pool` valer
`5`. Num PS3 real ambos rebentariam na página nula; aqui propagam-se em silêncio e produzem
tudo o que persegui hoje.

**Próximo actor, sem ambiguidade:** `func_00227788` — porque devolve 0 — e a aritmética
`(r28*r9)/r0` que alimenta o slot do `func_00263554`. Ambos dentro de `func_002182A4`, e
ambos mediveis com uma sonda na entrada, que foi o único método fiável o dia inteiro.

## Um quarto aviso sobre ferramentas de atribuição

O `PS3_TRACE_ICALL_TO` deu **zero** para `func_00220F88` e eu quase escrevi que a função
"não é alcançada por chamada indirecta". Estava errado por omissão: há **dois**
despachantes com o seu próprio `ppu_lookup` — `ps3_indirect_call` (`bctrl`) e
`ps3_indirect_tail` (`bctr`) — e o tracer só estava no primeiro. Corrigido (o campo `via=`
diz agora por qual passou).

É a terceira ferramenta de atribuição que me engana hoje, depois do `lr` do guest e do rbp
frame walk. **Um instrumento que cobre metade dos caminhos mente por omissão, e a omissão
parece prova.**

---

# O fim da cadeia: 237 alocações sãs, e a 238.ª de um pool vazio

Correlação directa, no mesmo log, linhas consecutivas:

```
4535: [SZCLASS] #236 alloc=0x406387E0 idx=10 ea=0x40638894 slot=0x40771A10
4536: [SZCLASS] #237 alloc=0x406387E0 idx=10 ea=0x40638894 slot=0x40771A10
4539: [SZCLASS] #238 alloc=0x400C6B50 idx=0  ea=0x400C6BDC slot=0x00000000  <SLOT-ZERO>
4540: [CPY284]  #13  this=0x00000000 <THIS-MAU>
```

**237 passagens perfeitamente sãs**, todas no alocador `0x406387E0`, com índices de classe
entre 0 e 10 e slots válidos. A 238.ª usa **outro alocador** — `0x400C6B50` — e a sua
free-list de índice 0 está **vazia**.

O pop devolve 0 → `func_00227788` devolve 0 → `func_002182A4` passa-o sem verificar →
`func_002210BC` põe-no em `r26` → `func_00220284` corre com `this = 0` e escreve através
dos campos que lê da base da memória guest.

## O que isto elimina, por medição

- **Não é índice absurdo.** `idx=0` é válido; os 237 anteriores usam 0..10 sem problema.
- **Não é corrupção de memória.** Nenhum watch apanhou uma escrita indevida neste slot, e
  os 237 anteriores provam que o mecanismo funciona.
- **Não é o lifter.** Cada passo desta cadeia foi verificado contra o PPC original.

## O que fica, e é uma frase

**O pool `0x400C6B50` nunca foi abastecido.** É a primeira vez que o código o usa, e a sua
free-list de classe 0 está a zero.

O endereço é revelador: `0x400C6B50` está na mesma vizinhança dos objectos de tipo
(`0x400C3D48`, `0x400C3D88`) — a região que o `F2B-STREAM-PUMP` destruía antes do fix de
hoje. Agora está intacta, mas **vazia**.

## A próxima medição, exacta

`PS3_WATCH_STORE=0x400C6BDC` — a cabeça dessa free-list. Duas respostas possíveis, ambas
úteis:

- **Zero escritas** → o pool nunca foi inicializado; a rotina que o abastece não corre. A
  pergunta passa a ser qual é e porque não corre.
- **Escritas e depois um zero** → foi esvaziado; a pergunta passa a ser por quem.
