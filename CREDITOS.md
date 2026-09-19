# Créditos das gravações

O motor funciona com qualquer gravação. Estas três vêm junto pra que a linha de
comando rode logo depois do clone, e são a demonstração do efeito, não parte
dele.

## assets/uirapuru.wav

Canto do uirapuru-verdadeiro (*Cyphorhinus arada*).

- Gravação: PREENCHER
- Licença: PREENCHER

## assets/mau.wav

Canto do Mau, o pássaro grave desta combinação.

- Gravação: PREENCHER
- Licença: PREENCHER

## assets/kiskadee.wav

Canto do bem-te-vi (*Pitangus sulphuratus*), recortado da gravação XC1163691 do
[xeno-canto](https://xeno-canto.org/1163691).

- Gravação: PREENCHER o nome de quem gravou
- Licença: PREENCHER a licença que consta na página da gravação

O trecho em uso tem 1,43 s, recortado e normalizado. O processo está em
`tools/prepara_bemtevi.py` no repositório do plugin, que registra por medição
qual trecho foi escolhido e por quê.

## Trocar os cantos

Qualquer WAV mono serve. Meça a altura mediana do seu e ajuste `casa_hz` no
motor, senão a transposição fica medida contra a referência errada. O README
explica o formato esperado.
