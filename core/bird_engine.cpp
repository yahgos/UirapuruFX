#include "bird_engine.h"

#include <cmath>

namespace {
/** Limitador de segurança.
 *
 * Transparente abaixo de 0.9, toque normal passa intacto, e daí pra cima
 * satura suave, em vez de deixar alguma coisa chegar no alto-falante a todo
 * volume.
 */
inline float SoftClip(float x)
{
    const float t = 0.9f;
    if(x > t) return t + (1.0f - t) * tanhf((x - t) / (1.0f - t));
    if(x < -t) return -t + (1.0f - t) * tanhf((x + t) / (1.0f - t));
    return x;
}
} // namespace

void BirdEngine::SegmentaFrases(const std::vector<float>& s,
                                float                     sr,
                                float                     min_ms,
                                std::vector<Frase>&       out)
{
    out.clear();
    if(s.empty()) return;

    // Envelope: sobe em 2 ms, cai em 30 ms. A queda é o que decide se dois
    // pedaços contam como uma frase ou duas: rápida demais e o trinado se
    // parte todo, lenta demais e o arquivo inteiro vira uma frase só.
    const float atk = 1.0f - expf(-1.0f / (0.002f * sr));
    const float rel = 1.0f - expf(-1.0f / (0.030f * sr));

    std::vector<float> env(s.size());
    float e = 0.0f, pico = 0.0f;
    for(size_t i = 0; i < s.size(); i++)
    {
        const float r = fabsf(s[i]);
        e += (r > e ? atk : rel) * (r - e);
        env[i] = e;
        if(e > pico) pico = e;
    }
    if(pico <= 0.0f) return;

    const float lim     = 0.06f * pico;                 // 6% do pico é "tem som"
    const int   gap     = (int)(0.060f * sr);           // junta pausas < 60 ms
    const int   min_len = (int)(min_ms * 0.001f * sr);

    bool dentro = false;
    int  ini = 0, silencio = 0;
    for(size_t i = 0; i < env.size(); i++)
    {
        if(env[i] > lim)
        {
            if(!dentro) { dentro = true; ini = (int)i; }
            silencio = 0;
        }
        else if(dentro && ++silencio >= gap)
        {
            const int fim = (int)i - silencio;
            if(fim - ini >= min_len) out.push_back({ini, fim - ini});
            dentro = false;
        }
    }
    if(dentro)
    {
        const int fim = (int)env.size();
        if(fim - ini >= min_len) out.push_back({ini, fim - ini});
    }

    // Se nada passou do piso (arquivo curtinho, ou piso alto demais), usa o
    // arquivo inteiro como uma frase. O modo frase nunca pode ficar mudo.
    if(out.empty()) out.push_back({0, (int)s.size()});
}

int BirdEngine::SorteiaFrase(int bird)
{
    const std::vector<Frase>& f = (bird == 2 ? frases2_ : frases_);
    const int n = (int)f.size();
    if(n <= 1) return 0;

    // Sorteia, e se cair na mesma de antes anda uma casa. Repetir a mesma
    // frase duas vezes seguidas denuncia o sample na hora.
    int i = (int)(NextRandom() * (float)n);
    if(i >= n) i = n - 1;
    if(i == frase_ant_) i = (i + 1) % n;
    return i;
}

void BirdEngine::ComecaChamada()
{
    const int bird = want_bird2_ ? 2 : 1;
    const std::vector<Frase>& f = (bird == 2 ? frases2_ : frases_);
    const std::vector<float>& amostras = (bird == 2 ? bird2_ : bird_);
    if(f.empty() || amostras.empty()) return;

    const int i = SorteiaFrase(bird);
    frase_ant_  = i;

    // Apara pelo teto, contado no ARQUIVO. Assim o teto significa a mesma
    // coisa em qualquer velocidade.
    int len = f[i].len;
    const int teto = (int)(p_.frase_max_ms * 0.001f * sr_);
    if(teto > 0 && len > teto) len = teto;

    // Aponta o granular pro trecho. O size dele passa a ser o da frase, então
    // uma volta do fasor de posição é exatamente uma passada pela frase.
    uirapuru::GranularPlayer& g = (bird == 2 ? gran2_ : gran_);
    g.Restart(const_cast<float*>(amostras.data()) + f[i].ini, len);

    // Quanto tempo de saída isso dá: a frase tem len amostras de arquivo e é
    // lida a `speed`, então demora len/speed. Com speed < 1 estica.
    float v = fabsf(p_.speed);
    if(v < 0.02f) v = 0.02f;   // não deixa a chamada durar pra sempre
    canto_restante_ = (int)((float)len / v);

    // Com o portão fechado dá pra trocar de pássaro na hora, sem crossfade:
    // não tem o que estalar se não está saindo som.
    xfade_    = want_bird2_ ? 1.0f : 0.0f;
    cantando_ = true;
    descanso_ = 0;
}

