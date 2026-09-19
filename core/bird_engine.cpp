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
    const std::vector<Frase>& f = passaros_[bird].frases;
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
    Passaro& pa = passaros_[para_];
    if(pa.frases.empty() || pa.sample.empty()) return;

    const int i = SorteiaFrase(para_);
    frase_ant_  = i;

    // Apara pelo teto, contado no ARQUIVO. Assim o teto significa a mesma
    // coisa em qualquer velocidade.
    int len = pa.frases[i].len;
    const int teto = (int)(p_.frase_max_ms * 0.001f * sr_);
    if(teto > 0 && len > teto) len = teto;

    // Aponta o granular pro trecho. O size dele passa a ser o da frase, então
    // uma volta do fasor de posição é exatamente uma passada pela frase.
    pa.gran.Restart(const_cast<float*>(pa.sample.data()) + pa.frases[i].ini, len);

    // Quanto tempo de saída isso dá: a frase tem len amostras de arquivo e é
    // lida a `speed`, então demora len/speed. Com speed < 1 estica.
    float v = fabsf(p_.speed);
    if(v < 0.02f) v = 0.02f;   // não deixa a chamada durar pra sempre
    canto_restante_ = (int)((float)len / v);

    // Com o portão fechado dá pra trocar de pássaro na hora, sem crossfade:
    // não tem o que estalar se não está saindo som.
    de_       = para_;
    xfade_    = 1.0f;
    cantando_ = true;
    descanso_ = 0;
}

int BirdEngine::phrase_count(int bird) const
{
    if(bird < 0 || bird >= Params::kPassaros) return 0;
    return (int)passaros_[bird].frases.size();
}

void BirdEngine::phrase_at(int bird, int i, int& ini, int& len) const
{
    ini = len = 0;
    if(bird < 0 || bird >= Params::kPassaros) return;
    const std::vector<Frase>& f = passaros_[bird].frases;
    if(i < 0 || i >= (int)f.size()) return;
    ini = f[i].ini;
    len = f[i].len;
}

bool BirdEngine::has_bird(int bird) const
{
    return bird >= 0 && bird < Params::kPassaros && !passaros_[bird].sample.empty();
}

int BirdEngine::bird_count() const
{
    int n = 0;
    for(int i = 0; i < Params::kPassaros; i++) if(has_bird(i)) n++;
    return n;
}

void BirdEngine::Init(float sample_rate) {
    sr_ = sample_rate;
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

    for(int i = 0; i < Params::kPassaros; i++)
    {
        passaros_[i].sample.clear();
        passaros_[i].frases.clear();
        passaros_[i].cents_smooth = 0.0f;
    }
    min_ms_usado_ = -1.0f;

    env_          = 0.0f;
    have_pitch_   = false;
    refract_      = 0;
    latched_hz_ = 0.0f;
    congela_em_ = 0;
    congelado_     = false;
    on_fast_ = thresh_ = 0.0f;
    de_ = para_ = 1;
    xfade_       = 1.0f;
    onsets_      = 0;
    ignorados_   = 0;
    for(int i = 0; i < Params::kPassaros; i++) { picks_[i] = 0; peso_now_[i] = 0.0f; }
    alvo_now_ = 0.0f;
    k_now_    = 0;
    classe_firme_ = -1;
    ring_w_ = ring_n_ = 0;
    for(int i = 0; i < 12; i++) conta_classe_[i] = 0;
    for(int i = 0; i < kJanelaAcorde; i++) ring_classe_[i] = 0;
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

    // As casas podem ter mudado, e o deslocamento de cada pássaro sai delas.

    if(piso_mudou)
    {
        for(int i = 0; i < Params::kPassaros; i++)
            if(!passaros_[i].sample.empty())
                SegmentaFrases(passaros_[i].sample, sr_, p_.frase_min_ms,
                               passaros_[i].frases);
        min_ms_usado_ = p_.frase_min_ms;
        frase_ant_    = -1;
    }
}

