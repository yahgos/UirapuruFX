#pragma once
#include <vector>

#include "granular_player.h"
#include "pitch_tracker.h"

/** O pedal.
 *
 * Toca o canto gravado do pássaro de forma granular, transpondo pra acompanhar
 * a nota que você tocou, e mistura isso embaixo da sua guitarra seca.
 *
 * Por que granular e não sampler comum: no sampler normal velocidade e altura
 * andam juntas, então acelerar a frase desafinaria ela. O GranularPlayer separa
 * as duas coisas: o knob de velocidade só muda o quão rápido a frase corre, e
 * a altura só acompanha o que você toca.
 */
class BirdEngine {
  public:
    // --- A CARACTERÍSTICA DO PEDAL ---------------------------------------
    //
    // Três destes valores são fixos por decisão de projeto, não são "defaults
    // que dá pra mexer". Foram apurados tocando, e é a combinação deles que dá
    // identidade ao pedal:
    //
    //   octave_offset = 2   acima disso o pássaro fica esquisito
    //   pitch_follow  = on  sem isso não existe o efeito, só um sample tocando
    //   dynamics      = 1   o pássaro respira junto com o toque, sempre
    //   speed         = 0,52 (ponto de partida) o que soou mais real
    //   ceiling_hz    = 1850  acima daqui o pássaro dobra uma oitava pra baixo
    //                         em vez de ficar fino, descoberto tocando agudo
    //   a curva de chance do bem-te-vi (5% no grave -> 20% no agudo) também é
    //   característica: a chave do plugin só liga ou desliga o segundo pássaro,
    //   não mexe na curva
    //
    // O plugin expõe só Mix, Speed e Grain. Estes quatro moram aqui porque
    // este arquivo é a fonte única: linha de comando, plugin e o pedal na
    // Daisy nascem todos dos mesmos números e não têm como divergir.
    //
    // A CLI ainda aceita --octaves, --no-follow e --dynamics de propósito: ela
    // é a bancada de teste, e foi com essas flags que verificamos a afinação.
    // ---------------------------------------------------------------------
    struct Params {
        float speed    = 0.52f;  // ritmo da frase; 1 = natural. 0,52 foi o que soou mais real
        float mix      = 0.5f;   // 0 = só guitarra seca, 1 = só pássaro
        float grain_ms = 80.0f;  // pequeno = tagarela, grande = colchão suave

        bool pitch_follow = true;   // false = pássaro fica na altura natural dele

        // Como o pássaro reage quando a altura muda DENTRO de uma nota: bend,
        // slide, vibrato.
        //
        // O problema medido: com quantização em semitons, um bend de um tom faz
        // o pássaro dar três saltos de 100 cents. Com vibrato ele fica cruzando
        // a fronteira do semitom pra frente e pra trás. É o tremido.
        //
        //   TRAVA  - captura a altura no ataque e SEGURA até a próxima nota.
        //            Bend, slide e vibrato não movem o pássaro. Mais estável.
        //   DESLIZA- acompanha continuamente, sem quantizar. O pássaro faz o
        //            bend junto com você, liso, sem degraus.
        //   DEGRAU - o comportamento antigo, em semitons.
        enum ModoAltura { TRAVA = 0, DESLIZA = 1, DEGRAU = 2 };
        int modo_altura = TRAVA;