void BirdEngine::Init(float sample_rate, const float* bird, int bird_len) {
    sr_ = sample_rate;
    bird_.assign(bird, bird + bird_len);
    gran_.Init(bird_.data(), (int)bird_.size(), sample_rate);
    pitch_.Init(sample_rate);

    // Glissando de ~60 ms na transposição: rápido o bastante pra responder
    // bem, lento o bastante pra não ficar saltando a cada detecção.
    cents_coeff_ = 1.0f - expf(-1.0f / (0.060f * sr_));
    env_atk_     = 1.0f - expf(-1.0f / (0.005f * sr_));
    env_rel_     = 1.0f - expf(-1.0f / (0.250f * sr_));

    // Detecção de ataque: envelope rápido (1 ms) contra limiar lento (30 ms
    // subindo, 250 ms caindo).
    on_fast_atk_ = 1.0f - expf(-1.0f / (0.001f * sr_));
    thresh_atk_  = 1.0f - expf(-1.0f / (0.030f * sr_));
    AtualizaCoefsAtaque();

    // Crossfade de ~20 ms na troca de pássaro: curto o suficiente pra parecer
    // instantâneo, longo o suficiente pra não estalar.
    xfade_coeff_ = 1.0f - expf(-1.0f / (0.020f * sr_));

    // Portão da chamada: 12 ms. Precisa de rampa senão o começo e o fim da
    // frase estalam; 12 ms não é audível como fade, só mata o clique.
    gate_coeff_ = 1.0f - expf(-1.0f / (0.012f * sr_));

    // Acha as frases do canto. Aloca, então é aqui e não no Process.
    SegmentaFrases(bird_, sr_, p_.frase_min_ms, frases_);
    min_ms_usado_ = p_.frase_min_ms;

    cents_smooth_ = 0.0f;
    env_          = 0.0f;
    have_pitch_   = false;
    refract_      = 0;
    latched_cents_ = 0.0f;
    congela_em_    = 0;
    congelado_     = false;
    on_fast_ = thresh_ = 0.0f;
    want_bird2_   = false;
    xfade_        = 0.0f;
    onsets_       = 0;
    picks2_       = 0;
    ignorados_    = 0;
    cantando_       = false;
    canto_restante_ = 0;
    descanso_       = 0;
    frase_ant_      = -1;
    gate_           = 0.0f;
    nivel_canto_    = 1.0f;
    mede_nivel_     = 0;
}

void BirdEngine::AtualizaCoefsAtaque()
{
    // O modo de disparo escolhe o par. Ver o comentário em Params.
    const bool frase = (p_.modo_disparo == Params::FRASE);
    const float env_ms = frase ? p_.onset_env_frase_ms : p_.onset_env_rel_ms;
    const float lim_ms = frase ? p_.onset_rel_frase_ms : p_.onset_rel_ms;
    on_fast_rel_ = 1.0f - expf(-1.0f / (0.001f * env_ms * sr_));
    thresh_rel_  = 1.0f - expf(-1.0f / (0.001f * lim_ms * sr_));
}

void BirdEngine::SetParams(const Params& p)
{
    const bool piso_mudou = (p.frase_min_ms != min_ms_usado_);
    p_ = p;
    AtualizaCoefsAtaque();
    if(piso_mudou)
    {
        SegmentaFrases(bird_, sr_, p_.frase_min_ms, frases_);
        if(!bird2_.empty()) SegmentaFrases(bird2_, sr_, p_.frase_min_ms, frases2_);
        min_ms_usado_ = p_.frase_min_ms;
        frase_ant_    = -1;
    }
}

