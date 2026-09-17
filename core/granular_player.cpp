#include "granular_player.h"

using namespace uirapuru;

void GranularPlayer::Init(float* sample, int size, float sample_rate)
{
    /* guarda os parâmetros nos membros privados */
    sample_      = sample;
    size_        = size;
    sample_rate_ = sample_rate;
    /* inicializa os fasores. phs2_ começa com fase deslocada de 0.5f pra criar a sobreposição dos grãos */
    phs_.Init(sample_rate_, 0, 0);
    phsImp_.Init(sample_rate_, 0, 0);
    phs2_.Init(sample_rate_, 0, 0.5f);
    phsImp2_.Init(sample_rate_, 0, 0);
    /* calcula a frequência do sample inteiro */
    sample_frequency_ = sample_rate_ / size_;
    /* monta a tabela do envelope meio-cosseno */
    for(int i = 0; i < 256; i++)
    {
        cosEnv_[i] = sinf((i / 256.0f) * M_PI);
    }
}

void GranularPlayer::Restart(float* sample, int size)
{
    /* troca o trecho e zera a leitura */
    sample_           = sample;
    size_             = size;
    sample_frequency_ = sample_rate_ / size_;
    /* mesmos fasores do Init, com o phs2_ meia fase adiante pra sobrepor os
     * grãos. A tabela cosEnv_ já está montada, não precisa refazer. */
    phs_.Init(sample_rate_, 0, 0);
    phsImp_.Init(sample_rate_, 0, 0);
    phs2_.Init(sample_rate_, 0, 0.5f);
    phsImp2_.Init(sample_rate_, 0, 0);
}

uint32_t GranularPlayer::WrapIdx(uint32_t idx, uint32_t sz)
{
    /* Traz idx pra dentro de [0, sz).
     *
     * O original subtraía sz no máximo uma vez e usava `>` em vez de `>=`,
     * então idx == sz (e qualquer coisa >= 2*sz) escapava e indexava fora do
     * array. O módulo resolve todos os casos. */
    return sz ? (idx % sz) : 0;
}

float GranularPlayer::CentsToRatio(float cents)
{
    /* converte cents em razão de leitura */
    return powf(2.0f, cents / 1200.0f);
}


float GranularPlayer::MsToSamps(float ms, float samplerate)
{
    /* converte milissegundos em número de amostras */
    return (ms * 0.001f) * samplerate;
}

float GranularPlayer::NegativeInvert(daisysp::Phasor* phs, float frequency)
{
    /* inverte a fase do fasor quando a frequência é negativa, imitando o objeto phasor~ do Pure Data */
    return (frequency > 0) ? phs->Process() : ((phs->Process() * -1) + 1);
}

float GranularPlayer::Process(float speed,
                              float transposition,
                              float grain_size)
{
    grain_size_    = grain_size;
    speed_         = speed * sample_frequency_;
    transposition_ = (CentsToRatio(transposition) - speed)
                     * (grain_size >= 1 ? 1000 / grain_size_ : 1);
    phs_.SetFreq(fabs((speed_ / 2)));
    phs2_.SetFreq(fabs((speed_ / 2)));
    phsImp_.SetFreq(fabs(transposition_));
    phsImp2_.SetFreq(fabs(transposition_));
    idxSpeed_   = NegativeInvert(&phs_, speed_) * size_;
    idxSpeed2_  = NegativeInvert(&phs2_, speed_) * size_;
    idxTransp_  = (NegativeInvert(&phsImp_, transposition_)
                  * MsToSamps(grain_size_, sample_rate_));
    idxTransp2_ = (NegativeInvert(&phsImp2_, transposition_)
                   * MsToSamps(grain_size_, sample_rate_));
    idx_        = WrapIdx((uint32_t)(idxSpeed_ + idxTransp_), size_);
    idx2_       = WrapIdx((uint32_t)(idxSpeed2_ + idxTransp2_), size_);
    /* A tabela de envelope tem 256 posições, mas um fasor devolvendo
     * exatamente 1.0 dá índice 256, uma casa além do fim. Limita. */
    uint32_t e1 = (uint32_t)(phs_.Process() * 256.0f);
    uint32_t e2 = (uint32_t)(phs2_.Process() * 256.0f);
    if(e1 > 255) e1 = 255;
    if(e2 > 255) e2 = 255;
    sig_        = sample_[idx_] * cosEnv_[e1];
    sig2_       = sample_[idx2_] * cosEnv_[e2];
    return (sig_ + sig2_) / 2;
}