        // --- COMO O CANTO É DISPARADO ------------------------------------
        //
        // CONTINUO é o comportamento original: o granular varre o arquivo sem
        // parar e o que você ouve é o pássaro cantando ininterrupto, com a
        // dinâmica da guitarra abrindo e fechando o volume.
        //
        // O problema disso, medido nos dois arquivos: o canto quase não tem
        // silêncio. Agrupando as frases, o uirapuru é 90% som e o bem-te-vi
        // também 90%. Ou seja, a fonte é um muro de pássaro, e em toque
        // rápido os cantos se embolam, porque nunca sobra respiro entre eles.
        //
        // FRASE troca isso por chamada e resposta. Cada ataque dispara UMA
        // frase do canto, do início ao fim, e depois o pássaro DESCANSA. Notas
        // tocadas durante a chamada ou durante o descanso não disparam nada.
        //
        // Duas coisas boas saem de graça daí:
        //   - a frase tem forma: começa onde o pássaro começou e termina onde
        //     ele terminou, em vez de ser uma fatia qualquer do meio
        //   - solo rápido deixa de ser problema. O efeito para de tentar
        //     acompanhar cada nota: ele escolhe uma, canta a frase inteira
        //     naquela altura, e descansa. O piso de ~110 ms do detector deixa
        //     de importar, porque ele não decide mais nota por nota.
        enum ModoDisparo { CONTINUO = 0, FRASE = 1 };
        int modo_disparo = CONTINUO;

        // Quanto silêncio entre o fim de uma chamada e a próxima poder
        // disparar. O padrão sai da medição do próprio arquivo: as pausas do
        // uirapuru entre frases são de 351 a 430 ms.
        float espaco_ms = 400.0f;

        // Teto pra duração de UMA frase, medido no arquivo (não na saída).
        //
        // Precisa de teto porque a maior frase do uirapuru tem 3,0 s de
        // arquivo, e a velocidade 0,52 estica isso pra quase 6 s de som. Frase
        // longa demais vira drone de novo e perde o sentido do modo.
        float frase_max_ms = 1500.0f;

        // Piso pra o que conta como frase. A segmentação acha uns fragmentos
        // de 44 e 77 ms no uirapuru; são pedaços de trinado, não chamadas.
        float frase_min_ms = 150.0f;

        // --- Velocidade do detector de ataque ----------------------------
        //
        // Dois pares de constantes, e o modo de disparo escolhe qual vale.
        //
        // São estes números que decidem o quão rápido você pode tocar antes de
        // o pedal parar de perceber as notas. O limiar persegue o envelope: se
        // ele cai devagar, a nota seguinte chega e o limiar ainda está lá em
        // cima, então o ataque não consegue passar dos 30% exigidos.
        //
        // Medido, em notas por segundo, contando ataques numa corrida de 32:
        //
        //   env/limiar   1   2   3   4   6   8  notas/s
        //     40 / 250  32  32  32   1   1   1
        //     12 /  25  32  32  32  32  32  32
        //
        // Então por que não usar o par rápido sempre? Porque ele também
        // dispara MAIS em acorde: uma batida de acorde aberto dá 8 ataques com
        // o par lento e 22 com o rápido. No modo contínuo isso é ruim, cada
        // ataque re-sorteia o pássaro e destrava a altura.
        //
        // No modo FRASE o custo desaparece: o primeiro ataque começa a chamada
        // e todos os outros são ignorados enquanto ela toca. O portão engole os
        // falsos positivos de graça. Medi até 28 ignorados por trecho, sem
        // nenhum efeito audível. É por isso que o par depende do modo, em vez
        // de ser um só valor de compromisso.
        float onset_env_rel_ms   = 40.0f;   // contínuo
        float onset_rel_ms       = 250.0f;
        float onset_env_frase_ms = 12.0f;   // frase
        float onset_rel_frase_ms = 25.0f;

        // --- OS TRÊS PÁSSAROS ---------------------------------------------
        //
        // Cada canto tem uma CASA: a altura mediana da gravação, medida. Longe
        // de casa o canto soa estranho, e é esse o problema que o terceiro
        // pássaro resolve.
        //
        // Medido com o mesmo método nas três gravações, o que importa: se cada
        // referência vier de um método diferente, os pássaros não pousam na
        // mesma altura e a troca desafina.
        //
        // A do Mau é a exceção, e vale saber por quê. A mediana do arquivo dele
        // dá 940 Hz, mas com esse valor a saída renderizada pousava 267 cents
        // ABAIXO das outras duas. O canto dele é bimodal (p10 em 675 Hz, p90 em
        // 1452), e nesse caso a mediana do arquivo não prevê onde o resultado
        // transposto cai. Os 805 saíram de varredura na SAÍDA, procurando o
        // valor que faz o pássaro pousar no alvo. É o que "referência" quer
        // dizer na prática.
        //
        //   índice  pássaro     casa      papel
        //     0     Mau         1100 Hz   o grave
        //     1     uirapuru    1823 Hz   o do meio
        //     2     bem-te-vi   2853 Hz   o agudo
        //
        // Com um pássaro só, o pior caso na faixa E2 a E6 era o canto tocando
        // 2961 cents (24 semitons) abaixo de casa. Com os três e a regra de
        // escolha abaixo, o pior caso cai pra 661 cents.
        static constexpr int kPassaros = 3;
        float casa_hz[kPassaros] = {1100.0f, 1823.0f, 2853.0f};


