#include "pitch_tracker.h"
#include <cmath>

void PitchTracker::Init(float sample_rate) {
    decim_rate_ = sample_rate / kDecim;
    // Anti-alias de um polo em ~45% do Nyquist já dizimado.
    const float fc = decim_rate_ * 0.45f;
    lp_coeff_ = 1.0f - expf(-2.0f * (float)M_PI * fc / sample_rate);
    for (int i = 0; i < kBuf; i++) buf_[i] = 0.0f;
    write_ = filled_ = hop_count_ = decim_count_ = 0;
    lp_ = 0.0f;
    freq_ = conf_ = stable_ = 0.0f;
    hist_w_ = hist_n_ = 0;
}

bool PitchTracker::Process(float x) {
    lp_ += lp_coeff_ * (x - lp_);
    if (++decim_count_ < kDecim) return false;
    decim_count_ = 0;

    buf_[write_] = lp_;
    write_ = (write_ + 1) % kBuf;
    if (filled_ < kBuf) filled_++;

    if (++hop_count_ < kHop) return false;
    hop_count_ = 0;
    if (filled_ < kBuf) return false;

    Analyse();
    return true;
}

void PitchTracker::Analyse() {
    // Copia o buffer circular em ordem linear, do mais antigo primeiro.
    float x[kBuf];
    for (int i = 0; i < kBuf; i++) x[i] = buf_[(write_ + i) % kBuf];

    // Portão de energia: não fica correndo atrás de ruído entre as notas.
    float energy = 0.0f;
    for (int i = 0; i < kWindow; i++) energy += x[i] * x[i];
    energy = sqrtf(energy / kWindow);
    if (energy < 0.003f) {
        conf_ = 0.0f;
        return;  // mantém a frequência anterior; o motor segura a altura
    }

    // Função de diferença do YIN.
    float d[kMaxLag + 1];
    d[0] = 0.0f;
    for (int tau = 1; tau <= kMaxLag; tau++) {
        float sum = 0.0f;
        for (int j = 0; j < kWindow; j++) {
            const float diff = x[j] - x[j + tau];
            sum += diff * diff;
        }
        d[tau] = sum;
    }

    // Normalização pela média acumulada: deixa o limiar independente de escala.
    float dp[kMaxLag + 1];
    dp[0] = 1.0f;
    float running = 0.0f;
    for (int tau = 1; tau <= kMaxLag; tau++) {
        running += d[tau];
        dp[tau] = running > 0.0f ? d[tau] * tau / running : 1.0f;
    }

    // A primeira queda abaixo do limiar ganha (prefere o período verdadeiro
    // em vez dos múltiplos dele); se não achar nenhuma, cai no mínimo global.
    const float kThreshold = 0.15f;
    int best = -1;
    for (int tau = kMinLag; tau <= kMaxLag; tau++) {
        if (dp[tau] < kThreshold) {
            while (tau + 1 <= kMaxLag && dp[tau + 1] < dp[tau]) tau++;
            best = tau;
            break;
        }
    }
    if (best < 0) {
        best = kMinLag;
        for (int tau = kMinLag; tau <= kMaxLag; tau++)
            if (dp[tau] < dp[best]) best = tau;
    }

    // Interpolação parabólica pra ter precisão menor que uma amostra.
    float tau = (float)best;
    if (best > kMinLag && best < kMaxLag) {
        const float a = dp[best - 1], b = dp[best], c = dp[best + 1];
        const float denom = 2.0f * (2.0f * b - a - c);
        if (fabsf(denom) > 1e-9f) tau += (c - a) / denom;
    }

    if (tau > 0.0f) {
        freq_ = decim_rate_ / tau;
        conf_ = 1.0f - dp[best];
        if (conf_ < 0.0f) conf_ = 0.0f;
        if (conf_ > 1.0f) conf_ = 1.0f;

        // Só leitura confiável entra no histórico. Assim um quadro duvidoso
        // deixa a última estimativa boa de pé em vez de trocar ela por um
        // chute.
        if (conf_ >= kConfidence) {
            hist_[hist_w_] = freq_;
            hist_w_ = (hist_w_ + 1) % kHist;
            if (hist_n_ < kHist) hist_n_++;

            float sorted[kHist];
            for (int i = 0; i < hist_n_; i++) sorted[i] = hist_[i];
            for (int i = 1; i < hist_n_; i++) {
                const float key = sorted[i];
                int j = i - 1;
                while (j >= 0 && sorted[j] > key) { sorted[j + 1] = sorted[j]; j--; }
                sorted[j + 1] = key;
            }
            stable_ = sorted[hist_n_ / 2];
        }
    }
}