void BirdEngine::SetBird(int bird, const float* sample, int len)
{
    if(bird < 0 || bird >= Params::kPassaros || sample == nullptr || len <= 0) return;

    Passaro& pa = passaros_[bird];
    pa.sample.assign(sample, sample + len);
    pa.gran.Init(pa.sample.data(), (int)pa.sample.size(), sr_);
    SegmentaFrases(pa.sample, sr_, p_.frase_min_ms, pa.frases);
    min_ms_usado_ = p_.frase_min_ms;


    // Se o pássaro que estava selecionado não tem sample, cai pra este.
    if(!has_bird(para_)) { de_ = para_ = bird; xfade_ = 1.0f; }
}

float BirdEngine::NextRandom()
{
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return (float)(rng_ >> 8) * (1.0f / 16777216.0f); // 0..1
}

void BirdEngine::PesosDosPassaros(float played_hz, float* peso) const
{
    for(int i = 0; i < Params::kPassaros; i++) peso[i] = 0.0f;

    // O sorteio usa outra régua que a altura: o REGISTRO em que você tocou,
    // que é a nota deslocada por uma oitava de referência fixa. Precisa ser
    // assim porque, com cada pássaro perto da própria casa, distância de casa
    // não diferencia mais ninguém: os três ficam dentro de meia oitava.
    const float alvo = (played_hz > 0.0f)
                           ? played_hz * powf(2.0f, (float)p_.oitava_registro)
                           : 0.0f;
    if(alvo <= 0.0f)
    {
        // Sem altura detectada ainda. Fica no pássaro do meio, que é o que
        // cobre a maior parte do braço.
        peso[1] = 1.0f;
        return;
    }

    // Peso cai com o quadrado da distância até a casa, em cents. A `variedade`
    // abre a curva: apertada, só o mais perto sobrevive; aberta, os vizinhos
    // de região entram no sorteio.
    const float sigma = 40.0f + p_.variedade * 900.0f;
    float soma = 0.0f;
    int   perto = -1;
    float menor = 1e9f;
    for(int i = 0; i < Params::kPassaros; i++)
    {
        if(!has_bird(i)) continue;
        const float d = 1200.0f * log2f(alvo / p_.casa_hz[i]);
        if(fabsf(d) < menor) { menor = fabsf(d); perto = i; }
        const float x = d / sigma;
        peso[i] = expf(-x * x);
        soma += peso[i];
    }

    // Com sigma apertado e o alvo longe de tudo, todos os pesos somem no
    // subfluxo. Aí o mais perto leva, que é o comportamento certo: nunca ficar
    // sem pássaro.
    if(soma <= 1e-12f)
    {
        for(int i = 0; i < Params::kPassaros; i++) peso[i] = 0.0f;
        if(perto >= 0) peso[perto] = 1.0f;
        return;
    }
    for(int i = 0; i < Params::kPassaros; i++) peso[i] /= soma;
}