        // O suavizador da transposição é ancorado num pássaro só, e os outros
        // saem dele por um deslocamento constante. Um suavizador só garante que
        // os três nunca se desencontrem, por mais que o alvo se mexa.
        //
        // A âncora é o uirapuru, por dois motivos. Ele é o do meio, o que
        // deixa os deslocamentos pequenos e simétricos. E ele era a referência
        // única antes de existirem três, então a conta dele continua sendo um
        // logaritmo só, sem o arredondamento de virar soma de dois. Medido:
        // ancorar em outro pássaro mudava a saída do uirapuru em fração de
        // cent, inaudível mas o bastante pra derrubar comparação exata.
        static constexpr int kAncora = 1;

        // Ganho de correção por pássaro, pra nenhum entrar mais forte que os
        // outros. Dois conjuntos porque o valor depende do modo de disparo: no
        // modo frase cada pássaro toca só os trechos que a segmentação marcou
        // como canto, que são os mais fortes dele, e a média sobe.
        //
        // Descobrimos isso no bem-te-vi: os arquivos foram normalizados
        // separadamente e ele entrava 11,5 dB mais alto que o uirapuru toda vez
        // que era sorteado. Calibrado medindo o RMS da SAÍDA renderizada, não
        // o do arquivo, porque a transposição muda a energia.
        // Calibrados medindo o RMS da SAÍDA em sete notas de E2 a A4, com cada
        // pássaro forçado, e igualando tudo ao uirapuru:
        //
        //             contínuo   frase
        //   Mau         0,739    0,804    estava 2,6 dB acima
        //   uirapuru    1,000    1,000    a referência
        //   bem-te-vi   0,262    0,443    estava 11,5 dB acima
        //
        // O Mau precisa de mais correção que o bem-te-vi por duas razões: foi
        // normalizado pra 0,98 de pico contra 0,324 do uirapuru, e o canto dele
        // é contínuo, sem as pausas internas que os outros dois têm.
        float ganho[kPassaros]       = {0.739f, 1.0f, 0.264f};  // contínuo
        float ganho_frase[kPassaros] = {0.804f, 1.0f, 0.444f};  // modo frase

        // --- COMO A ALTURA É ESCOLHIDA ------------------------------------
        //
        // Cada pássaro canta na oitava que o deixa MAIS PERTO DA CASA DELE:
        //
        //   K_i = round(log2(casa_i / nota))        oitavas, inteiro
        //   alvo_i = nota * 2^K_i
        //
        // Duas propriedades saem disso, e as duas importam.
        //
        // Nunca desafina: K é inteiro, e oitava inteira não muda a classe da
        // nota. O pássaro cai exatamente na nota que você tocou.
        //
        // E deforma o mínimo possível. Como K é o arredondamento, o canto nunca
        // é esticado mais que meia oitava. Medido na varredura de E2 a E6, o
        // pior caso é 214 cents, menos de dois semitons. Pra comparar, com alvo
        // comum pros três o pior caso ia de 414 a 886 cents conforme o ajuste,
        // e com um pássaro só chegava a 2961.
        //
        // O preço: como cada pássaro segue a própria casa, o alvo troca de
        // oitava mais vezes ao longo do braço. Mas o pássaro em si quase não sai
        // do registro dele, e é isso que o ouvido percebe.
        //
        // Viés em oitavas, somado ao K de cada pássaro. Zero é o mínimo
        // esticamento, que é o padrão. Existe pra poder experimentar.
        int octave_offset = 0;

