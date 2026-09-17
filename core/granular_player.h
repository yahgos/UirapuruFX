/*
Copyright (c) 2020 Electrosmith, Corp, Vinícius Fernandes

Use of this source code is governed by an MIT-style
license that can be found in the LICENSE file or at
https://opensource.org/licenses/MIT.
*/

#pragma once
#ifndef UIRAPURU_GRANULARPLAYER_H
#define UIRAPURU_GRANULARPLAYER_H

#include <stdint.h>
#include <cmath>
#include "phasor.h"
#ifdef __cplusplus
#ifndef M_PI
#define M_PI 3.14159265358979323846 /* pi */
#endif

namespace uirapuru
{
// Cópia corrigida do daisysp::GranularPlayer.
//
// A versão original lê um elemento além do fim tanto do array do sample quanto
// da tabela de envelope. Isso devolve o que estiver na memória vizinha, e de vez
// em quando um valor gigante, que sai como um estouro seco no alto-falante.
// Confirmado com AddressSanitizer:
//   heap-buffer-overflow, READ of size 4, 0 bytes after the sample buffer.
// As correções estão no WrapIdx() e na leitura do envelope; fora isso o som é
// exatamente o mesmo.
/** GranularPlayer Module

    Date: November, 2023

    Author: Vinícius Fernandes

    O GranularPlayer toca uma tabela permitindo esticar o tempo e transpor a
    altura de forma independente, via granulação.
    Inspirado no objeto grain.player da biblioteca else do Pure Data.
*/

class GranularPlayer
{
  public:
    GranularPlayer() {}
    ~GranularPlayer() {}

    /** Inicializa o módulo GranularPlayer.
        \param sample ponteiro pro sample que vai ser tocado
        \param size quantidade de elementos no array do sample
        \param sample_rate taxa de amostragem do motor de áudio
    */
    void Init(float* sample, int size, float sample_rate);

    /** Processa o granular.
        \param speed velocidade de leitura. 1 é normal, 2 é o dobro, 0.5 é a metade. Valor negativo toca o sample de trás pra frente.
        \param transposition transposição em cents. 100 cents é um semitom. Negativo desce, positivo sobe.
        \param grain_size tamanho do grão em milissegundos. 1 é um milissegundo, 1000 é um segundo. Não aceita negativo. O mínimo é 1.
    */
    float Process(float speed, float transposition, float grain_size);

    /** Aponta a leitura pra outro trecho e volta pro começo dele.

        Serve pro modo frase: em vez de varrer o arquivo inteiro sem parar, o
        motor manda o granular tocar UMA frase do canto, do início ao fim.

        É o Init() sem a parte cara: não remonta a tabela do envelope (são 256
        cosf), só reinicia os quatro fasores e recalcula a frequência do
        trecho. Tudo o que o Process() usa sai dos fasores, então isso basta,
        e pode ser chamado de dentro da thread de áudio.

        \param sample ponteiro pro início do trecho
        \param size quantas amostras o trecho tem
    */
    void Restart(float* sample, int size);

  private:
    // Traz um índice pra dentro do tamanho do array do sample
    uint32_t WrapIdx(uint32_t idx, uint32_t size);

    // Converte cents (centésimo de semitom) em razão de leitura
    float CentsToRatio(float cents);

    // Converte milissegundos em número de amostras
    float MsToSamps(float ms, float samplerate);

    // Inverte a fase do fasor quando a frequência é negativa, imitando o phasor~ do Pure Data
    float NegativeInvert(daisysp::Phasor* phs, float frequency);


    float* sample_;        // ponteiro pro sample que vai ser tocado
    float  sample_rate_;   // taxa de amostragem do motor de áudio
    int    size_;          // quantidade de elementos no array do sample
    float  grain_size_;    // tamanho do grão em milissegundos
    float  speed_;         //processed playback speed.
    float  transposition_; //processed transpotion.
    float  sample_frequency_;
    float  cosEnv_[256] = {0}; //cosine envelope for crossfading between grains
    float
        idxTransp_; // Adjusted Transposition value contribution to idx of first grain
    float
          idxTransp2_; // Adjusted Transposition value contribution to idx of second grain
    float idxSpeed_; // Adjusted Speed value contribution to idx of first grain
    float
          idxSpeed2_; // Adjusted Speed value contribution to idx of second grain
    float sig_;       // Output of first grain
    float sig2_;      // Output of second grain

    uint32_t idx_;  // Index of first grain
    uint32_t idx2_; // Index of second grain

    daisysp::Phasor phs_;     // Phasor for speed
    daisysp::Phasor phsImp_;  // Phasor for transposition
    daisysp::Phasor phs2_;    // Phasor for speed
    daisysp::Phasor phsImp2_; // Phasor for transposition
};
} // namespace uirapuru
#endif
#endif