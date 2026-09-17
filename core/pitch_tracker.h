#pragma once

/** Detector de altura monofônico (função de diferença, estilo YIN).
 *
 * Só dá conta de uma nota por vez: nota isolada ele acerta bonito, acorde ele
 * não resolve. Isso é normal em pedal de pitch shift, não é gambiarra nossa.
 *
 * Pra caber no processador da Daisy ele dizima o sinal em 4x antes de analisar.
 * Sobra faixa de sobra pros ~80-1000 Hz da guitarra e o trabalho cai umas 16
 * vezes.
 */
class PitchTracker {
  public:
    void Init(float sample_rate);

    /** Joga uma amostra pra dentro. Devolve true quando saiu estimativa nova. */
    bool Process(float x);

    /** Última fundamental detectada em Hz, ou 0 quando ninguém está tocando. */
    float frequency() const { return freq_; }

    /** 0..1, o quanto ele confia no que achou. Valor baixo é ruído ou silêncio. */
    float confidence() const { return conf_; }

    /** Mediana das últimas leituras *confiáveis*.
     *
     * Use esta, e não frequency(), pra controlar qualquer coisa musical. Uma
     * leitura sozinha pode estar errada, principalmente em acorde, onde um
     * detector monofônico fica pulando entre as notas, e tirar a mediana das
     * últimas joga esses pontos fora em vez de repassar como salto de altura.
     */
    float stable_frequency() const { return stable_; }

    /** True quando já chegaram leituras confiáveis suficientes pra valer a mediana. */
    bool stable_ready() const { return hist_n_ >= kHist; }

  private:
    static constexpr int kDecim  = 4;    // 48k -> 12k
    static constexpr int kWindow = 512;  // janela de análise, em amostras dizimadas
    static constexpr int kMaxLag = 200;  // piso de ~60 Hz a 12 kHz
    static constexpr int kMinLag = 10;   // teto de ~1200 Hz
    static constexpr int kHop    = 128;  // de quanto em quanto ele analisa
    static constexpr int kBuf    = kWindow + kMaxLag;

    void Analyse();

    float buf_[kBuf] = {0};
    int   write_ = 0;
    int   filled_ = 0;
    int   hop_count_ = 0;

    // Filtro anti-alias + contador da dizimação
    float lp_ = 0.0f;
    float lp_coeff_ = 0.0f;
    int   decim_count_ = 0;

    float decim_rate_ = 12000.0f;
    float freq_ = 0.0f;
    float conf_ = 0.0f;

    // Filtro de mediana sobre as leituras confiáveis recentes.
    static constexpr int   kHist       = 5;
    static constexpr float kConfidence = 0.55f;
    float hist_[kHist] = {0};
    int   hist_w_ = 0;
    int   hist_n_ = 0;
    float stable_ = 0.0f;
};