        // Oitava de referência do SORTEIO, que é coisa separada da altura.
        //
        // Com cada pássaro perto da própria casa, a distância de casa não
        // diferencia mais ninguém: os três ficam dentro de meia oitava. Então o
        // sorteio precisa de outra régua, e a régua é o registro em que você
        // tocou: `nota * 2^oitava_registro` comparado com a casa de cada um.
        //
        // Separar as duas coisas é o que deixa o pedal ter as duas
        // propriedades ao mesmo tempo: quem responde depende de onde você
        // tocou, e como ele é transposto depende só de não deformar.
        int oitava_registro = 2;

        // --- PISO E TETO DO ALVO ------------------------------------------
        //
        // O alvo é `nota x 2^K`. Estes dois limites apertam o K pelos dois
        // lados, pra que o alvo caia sempre perto da casa de algum pássaro.
        //
        // Por que os dois: a gravação só soa como pássaro perto da altura
        // original. Empurrada pra cima fica fina e sibilante, porque o espectro
        // estica pra uma faixa onde o arquivo não tem energia. Puxada demais
        // pra baixo fica poluída. O teto trata uma ponta e o piso a outra.
        //
        // Os valores saíram de varredura. Com piso 550 e teto 4000:
        //   - o alvo fica sempre dentro de 661 cents de alguma casa
        //   - K continua em 2 em 34 dos 49 semitons do braço
        //   - as duas quebras de monotonicidade caem em C#3 e C6. Antes o
        //     pássaro descia uma oitava em A#4, que é região mais tocada
        //
        // Subir muito o piso força K=4 no grave e cria uma TERCEIRA quebra, o
        // que é pior que os poucos cents que se ganharia.
        //
        // Desligados por padrão: com a oitava escolhida por pássaro, o alvo já
        // fica perto de casa sozinho, e apertar mais só criaria salto de oitava
        // sem ganho nenhum. Ficam disponíveis pra experimentar. 0 desliga.
        float piso_hz = 0.0f;
        float teto_hz = 0.0f;

        // O quanto o pássaro acompanha a dinâmica do seu toque.
        float dynamics = 1.0f;

        // --- QUEM RESPONDE A CADA NOTA ------------------------------------
        //
        // O peso de cada pássaro cai conforme o alvo se afasta da casa dele:
        //
        //   d_i    = |1200 * log2(alvo / casa_i)|     distância em cents
        //   peso_i = exp(-(d_i / sigma)^2)
        //   sigma  = 40 + variedade * 900             cents
        //
        // Isto substituiu uma curva de inclinação desenhada à mão. O
        // mapeamento por região cai como consequência, sem ninguém desenhar:
        // Mau responde de E2 a C#4, uirapuru de E4 a C#5, bem-te-vi de E5 pra
        // cima.
        //
        // A `variedade` é o único knob disso, e é o que o plugin expõe. Medido
        // sobre o braço todo, quanto cada posição aparece:
        //
        //   variedade   dominante   segundo   terceiro
        //        0%        100%        0%        0%
        //       30%         96%        4%        0%
        //       55%         88%       12%        0%
        //      100%         73%       25%        3%
        //
        // Em 0% o pedal sempre escolhe o pássaro que vai soar melhor. O padrão
        // de 55% dá ~12% de aparição do segundo, que é aproximadamente o que o
        // knob antigo do bem-te-vi entregava.
        float variedade = 0.55f;

        // Força um pássaro só, pra comparar de ouvido. 0 = sorteia normalmente,
        // 1 = Mau, 2 = uirapuru, 3 = bem-te-vi.
        int force_bird = 0;
    };

    /** Inicializa o motor. Os cantos entram depois, com SetBird().
     *
     * Separado porque a taxa de amostragem vem do host e os samples vêm de
     * outro lugar (arquivo na linha de comando, dado embutido no plugin).
     */
    void Init(float sample_rate);

