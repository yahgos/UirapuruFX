# UirapuruFX

Um efeito de guitarra que responde ao que você toca com canto de pássaro.

Você entrega uma gravação de canto. O efeito granula esse arquivo e transpõe o
resultado pra acompanhar a nota que você acabou de tocar, misturando por baixo
da guitarra seca, sempre no tom.

O nome vem do uirapuru-verdadeiro (*Cyphorhinus arada*), o pássaro amazônico
que Villa-Lobos usou no poema sinfônico de 1917.

Este repositório tem o motor de DSP e o renderizador de linha de comando. O
plugin de Audio Unit e o firmware do pedal vêm depois.

## Como ele fica no tom

O pássaro canta a nota que você tocou, algumas oitavas acima. Para a nota
detectada `f` e um deslocamento de `K` oitavas, a transposição em cents contra a
altura de referência da gravação é:

```
cents = 1200 * log2(f * 2^K / f_referencia)
```

**O `K` é sempre um número inteiro, e é daí que vem o "sempre no tom".** Oitava
inteira não muda a classe da nota, então o pássaro cai exatamente na nota que
você tocou, algumas oitavas acima. Isso vale em toda a extensão do braço, sem
exceção.

### O `K` não é constante, e isso tem um preço

O valor pedido é 2, mas o teto de 1850 Hz pode reduzir ele:

```
K = min(2, floor(log2(1850 / f)))
```

Quando o pássaro passaria do teto, ele desce uma oitava inteira em vez de
continuar subindo. Medindo a saída real, nota por nota:

| nota tocada | multiplicador | pássaro vai de |
|---|---|---|
| E2 a A4 | x4 | 330 a 1760 Hz |
| A#4 a A5 | x2 | 932 a 1760 Hz |
| A#5 pra cima | x1 | 932 Hz pra cima |

A consequência, dita sem rodeio: **subir o braço não garante pássaro mais agudo.**
Em A#4 e de novo em A#5, você sobe um semitom e o pássaro cai onze. Dentro de
cada faixa da tabela ele sobe junto com você, mas nas duas fronteiras ele volta.

O teto existe porque a gravação só soa como pássaro quando puxada pra baixo.
Acima dele o espectro estica pra uma faixa onde o arquivo quase não tem energia,
e o som fica fino e sibilante. A escolha foi dobrar pra baixo em vez de ficar
fino, e o custo é essa quebra.

`--no-ceiling` desliga o teto e o mapeamento passa a subir sempre, ao preço de
trazer o som fino de volta nas notas agudas.

## Por que granular, e não um sampler

Sampler comum amarra velocidade e altura: acelere a frase e ela sobe de tom.
Isso não serve aqui, porque as duas coisas precisam ser controladas em
separado. A velocidade é escolha sua. A altura tem que obedecer à guitarra.

A síntese granular resolve isso lendo o arquivo com dois relógios
independentes: um caminha pela gravação e define a velocidade, o outro lê dentro
de cada grão e define a altura. O knob de velocidade só muda o quão rápido a
frase corre, e a altura só acompanha o que você toca.

## Compilar

Precisa de `make` e de um compilador com C++17. Nada mais: tudo o que o motor
usa está dentro de `core/`.

```sh
make
```

Para compilar com AddressSanitizer e UBSan:

```sh
make asan
```

## Usar

O efeito precisa de uma gravação de canto de pássaro. O repositório não traz
nenhuma: gravação tem licença, e a licença depende de onde o arquivo veio.
Consiga a sua e aponte o caminho.

O que o arquivo precisa ter:

| propriedade | o que funciona bem |
|---|---|
| formato | WAV mono, PCM de 16, 24 ou 32 bits, ou float de 32 |
| taxa | a mesma da guitarra, tipicamente 48 kHz |
| duração | de 1 a 10 segundos, uma ou mais frases do canto |
| conteúdo | canto limpo, com pouco ruído de fundo e sem estourar |