bool BirdEngine::TrocouDeAcorde(bool leitura_nova)
{
    // Só mexe no histórico quando chega leitura nova do detector de altura.
    // Contar amostra a amostra encheria a janela com a mesma leitura repetida.
    if(!leitura_nova) return false;

    const float hz = (p_.pitch_follow && have_pitch_) ? pitch_.stable_frequency() : 0.0f;
    if(hz <= 0.0f) return false;

    // Classe de nota, 0 a 11. Comparar CLASSE e não frequência é o ponto: o
    // mesmo acorde batido de novo pode dar E2 numa vez e E3 na outra conforme
    // qual corda soou mais forte, e as duas são a mesma harmonia.
    const float midi = 69.0f + 12.0f * log2f(hz / 440.0f);
    int c = ((int)lroundf(midi)) % 12;
    if(c < 0) c += 12;

    // Quantas leituras cabem na janela pedida. O detector entrega uma a cada
    // hop, e o hop é kHop amostras dizimadas.
    int janela = (int)(p_.acorde_estavel_ms * 0.001f * sr_ / 512.0f);
    if(janela < 2) janela = 2;
    if(janela > kJanelaAcorde) janela = kJanelaAcorde;

    // Anel: tira a mais antiga quando a janela enche, põe a nova.
    if(ring_n_ >= janela)
    {
        const int velha = ring_classe_[(ring_w_ - janela + kJanelaAcorde * 2) % kJanelaAcorde];
        if(conta_classe_[velha] > 0) conta_classe_[velha]--;
    }
    else ring_n_++;
    ring_classe_[ring_w_] = c;
    conta_classe_[c]++;
    ring_w_ = (ring_w_ + 1) % kJanelaAcorde;

    if(ring_n_ < janela) return false;

    // A classe que domina a janela.
    int dom = 0;
    for(int i = 1; i < 12; i++) if(conta_classe_[i] > conta_classe_[dom]) dom = i;

    // Exige maioria de verdade, não só o maior monte. Sem isto, tremor entre
    // três classes elegeria qualquer uma delas com um terço dos votos.
    if(conta_classe_[dom] * 2 <= janela) return false;

    if(dom == classe_firme_) return false;
    classe_firme_ = dom;
    return true;
}

int BirdEngine::EscolhePassaro(float played_hz)
{
    // Força um pássaro só, pra comparar de ouvido. 1..3 na linha de comando
    // viram 0..2 aqui.
    if(p_.force_bird >= 1 && p_.force_bird <= Params::kPassaros)
    {
        const int f = p_.force_bird - 1;
        if(has_bird(f)) return f;
    }

    PesosDosPassaros(played_hz, peso_now_);

    // Roleta: sorteia um ponto em [0,1) e caminha somando os pesos.
    const float r = NextRandom();
    float acc = 0.0f;
    for(int i = 0; i < Params::kPassaros; i++)
    {
        acc += peso_now_[i];
        if(r < acc && has_bird(i)) return i;
    }
    // Arredondamento pode deixar r acima da soma na última casa. Devolve o
    // último com sample, em vez de cair num índice vazio.
    for(int i = Params::kPassaros - 1; i >= 0; i--) if(has_bird(i)) return i;
    return para_;
}

void BirdEngine::Retrigger() {
    for(int i = 0; i < Params::kPassaros; i++)
    {
        Passaro& pa = passaros_[i];
        if(!pa.sample.empty())
            pa.gran.Restart(pa.sample.data(), (int)pa.sample.size());
    }
    cantando_       = false;
    canto_restante_ = 0;
    descanso_       = 0;
    frase_ant_      = -1;
    gate_           = 0.0f;
}

float BirdEngine::AlvoHz(int bird, float played_hz, int* k_usado) const
{
    if(k_usado) *k_usado = 0;
    if(played_hz <= 0.0f || bird < 0 || bird >= Params::kPassaros) return 0.0f;

    float hz = played_hz;
    if(p_.modo_altura == Params::DEGRAU)
    {
        // Trava no semitom mais próximo, assim uma oscilaçãozinha na altura
        // detectada não faz o pássaro tremer.
        const float midi = roundf(69.0f + 12.0f * log2f(hz / 440.0f));
        hz               = 440.0f * powf(2.0f, (midi - 69.0f) / 12.0f);
    }

    // A oitava que deixa ESTE pássaro mais perto da casa DELE.
    //
    // Arredondar o logaritmo é o que garante o mínimo esticamento: o resto
    // nunca passa de meia oitava, então o canto nunca é deformado mais que
    // isso. E K sendo inteiro, a classe da nota não muda, ou seja não desafina.
    int k = (int)roundf(log2f(p_.casa_hz[bird] / hz)) + p_.octave_offset;

    // Piso e teto ficam desligados por padrão. Com a oitava por pássaro o alvo
    // já cai perto de casa sozinho; estes existem pra experimentar.
    if(p_.teto_hz > 0.0f)
        while(k > -10 && hz * powf(2.0f, (float)k) > p_.teto_hz) k--;
    if(p_.piso_hz > 0.0f)
        while(k < 10 && hz * powf(2.0f, (float)k) < p_.piso_hz) k++;

    if(k_usado) *k_usado = k;
    return hz * powf(2.0f, (float)k);
}