    /** Carrega o canto de um pássaro.
     *
     * O índice é o mesmo de Params::casa_hz: 0 = Mau, 1 = uirapuru,
     * 2 = bem-te-vi. Pássaro sem sample carregado nunca é sorteado, então dá
     * pra rodar com um, dois ou três.
     */
    void SetBird(int bird, const float* sample, int len);
    bool has_bird(int bird) const;
    int  bird_count() const;
    /** Troca os parâmetros.
     *
     * Seguro de chamar da thread de áudio: só refaz a segmentação das frases
     * se o piso `frase_min_ms` tiver mudado, e no plugin ele nunca muda. A
     * linha de comando chama isto uma vez antes de renderizar, e é ali que o
     * piso novo entra em vigor.
     */
    void SetParams(const Params& p);
    const Params& params() const { return p_; }

    /** Volta a frase pro começo. */
    void Retrigger();

    /** Processa uma amostra de guitarra e devolve a saída já misturada. */
    float Process(float in);

    /** Diagnóstico pra linha de comando. */
    float detected_hz() const { return pitch_.frequency(); }
    float confidence() const { return pitch_.confidence(); }
    float transposition_cents() const {
        return (para_ >= 0 && para_ < Params::kPassaros)
                   ? passaros_[para_].cents_smooth : 0.0f;
    }

    /** Qual pássaro está tocando agora (pra conferir o sorteio). */
    int   current_bird() const { return para_; }
    int   onset_count() const { return onsets_; }
    /** Quantas vezes cada pássaro foi sorteado, pro teste estatístico. */
    int   pick_count(int bird) const {
        return (bird >= 0 && bird < Params::kPassaros) ? picks_[bird] : 0;
    }
    /** Peso de cada pássaro no último ataque, 0..1. */
    float weight_now(int bird) const {
        return (bird >= 0 && bird < Params::kPassaros) ? peso_now_[bird] : 0.0f;
    }
    /** Frequência alvo absoluta agora, em Hz, e as oitavas aplicadas.
     *
     * Ao vivo, não o valor do último ataque. Os dois são diagnóstico, e o que
     * interessa medir é o mapeamento da altura que está tocando.
     */
    float target_hz() const { return AlvoHz(para_, pitch_.stable_frequency()); }
    int   octaves_now() const {
        int k = 0;
        AlvoHz(para_, pitch_.stable_frequency(), &k);
        return k;
    }
    /** Alvo no instante do último ataque, que foi o que decidiu o sorteio.
     *
     * Difere do de cima na PRIMEIRA nota depois do silêncio: ali o detector
     * ainda não assentou, então o sorteio cai no pássaro do meio, que é o que
     * cobre a maior parte do braço. Da segunda nota em diante ele usa a altura
     * da anterior, que costuma ser vizinha.
     */
    float target_at_onset() const { return alvo_now_; }

    /** Modo frase: ataques que chegaram com o pássaro ocupado e foram
     *  ignorados. É a medida de quanto o espaçamento está filtrando. */
    int   ignored_count() const { return ignorados_; }
    /** Modo frase: true enquanto uma chamada está no ar. */
    bool  singing() const { return cantando_; }
    /** Quantas frases a segmentação achou no canto deste pássaro. */
    int   phrase_count(int bird) const;
    /** Início e duração de uma frase, em amostras (pra conferência). */
    void  phrase_at(int bird, int i, int& ini, int& len) const;

    /** Espiada no detector de ataque, só pra depuração. */
    float dbg_env() const { return on_fast_; }
    float dbg_thresh() const { return thresh_; }
    int   dbg_refract() const { return refract_; }

    /** Semente do sorteio, pra teste reprodutível. */
    void SetSeed(unsigned s) { rng_ = s ? s : 1u; }

  private:
    /** Um trecho contíguo com som dentro do arquivo do canto. */
    struct Frase {
        int ini;  // primeira amostra
        int len;  // quantas amostras
    };