O [xeno-canto](https://xeno-canto.org) tem um acervo grande de cantos com
licença declarada em cada gravação. Confira a licença da que você baixar.

Com o arquivo em mão:

```sh
PASSARO=caminho/pro/canto.wav

# Com uma gravação sua de guitarra:
./build/uirapuru render $PASSARO guitarra.wav saida.wav --mix 0.55

# Com uma escala sintética, se ainda não tem gravação de guitarra:
./build/uirapuru render $PASSARO --synth saida.wav --mix 0.55

# Só o pássaro, sem a guitarra seca, pra ouvir o efeito isolado:
./build/uirapuru render $PASSARO --synth saida.wav --tone 110 --mix 1.0
```

Rode `./build/uirapuru` sem argumento pra lista completa de opções.

### Antes de usar um arquivo novo

A transposição é medida contra a altura mediana da gravação, e o padrão
(1828 Hz) vale pro arquivo que eu uso. Com outro canto, meça a mediana do seu e
ajuste:

```sh
./build/uirapuru render $PASSARO --synth saida.wav --ref 2400
```

Sem isso o pássaro fica afinado em relação à referência errada, e o efeito
desafina junto com a guitarra.

## Controles

| opção | o que faz |
|---|---|
| `--mix <0..1>` | guitarra seca contra pássaro |
| `--speed <x>` | ritmo da frase; 1 é a velocidade natural, negativo toca de trás pra frente |
| `--grain <ms>` | tamanho do grão; pequeno deixa tagarela, grande deixa um colchão |
| `--ref <hz>` | altura mediana da sua gravação |
| `--octaves <n>` | oitavas acima da sua nota |
| `--latch` / `--glide` / `--step` | como o pássaro reage a bend e slide |
| `--calls` | modo frase: uma chamada inteira por ataque, depois descanso |
| `--bird2 <wav>` | um segundo canto, sorteado por nota |

## Modo frase

Por padrão o pássaro canta sem parar, e o seu toque só abre e fecha o volume
dele. Isso vira um muro de som, porque canto de pássaro quase não tem silêncio:
medindo as gravações que eu uso, 90% do arquivo tem som. Toque rápido e as
frases se emendam umas nas outras.

O `--calls` troca isso por chamada e resposta. Cada ataque dispara uma frase
inteira, de onde o pássaro começou até onde ele terminou, e depois o pássaro
descansa. Ataque que chega durante a chamada ou durante o descanso é ignorado.

As fronteiras das frases são detectadas no carregamento, não escritas no
código: o motor segue o envelope, corta onde ele cai abaixo de 6% do pico, e
junta pedaços separados por menos de 60 ms. Troque o arquivo e continua
funcionando. `uirapuru phrases <arquivo.wav>` mostra a tabela que ele achou.

### O que isso conserta

O modo contínuo tentava decidir nota por nota, e o detector de ataque não dava
conta. Contando ataques numa corrida de 32 notas:

| notas por segundo | 1 | 2 | 3 | 4 | 6 | 8 |
|---|---|---|---|---|---|---|
| com as constantes do modo contínuo (40 ms / 250 ms) | 32 | 24 | 1 | 1 | 1 | 1 |
| com as constantes do modo frase (12 ms / 25 ms) | 32 | 32 | 32 | 32 | 32 | 32 |

Acima de duas notas por segundo o detector do modo contínuo fica surdo, e o
pássaro trava numa altura e zune pela corrida inteira. O modo frase usa um
detector mais rápido, e pode se dar a esse luxo porque ali disparo falso não
custa nada: o portão engole. Até 28 ataques por trecho são ignorados sem nenhum
efeito audível, dependendo do quanto você toca.

O resultado é uma taxa de chamadas que quase não muda com a sua velocidade:

| entrada | chamadas por segundo |
|---|---|
| nota sustentada | 0,25 |
| acorde aberto | 0,50 |
| escala | 0,50 |
| corrida a 3 notas/s | 0,59 |
| corrida a 6 notas/s | 0,77 |
| corrida a 8 notas/s | 0,77 |

O tempo com o pássaro audível cai por volta da metade. Medido na saída, com o
pássaro isolado:

| entrada | contínuo | frase |
|---|---|---|
| nota sustentada | 91% | 13% |
| acorde aberto | 72% | 38% |
| escala | 90% | 44% |
| corrida a 3 notas/s | 85% | 52% |
| corrida a 6 notas/s | 81% | 58% |
| corrida a 8 notas/s | 78% | 39% |

### O que se perde

O modo frase abre mão do envelope de dinâmica ao vivo. A força da palhetada
define o volume da chamada, medida nos primeiros 50 ms, e daí a chamada segura
aquilo até terminar. Abafe a corda e o pássaro canta até o fim. É o ponto do
modo, e é por isso que ele vem desligado.

## O que é fixo, e por quê

Alguns valores não são parâmetros. São a característica do pedal, e foram
decididos tocando:

| fixo | valor | por quê |
|---|---|---|
| oitavas acima | 2 | acima disso fica estranho |
| teto do pássaro | 1850 Hz | acima daqui ele desce uma oitava inteira em vez de ficar fino |
| seguir a altura | sempre ligado | sem isso não existe efeito, é só um sample tocando |
| altura em bends | trava | o pássaro prende na nota quando o detector assenta, por volta de 130 ms, e segura durante bend, slide e vibrato |
| dinâmica | sempre no máximo | o pássaro respira junto com o toque |

**Por que a trava existe.** Quantizando em semitons, um bend de um tom fazia o
pássaro dar três saltos de 100 cents, e o vibrato fazia ele tremer cruzando a
fronteira do semitom. A trava acompanha ao vivo enquanto o detector assenta e
então congela, então o pássaro fica estável exatamente onde importa: no corpo
sustentado da nota. `--glide` e `--step` estão na linha de comando pra comparar.

**Por que o teto existe.** A gravação só soa como pássaro quando puxada pra
baixo. Subir estica o espectro pra uma faixa onde a gravação quase não tem
energia, e o resultado fica fino e sibilante. Acima de 1850 Hz o pássaro desce
uma oitava inteira, que mantém a mesma classe de nota e não sai do tom.

Esses valores moram em `BirdEngine::Params`, em
[core/bird_engine.h](core/bird_engine.h), que é a fonte única. A linha de
comando ainda aceita `--octaves`, `--no-follow` e `--dynamics` de propósito:
ela é a bancada de teste, e foi com essas opções que a afinação foi conferida.

## O que está verificado

Cada número abaixo saiu de uma medição, não de impressão de ouvido.

- Detector de altura: 8 de 8 notas de teste, de E2 a E5, dentro de 7 cents.
- Transposição: 1200 cents por oitava **abaixo do teto**, medido de A2 a A4.
  Passando o teto a conta muda, porque o pássaro dobra pra baixo: de A4 pra A5 o
  deslocamento mede 10 cents, não 1200.
- O mapeamento fica sempre no tom, em toda a extensão do braço.
- O mapeamento **não** é monotônico: com o teto ligado o pássaro desce onze
  semitons em A#4 e de novo em A#5. Medido, e é consequência conhecida do teto.
- O detector de altura satura perto de 1200 Hz. Acima disso ele trava no
  subharmônico, o que atinge só as últimas casas de um braço de 24 trastes.
- Detecção de ataque: 400 ataques contados em 400 notas tocadas.
- Segmentação de frases: confere com uma ferramenta de medida independente.
- Modo frase: nenhuma descontinuidade amostra a amostra em nenhum render.
- Modo frase soa mais alto que o contínuo, de 1,1 a 1,9 vezes conforme o que
  você toca. Isso não é proposital: a constante de casamento foi calibrada antes
  do ganho de correção do bem-te-vi entrar, e o ganho desequilibrou os dois
  modos de novo. Vale recalibrar antes de fechar o modo frase.
- Segundo pássaro em zero: saída byte a byte idêntica a não ter segundo pássaro.
- 112 combinações sob AddressSanitizer e UBSan, incluindo casos extremos de
  espaçamento e velocidades de 0,02 a 2,0. Limpo.
- Toda opção e comando da ajuda foram executados: 41 opções e 6 comandos.

## Estrutura

```
core/
  bird_engine.{h,cpp}      o efeito: transposição, mistura, sorteio, modo frase
  pitch_tracker.{h,cpp}    detector de altura monofônico, estilo YIN
  granular_player.{h,cpp}  leitura granular, com duas correções de acesso fora de limite
  phasor.{h,cpp}           oscilador de rampa usado pelo granular
cli/
  main.cpp                 renderizador e bancada de testes
  wav.{h,cpp}              leitura e escrita de WAV mono
```

O `core/` não depende do `cli/` e não usa nada além da biblioteca padrão, então
o mesmo código serve pro renderizador, pro plugin e pro firmware.

## Código de terceiros

`granular_player.{h,cpp}` e `phasor.{h,cpp}` vêm da biblioteca
[DaisySP](https://github.com/electro-smith/DaisySP), da Electrosmith, sob
licença MIT. Os avisos de copyright originais estão nos arquivos.

O `GranularPlayer` traz duas correções em relação ao original. Ele lia uma
posição além do fim do array do sample e uma além do fim da tabela de envelope,
o que devolvia o que estivesse na memória vizinha e às vezes saía como um
estouro seco no alto-falante. O AddressSanitizer confirmou:
`heap-buffer-overflow`, leitura de 4 bytes logo após o buffer. As correções
estão no `WrapIdx()` e na leitura do envelope, e fora isso o som é o mesmo.

## Licença

MIT. Veja [LICENSE](LICENSE).
