/*
Copyright (c) 2020 Electrosmith, Corp

Use of this source code is governed by an MIT-style
license that can be found in the LICENSE file or at
https://opensource.org/licenses/MIT.
*/

#pragma once
#ifndef UIRAPURU_PHASOR_H
#define UIRAPURU_PHASOR_H

// Copia do daisysp::Phasor, trazida pra dentro do projeto.
//
// O original mora na biblioteca DaisySP, e era a unica coisa que fazia este
// repositorio depender dela. Sao 27 linhas de codigo e uma constante, contra
// uma biblioteca inteira que quem clonasse teria que baixar e apontar o
// caminho. Trazer pra dentro deixa o `make` funcionar sem nenhum preparo.
//
// O codigo e' o mesmo, sem alteracao nenhuma. O aviso de licenca acima e' do
// autor original e fica como esta'.

namespace daisysp
{
/** Gera um sinal que caminha de 0 a 1 na frequencia pedida. */
class Phasor
{
  public:
    Phasor() {}
    ~Phasor() {}

    /** Inicializa. A taxa de amostragem e a frequencia sao em Hz, e a fase
        inicial em radianos. */
    inline void Init(float sample_rate, float freq, float initial_phase)
    {
        sample_rate_ = sample_rate;
        phs_         = initial_phase;
        SetFreq(freq);
    }

    inline void Init(float sample_rate, float freq)
    {
        Init(sample_rate, freq, 0.0f);
    }

    inline void Init(float sample_rate) { Init(sample_rate, 1.0f, 0.0f); }

    /** Avanca uma amostra e devolve o valor atual. */
    float Process();

    /** Ajusta a frequencia, em Hz. */
    void SetFreq(float freq);

    inline float GetFreq() { return freq_; }

  private:
    float freq_;
    float sample_rate_, inc_, phs_;
};
} // namespace daisysp
#endif