void BirdEngine::SetSecondBird(const float* bird, int bird_len)
{
    bird2_.assign(bird, bird + bird_len);
    gran2_.Init(bird2_.data(), (int)bird2_.size(), sr_);

    // A frequência alvo é a mesma pros dois pássaros, então a diferença de
    // cents entre eles é uma constante: só as referências (e a oitava extra)
    // mudam. Guardar como deslocamento fixo garante que eles nunca desafinem um
    // em relação ao outro, por mais que o alvo se mexa.
    //
    //   cents2 = cents1 + 1200*log2(ref1/ref2) + 1200*bonus
    cents2_offset_ = 1200.0f * log2f(p_.bird_ref_hz / p_.bird2_ref_hz)
                     + 1200.0f * (float)p_.bird2_octave_bonus;

    SegmentaFrases(bird2_, sr_, p_.frase_min_ms, frases2_);
    min_ms_usado_ = p_.frase_min_ms;
}

float BirdEngine::NextRandom()
{
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return (float)(rng_ >> 8) * (1.0f / 16777216.0f); // 0..1
}

float BirdEngine::ChanceOfBird2(float played_hz) const
{
    if(p_.bird2_chance <= 0.0f) return 0.0f;

    // Onde estamos no braço, de 0 (grave) a 1 (agudo). Interpolado em log2, ou
    // seja, por oitavas, é assim que o ouvido percebe altura.
    float t = 0.5f; // sem altura detectada, assume o centro
    if(played_hz > 0.0f)
    {
        const float span = log2f(p_.chance_f_hi / p_.chance_f_lo);
        if(span > 0.0f)
        {
            t = log2f(played_hz / p_.chance_f_lo) / span;
            if(t < 0.0f) t = 0.0f;
            if(t > 1.0f) t = 1.0f;
        }
    }

    // O knob dá o valor no centro; a inclinação distribui em volta dele.
    const float tilt = p_.bird2_tilt_lo + (p_.bird2_tilt_hi - p_.bird2_tilt_lo) * t;
    float c = p_.bird2_chance * tilt;
    if(c < 0.0f) c = 0.0f;
    if(c > 1.0f) c = 1.0f;
    return c;
}

void BirdEngine::Retrigger() {
    gran_.Restart(bird_.data(), (int)bird_.size());
    if(!bird2_.empty()) gran2_.Restart(bird2_.data(), (int)bird2_.size());
    cantando_       = false;
    canto_restante_ = 0;
    descanso_       = 0;
    frase_ant_      = -1;
    gate_           = 0.0f;
}

float BirdEngine::TargetCents(float played_hz) const {
    if(played_hz <= 0.0f) return 0.0f;

    float hz = played_hz;
    if(p_.modo_altura == Params::DEGRAU)
    {
        // Trava no semitom mais próximo, assim uma oscilaçãozinha na altura
        // detectada não faz o pássaro tremer.
        const float midi = roundf(69.0f + 12.0f * log2f(hz / 440.0f));
        hz               = 440.0f * powf(2.0f, (midi - 69.0f) / 12.0f);
    }

    // Canta a nota que você acabou de tocar, um número fixo de oitavas acima.
    //
    // O detalhe de serem oitavas inteiras é o que importa: assim o pássaro fica
    // exatamente na mesma classe de nota que a sua, e nunca sai do tom. E como
    // o deslocamento é fixo em vez de dobrado dentro de um registro, nota mais
    // aguda sempre dá pássaro mais agudo, não tem aquele efeito de voltar pra
    // trás quando você sobe no braço.
    int k = p_.octave_offset;
    if(p_.ceiling_hz > 0.0f && hz > 0.0f)
    {
        // Quantas oitavas ainda cabem debaixo do teto. Se forem menos do que o
        // deslocamento pedido, desce de oitava em oitava até caber, e como é
        // oitava inteira, continua na mesma nota que você tocou.
        const int k_max = (int)floorf(log2f(p_.ceiling_hz / hz));
        if(k_max < k) k = k_max;
        if(k < 0) k = 0; // no limite, o pássaro canta a sua própria nota
    }

    const float target_hz = hz * powf(2.0f, (float)k);
    return 1200.0f * log2f(target_hz / p_.bird_ref_hz);
}