    /** Acha as frases dentro de um canto, olhando onde tem som e onde tem
     *  silêncio.
     *
     * Roda uma vez no carregamento, NUNCA na thread de áudio, porque ela aloca.
     *
     * É o mesmo algoritmo que usei pra medir os arquivos: segue o envelope
     * (ataque de 2 ms, queda de 30 ms), corta onde ele fica abaixo de 6% do
     * pico, e junta trechos separados por menos de 60 ms de silêncio, senão
     * cada batidinha do trinado viraria uma "frase".
     *
     * Detectar em vez de deixar uma tabela chumbada no código: assim trocar o
     * arquivo do sample continua funcionando, e o pedal não fica preso a
     * medições feitas à mão numa gravação específica.
     */
    static void SegmentaFrases(const std::vector<float>& s,
                               float                     sr,
                               float                     min_ms,
                               std::vector<Frase>&       out);

    /** Recalcula as constantes do detector de ataque a partir do modo. */
    void AtualizaCoefsAtaque();

    /** Sorteia uma frase, evitando repetir a de agora. */
    int SorteiaFrase(int bird);

    /** Começa uma chamada: posiciona o granular na frase e abre o portão. */
    void ComecaChamada();

    /** A frequência absoluta que o pássaro vai cantar, em Hz.
     *
     * É `nota x 2^K`, com K inteiro apertado pelo piso e pelo teto. Todos os
     * pássaros miram o MESMO alvo: o que muda entre eles é só a distância até
     * a casa de cada um. Isso é o que garante que a troca não desafine.
     */
    float AlvoHz(int bird, float played_hz, int* k_usado = nullptr) const;

    /** Transposição deste pássaro pra esta nota, em cents contra a casa dele. */
    float CentsDoPassaro(int bird, float played_hz) const;

    /** A nota que manda na altura agora: a detectada, ou a travada. */
    float AlturaEfetiva() const;

    /** Transposição do pássaro 0, em cents, contra a casa dele.
     *
     * Só o pássaro 0 é suavizado. Os outros saem dele por um deslocamento
     * constante (offset_), porque o alvo é comum. Um suavizador só garante que
     * eles nunca se desencontrem, por mais que o alvo se mexa.
     */
    float TargetCents(float played_hz) const;

    /** Peso de cada pássaro pra esta nota, já normalizado (soma 1). */
    void PesosDosPassaros(float played_hz, float* peso) const;

    /** Sorteia qual pássaro responde, usando os pesos. */
    int EscolhePassaro(float played_hz);

    /** Sorteio próprio (xorshift32).
     *
     * Não usamos rand(): ele tem estado global e não é seguro de chamar da
     * thread de áudio. Este é determinístico, sem alocação, e roda igual no
     * desktop e na Daisy.
     */
    float NextRandom();

    /** Tudo o que um pássaro carrega.
     *
     * Antes isto era um punhado de pares escritos à mão (gran_/gran2_,
     * bird_/bird2_, frases_/frases2_). Com três pássaros aquilo viraria
     * bagunça, e um quarto seria pior. Agrupado numa struct, somar pássaro
     * passa a ser mexer no kPassaros e na tabela de casas.
     */
    struct Passaro {
        uirapuru::GranularPlayer gran;
        std::vector<float>       sample;
        std::vector<Frase>       frases;
        // Suavizador próprio. Cada pássaro tem um alvo diferente agora, então
        // não dá mais pra ter um suavizador só com deslocamento fixo. São três
        // filtros de um polo, custo irrelevante.
        //
        // Todos ficam atualizados o tempo todo, inclusive os que estão calados:
        // assim, quando um é sorteado, ele já está na altura certa em vez de
        // entrar deslizando de onde parou.
        float                    cents_smooth = 0.0f;
    };

    Passaro      passaros_[Params::kPassaros];
    PitchTracker pitch_;

    float sr_ = 48000.0f;
    Params p_;

    float cents_coeff_  = 0.0f;
    float env_          = 0.0f;
    float env_atk_      = 0.0f;
    float env_rel_      = 0.0f;
    bool  have_pitch_   = false;