float BirdEngine::CentsDoPassaro(int bird, float played_hz) const
{
    // Sem nota detectada o pássaro fica na altura natural dele, que é zero de
    // transposição. É o repouso.
    if(played_hz <= 0.0f || bird < 0 || bird >= Params::kPassaros) return 0.0f;
    const float alvo = AlvoHz(bird, played_hz);
    if(alvo <= 0.0f) return 0.0f;
    return 1200.0f * log2f(alvo / p_.casa_hz[bird]);
}

float BirdEngine::TargetCents(float played_hz) const
{
    return CentsDoPassaro(para_, played_hz);
}

float BirdEngine::AlturaEfetiva() const
{
    if(!p_.pitch_follow || !have_pitch_) return 0.0f;
    if(p_.modo_altura == Params::TRAVA && congelado_) return latched_hz_;
    return pitch_.stable_frequency();
}

float BirdEngine::Process(float in) {
    // Acompanha a nota tocada. Só confia em leitura firme, e segura a última
    // altura boa enquanto a nota decai, em vez de sair atrás de ruído.
    // Controla o pássaro pela mediana das leituras confiáveis recentes, não
    // pela estimativa crua de cada quadro. Em acorde um detector monofônico
    // fica pulando entre as notas; sem isso o pássaro iria junto no tranco.
    const bool leitura_nova = pitch_.Process(in);
    if (leitura_nova) {
        if (pitch_.stable_ready() && pitch_.stable_frequency() > 0.0f) {
            have_pitch_ = true;
        }
    }

    // A TRAVA agora congela a NOTA, não os cents.
    //
    // Antes dava no mesmo, porque havia um alvo só. Agora cada pássaro tem o
    // próprio alvo, então travar cents travaria um pássaro e deixaria os outros
    // soltos. Travando a nota, os três param juntos.
    //
    // Ele não congela no instante do ataque: o detector precisa de ~110 ms pra
    // ter mediana confiável da nota nova, e travar na hora pegaria a altura da
    // ANTERIOR. Então segue ao vivo durante o assentamento e só depois congela.
    // Assim não há atraso, e a estabilidade vale onde importa, que é no corpo
    // sustentado da nota, onde o bend e o vibrato acontecem.
    if(p_.modo_altura == Params::TRAVA && !congelado_ &&
       congela_em_ > 0 && --congela_em_ == 0)
    {
        latched_hz_ = (p_.pitch_follow && have_pitch_)
                          ? pitch_.stable_frequency() : 0.0f;
        congelado_  = true;
    }

    // Cada pássaro persegue o próprio alvo. Os calados também, pra já estarem
    // na altura certa quando forem sorteados.
    const float hz_efetivo = AlturaEfetiva();
    for(int i = 0; i < Params::kPassaros; i++)
    {
        const float alvo_i = CentsDoPassaro(i, hz_efetivo);
        passaros_[i].cents_smooth += cents_coeff_ * (alvo_i - passaros_[i].cents_smooth);
    }

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

    const bool modo_acorde = (p_.modo_disparo == Params::ACORDE);
    // O modo acorde também toca a chamada e descansa, se for esse o jeito de
    // responder escolhido. A máquina é a mesma; o que muda é o gatilho.
    const bool usa_chamada = (p_.modo_disparo == Params::FRASE) ||
                             (modo_acorde && p_.acorde_com_frase);
    const bool modo_frase  = usa_chamada;

    // O gatilho. No modo acorde é a troca de harmonia; nos outros, o ataque.
    //
    // A troca de harmonia precisa ser consultada TODA amostra, porque o
    // contador de estabilidade dela anda em amostras.
    const bool trocou = modo_acorde ? TrocouDeAcorde(leitura_nova) : false;
    const bool gatilho = modo_acorde
                             ? trocou
                             : (refract_ == 0 && alto && subindo);

    // Enquanto canta ou descansa o pássaro não atende. É isso que dá o
    // espaçamento: o gatilho chega, e é ignorado.
    const bool ocupado = usa_chamada && (cantando_ || descanso_ > 0);

    if (gatilho) {
        if (!modo_acorde) refract_ = (int)(0.080f * sr_);

        if (ocupado) {
            ignorados_++;
        } else {
            // Solta o congelamento e marca quando recongelar: 130 ms dão tempo
            // do detector assentar na nota nova (ele precisa de ~110 ms).
            congelado_  = false;
            congela_em_ = (int)(0.130f * sr_);
            onsets_++;

            // Sorteia quem responde. O pássaro mais perto da casa dele ganha
            // mais chance; a `variedade` decide o quanto os vizinhos entram.
            const float toque = have_pitch_ ? pitch_.stable_frequency() : 0.0f;
            // Sorteia UMA vez: cada chamada consome o gerador, e chamar duas
            // vezes quebraria a estatística do sorteio.
            const int novo = EscolhePassaro(toque);
            picks_[novo]++;
            alvo_now_ = AlvoHz(novo, toque, &k_now_);

            // Troca com crossfade curto. Só duas vozes participam de cada
            // troca, e o bloqueio de 80 ms garante que uma termine antes da
            // próxima começar.
            if (novo != para_) {
                de_    = para_;
                para_  = novo;
                xfade_ = 0.0f;
            }

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

    // --- modo frase: toca a chamada, depois descansa ----------------------
    if (modo_frase) {
        // Janela de medida da força do toque, logo depois do disparo.
        if (mede_nivel_ > 0) {
            mede_nivel_--;
            // O 0,70 aproxima o volume dos dois modos.
            //
            // No contínuo o pássaro desce junto com a nota que decai; na
            // chamada ele segura o nível do ataque até terminar de cantar, e
            // por isso sai mais alto. Sem nenhuma correção a diferença medida
            // foi de 1,4x em nota solta e 2,5x em acorde, o bastante pra você
            // achar que um modo é melhor só porque está mais forte.
            //
            // ATENÇÃO, este valor está desatualizado. Ele foi calibrado antes
            // de bird2_gain existir, e o ganho separado por modo (0,262 contra
            // 0,443) desequilibrou os dois de novo. Medindo hoje, o modo frase
            // sai de 1,1 a 1,9 vezes mais alto conforme a entrada. Não mexi no
            // número ainda porque isso muda o som, e o modo frase ainda não foi
            // aprovado de ouvido.
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

    // Caminha o crossfade em direção ao pássaro que entrou.
    xfade_ += xfade_coeff_ * (1.0f - xfade_);

    // Uma voz por pássaro envolvido na troca. Fora do instante da troca só uma
    // roda, então o custo de CPU normal é o de um granular, não de três.
    auto voz = [&](int i) -> float {
        Passaro& pa = passaros_[i];
        if (pa.sample.empty()) return 0.0f;
        const float g = modo_frase ? p_.ganho_frase[i] : p_.ganho[i];
        return g * pa.gran.Process(p_.speed, pa.cents_smooth, grain);
    };

    float bird;
    if (modo_frase && !cantando_ && gate_ < 0.001f) {
        // Descansando: nem processa o granular. Na Daisy isso devolve a CPU
        // inteira nos intervalos, que é a maior parte do tempo.
        bird = 0.0f;
    } else if (de_ == para_ || xfade_ > 0.999f) {
        bird = voz(para_);
    } else {
        // Potência igual, como na mistura seco/molhado: sem queda de volume no
        // meio da transição.
        bird = sqrtf(1.0f - xfade_) * voz(de_) + sqrtf(xfade_) * voz(para_);
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