float BirdEngine::Process(float in) {
    // Acompanha a nota tocada. Só confia em leitura firme, e segura a última
    // altura boa enquanto a nota decai, em vez de sair atrás de ruído.
    // Controla o pássaro pela mediana das leituras confiáveis recentes, não
    // pela estimativa crua de cada quadro. Em acorde um detector monofônico
    // fica pulando entre as notas; sem isso o pássaro iria junto no tranco.
    if (pitch_.Process(in)) {
        if (pitch_.stable_ready() && pitch_.stable_frequency() > 0.0f) {
            have_pitch_ = true;
        }
    }

    const float target = (p_.pitch_follow && have_pitch_)
                             ? TargetCents(pitch_.stable_frequency())
                             : 0.0f;
    // No modo TRAVA o pássaro segue ao vivo enquanto o detector assenta e
    // depois congela. Daí em diante bend, slide e vibrato não o movem mais.
    float alvo = target;
    if(p_.modo_altura == Params::TRAVA)
    {
        if(congelado_)
        {
            alvo = latched_cents_;
        }
        else if(congela_em_ > 0 && --congela_em_ == 0)
        {
            latched_cents_ = target;
            congelado_     = true;
        }
    }
    cents_smooth_ += cents_coeff_ * (alvo - cents_smooth_);

    // Segue a dinâmica da guitarra pro pássaro respirar junto com o seu toque.
    const float rect = fabsf(in);
    env_ += (rect > env_ ? env_atk_ : env_rel_) * (rect - env_);
    const float level = 1.0f - p_.dynamics + p_.dynamics * fminf(env_ * 6.0f, 1.0f);

    // --- sorteio no ataque ------------------------------------------------
    //
    // Cada nota nova é uma aposta. A histerese (dois limiares) evita disparar
    // duas vezes na mesma palhetada: só rearma depois que o sinal cai.
    on_fast_ += (rect > on_fast_ ? on_fast_atk_ : on_fast_rel_) * (rect - on_fast_);
    thresh_  += (on_fast_ > thresh_ ? thresh_atk_ : thresh_rel_) * (on_fast_ - thresh_);
    if (refract_ > 0) refract_--;
    const bool alto    = on_fast_ > 0.015f;
    const bool subindo = on_fast_ > thresh_ * 1.30f;

    const bool tem_segundo = p_.bird2_chance > 0.0f && !bird2_.empty();
    const bool modo_frase  = (p_.modo_disparo == Params::FRASE);

    // No modo frase o pássaro não atende enquanto está cantando nem enquanto
    // descansa. É isso que dá o espaçamento: o ataque chega, e é ignorado.
    const bool ocupado = modo_frase && (cantando_ || descanso_ > 0);

    if (refract_ == 0 && alto && subindo) {
        refract_ = (int)(0.080f * sr_);

        if (ocupado) {
            ignorados_++;
        } else {
            // Solta o congelamento e marca quando recongelar: 130 ms dão tempo
            // do detector assentar na nota nova (ele precisa de ~110 ms).
            congelado_  = false;
            congela_em_ = (int)(0.130f * sr_);
            onsets_++;
            chance_now_ = ChanceOfBird2(have_pitch_ ? pitch_.stable_frequency() : 0.0f);

            if (p_.force_bird == 1)       want_bird2_ = false;
            else if (p_.force_bird == 2)  want_bird2_ = true;
            else if (!tem_segundo)        want_bird2_ = false;
            else                          want_bird2_ = (NextRandom() < chance_now_);

            if (want_bird2_) picks2_++;

            if (modo_frase) {
                // Quão forte foi a palhetada, pra chamada sair no volume do
                // seu toque.
                //
                // Não dá pra ler isso no instante do disparo: no modo frase o
                // detector é rápido e dispara CEDO no transiente, com o
                // envelope ainda subindo. Medido: capturando na hora, uma
                // escala bem tocada saía com pico 0,096 contra 0,375 do modo
                // contínuo, quase quatro vezes mais baixa.
                //
                // Então abre uma janela de 50 ms e guarda o MAIOR valor que
                // passar por ela. O pico do ataque cai dentro dessa janela em
                // qualquer toque, e a chamada inteira usa esse número.
                nivel_canto_ = 0.0f;
                mede_nivel_  = (int)(0.050f * sr_);
                ComecaChamada();
            }
        }
    }
    if (!tem_segundo) want_bird2_ = false;

    // --- modo frase: toca a chamada, depois descansa ----------------------
    if (modo_frase) {
        // Janela de medida da força do toque, logo depois do disparo.
        if (mede_nivel_ > 0) {
            mede_nivel_--;
            // O 0,70 casa o volume dos dois modos.
            //
            // No contínuo o pássaro desce junto com a nota que decai; na
            // chamada ele segura o nível do ataque até terminar de cantar, e
            // por isso sai mais alto. Sem correção a diferença medida foi de
            // 1,4x em nota solta e 2,5x em acorde, o bastante pra você achar
            // que um modo é melhor só porque está mais forte.
            const float agora = 0.70f * (1.0f - p_.dynamics
                                + p_.dynamics * fminf(on_fast_ * 6.0f, 1.0f));
            if (agora > nivel_canto_) nivel_canto_ = agora;
        }
        if (cantando_) {
            if (--canto_restante_ <= 0) {
                cantando_ = false;
                descanso_ = (int)(p_.espaco_ms * 0.001f * sr_);
            }
        } else if (descanso_ > 0) {
            descanso_--;
        }
        gate_ += gate_coeff_ * ((cantando_ ? 1.0f : 0.0f) - gate_);
    } else {
        gate_ = 1.0f;
    }

    float grain = p_.grain_ms;
    if (grain < 1.0f) grain = 1.0f;

    // Caminha o crossfade em direção ao pássaro escolhido.
    xfade_ += xfade_coeff_ * ((want_bird2_ ? 1.0f : 0.0f) - xfade_);

    float bird;
    if (modo_frase && !cantando_ && gate_ < 0.001f) {
        // Descansando: nem processa o granular. Na Daisy isso devolve a CPU
        // inteira nos intervalos, que é a maior parte do tempo.
        bird = 0.0f;
    } else if (!tem_segundo && xfade_ < 0.001f) {
        // Com a chave desligada nem processamos o segundo granular, o custo de
        // CPU volta a ser o de antes, o que importa na Daisy.
        bird = gran_.Process(p_.speed, cents_smooth_, grain);
    } else {
        const float a = gran_.Process(p_.speed, cents_smooth_, grain);
        const float g2 = modo_frase ? p_.bird2_gain_frase : p_.bird2_gain;
        const float b  = g2
                        * gran2_.Process(p_.speed, cents_smooth_ + cents2_offset_, grain);
        // Potência igual, como na mistura seco/molhado: sem queda de volume no
        // meio da transição.
        bird = sqrtf(1.0f - xfade_) * a + sqrtf(xfade_) * b;
    }
    // No modo frase a dinâmica vira a forma da própria chamada: o quão forte
    // você tocou define o volume dela, e o portão a abre e fecha. O envelope
    // ao vivo sai de cena de propósito: se ele continuasse mandando, soltar a
    // corda cortaria a frase no meio, e o ponto do modo é justamente deixar o
    // pássaro terminar de cantar.
    bird *= modo_frase ? (nivel_canto_ * gate_) : level;

    // Mistura de potência igual mantém o volume estável enquanto você gira o mix.
    const float m   = p_.mix < 0.0f ? 0.0f : (p_.mix > 1.0f ? 1.0f : p_.mix);
    float       out = sqrtf(1.0f - m) * in + sqrtf(m) * bird;

    // Cinto e suspensório: NaN ou infinito não podem chegar no alto-falante de
    // jeito nenhum, e nada sai daqui acima da escala cheia.
    if(!std::isfinite(out)) out = 0.0f;
    return SoftClip(out);
}