    // Altura congelada (modo TRAVA).
    //
    // Não travamos no instante do ataque: o detector precisa de ~110 ms pra ter
    // mediana confiável da nota nova, então travar na hora pegaria a altura da
    // nota ANTERIOR. A primeira tentativa fez isso e o resultado ficou preso em
    // 0 cents, porque no instante do ataque nem existe altura detectada.
    //
    // Em vez de esperar parado (o que daria atraso audível), ele SEGUE ao vivo
    // durante a janela de assentamento e só então congela. Assim não há atraso,
    // e a estabilidade vale onde importa: no corpo sustentado da nota, que é
    // onde o bend e o vibrato acontecem.
    float latched_hz_ = 0.0f;   // a NOTA travada, não mais os cents
    int   congela_em_ = 0;
    bool  congelado_     = false;

    // --- Detecção de ataque -----------------------------------------------
    //
    // Um envelope rápido contra um limiar mais lento.
    //
    // O envelope sobe em 1 ms; o limiar sobe em 30 ms e cai em 250 ms. Num
    // transiente novo o envelope dispara na frente e passa 30% do limiar, e é o
    // ataque. Passados uns 80 ms o limiar alcança o nível sustentado, a razão
    // volta perto de 1 e a mesma nota não dispara de novo.
    //
    // Quatro versões falharam antes desta, e o teste estatístico pegou todas:
    //   1. limiar fixo no envelope da dinâmica -> 1 em 400 (nunca rearmava)
    //   2. dois envelopes, ataques 1ms vs 50ms -> 4000 em 400 (disparava na nota)
    //   3. dois envelopes, ataque igual        -> 0 em 400 (subiam juntos)
    //   4. limiar seguindo o pico toda amostra -> 1 em 400 (o limiar subia atrás
    //      do envelope e ele nunca conseguia ficar à frente)
    // Só o primeiro ataque funcionava na 4, porque o limiar partia de zero.
    float on_fast_ = 0.0f;
    float on_fast_atk_ = 0.0f, on_fast_rel_ = 0.0f;
    float thresh_ = 0.0f, thresh_atk_ = 0.0f, thresh_rel_ = 0.0f;
    int   refract_ = 0;  // amostras restantes de bloqueio após um ataque

    // --- Sorteio e troca de pássaro ---------------------------------------
    //
    // A troca é sempre entre DOIS: o que estava tocando e o novo. Só isso é
    // preciso, mesmo com três ou mais pássaros, porque o bloqueio entre
    // ataques é de 80 ms e o crossfade leva 20, então uma troca sempre termina
    // antes da próxima começar.
    unsigned rng_ = 0x13579bdfu;
    int   de_   = 1;             // pássaro que está saindo
    int   para_ = 1;             // pássaro que está entrando (começa no uirapuru)
    float xfade_ = 1.0f;         // 0 = de_, 1 = para_
    float xfade_coeff_ = 0.0f;

    // --- Modo frase: chamada e descanso -----------------------------------
    bool  cantando_       = false;
    int   canto_restante_ = 0;  // amostras que faltam da chamada
    int   descanso_       = 0;  // amostras que faltam de silêncio
    int   frase_ant_      = -1; // última frase tocada, pra não repetir seguido
    float gate_           = 0.0f;   // portão da chamada, 0..1
    float gate_coeff_     = 0.0f;   // subida/descida de ~12 ms
    float nivel_canto_    = 1.0f;   // dinâmica capturada no ataque
    int   mede_nivel_     = 0;      // amostras restantes medindo a força do toque
    float min_ms_usado_   = -1.0f;  // piso com que as frases foram cortadas

    // Contadores de diagnóstico (usados pelo teste estatístico).
    int   onsets_    = 0;
    int   ignorados_ = 0;
    int   picks_[Params::kPassaros] = {0, 0, 0};
    float peso_now_[Params::kPassaros] = {0.0f, 0.0f, 0.0f};
    float alvo_now_ = 0.0f;
    int   k_now_    = 0;
};
