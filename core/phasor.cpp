/*
Copyright (c) 2020 Electrosmith, Corp

Use of this source code is governed by an MIT-style
license that can be found in the LICENSE file or at
https://opensource.org/licenses/MIT.
*/

#include <math.h>

#include "phasor.h"

using namespace daisysp;

// No original isto vinha do dsp.h da biblioteca. E' a unica constante que o
// Phasor usa, entao vem junto em vez de arrastar o cabecalho inteiro.
#define TWOPI_F (2.0f * (float)M_PI)

void Phasor::SetFreq(float freq)
{
    freq_ = freq;
    inc_  = (TWOPI_F * freq_) / sample_rate_;
}

float Phasor::Process()
{
    float out;
    out = phs_ / TWOPI_F;
    phs_ += inc_;
    if(phs_ > TWOPI_F)
    {
        phs_ -= TWOPI_F;
    }
    if(phs_ < 0.0f)
    {
        phs_ = 0.0f;
    }
    return out;
}
