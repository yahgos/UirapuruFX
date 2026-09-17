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
        // falsos positivos de graça. Medi de 18 a 28 ignorados por trecho, sem
        // nenhum efeito audível. É por isso que o par depende do modo, em vez
        // de ser um só valor de compromisso.
        float onset_env_rel_ms   = 40.0f;   // contínuo
        float onset_rel_ms       = 250.0f;
        float onset_env_frase_ms = 12.0f;   // frase
        float onset_rel_frase_ms = 25.0f;

        // A altura mediana do próprio sample: é a referência de onde toda a
        // transposição é medida.
        //
        // Este valor vale pra gravação de uirapuru que eu uso. Se você trocar
        // o arquivo, meça a mediana do seu e ajuste aqui, senão o pássaro sai
        // afinado em relação à referência errada.
        float bird_ref_hz = 1828.0f;

        // Quantas oitavas acima da sua nota o pássaro canta. Em oitavas
        // inteiras, então ele fica SEMPRE na mesma classe de nota que você está
        // tocando (nunca sai do tom), e nota mais aguda sempre dá pássaro mais
        // agudo. Fixado em 2, acima disso fica estranho.
        int octave_offset = 2;

        // Teto do pássaro, em Hz.
        //
        // Descoberta importante: este sample só soa como pássaro quando puxado
        // pra BAIXO. Subir estica o espectro pra uma faixa onde a gravação não
        // tem energia, e o resultado fica fino e sibilante, é o que acontecia
        // acima de A4.
        //
        // Com o teto, quando o pássaro passaria daqui ele desce uma OCTAVA
        // INTEIRA em vez de continuar subindo. Oitava inteira mantém a mesma
        // classe de nota, então nunca sai do tom. 0 desliga o teto.
        float ceiling_hz = 1850.0f;

        // O quanto o pássaro acompanha a dinâmica do seu toque.
        float dynamics = 1.0f;

        // --- SEGUNDO PÁSSARO: o bem-te-vi ---------------------------------
        //
        // A cada nota que você toca é sorteado quem responde. A chance não é
        // fixa: cresce conforme você sobe o braço, porque o bem-te-vi é +817
        // cents mais agudo que o uirapuru e sofre menos ao ser transposto pra
        // cima. No centro do braço (~C4) a chance dá justo 25%.
        // O knob do plugin: o quanto o bem-te-vi aparece, 0..1, medido no
        // CENTRO do braço (~269 Hz). Zero desliga ele por completo.
        //
        // Por que medir no centro: é ali que você passa a maior parte do tempo,
        // então o número corresponde ao que o ouvido percebe como "quanto ele
        // aparece". A inclinação por região se aplica em volta disso.
        float bird2_chance = 0.125f;

        // Altura mediana medida no sample do bem-te-vi.
        float bird2_ref_hz = 2930.0f;

        // Ganho de correção do bem-te-vi.
        //
        // Os dois arquivos foram normalizados separadamente, e as gravações
        // têm densidades bem diferentes: o bem-te-vi é um grito curto e cheio,
        // o uirapuru é um assobio fino. Medindo só as partes com som:
        //
        //             pico    RMS
        //   uirapuru  0,324   -27,4 dBFS
        //   bem-te-vi 0,980   -16,0 dBFS     +11,5 dB mais alto
        //
        // Ou seja: toda vez que o bem-te-vi era sorteado ele entrava quase
        // quatro vezes mais forte. Isso estava aí desde que ele foi adicionado
        // e é parte do motivo de ele parecer aparecer demais. Mexemos na
        // probabilidade, mas o volume continuava desigual.
        //
        // Os valores foram calibrados na SAÍDA renderizada, não no arquivo:
        // o bem-te-vi é transposto bem mais pra baixo, o que muda a energia
        // dele. Medindo o RMS da saída em sete notas de E2 a A4, com cada
        // pássaro forçado:
        //
        //   contínuo   bem-te-vi +11,6 dB  ->  ganho 0,262
        //   frase      bem-te-vi  +7,1 dB  ->  ganho 0,443
        //
        // Os modos pedem valores diferentes porque no modo frase o uirapuru
        // toca só os trechos que a segmentação marcou como canto, que são os
        // mais fortes dele; no contínuo ele varre o arquivo inteiro, partes
        // fracas incluídas, e a média cai.
        //
        // Confere: o 0,262 do contínuo bate com 1/3,74, a razão dos RMS dos
        // dois arquivos. Duas medidas independentes chegando no mesmo número.
        float bird2_gain       = 0.262f;  // contínuo
        float bird2_gain_frase = 0.443f;  // frase

        // Uma oitava extra só pro bem-te-vi.
        //
        // Como a referência dele é mais alta, na mesma nota ele seria puxado ~8
        // semitons mais pra baixo que o uirapuru (em A3: -2085 contra -1266
        // cents), o que pode sair grave demais. Subir uma oitava aproveita que
        // ele é o pássaro agudo, e sendo oitava inteira, continua no tom.
        int bird2_octave_bonus = 1;

        // A inclinação por região: esta parte é CARACTERÍSTICA, não knob.
        //
        // Multiplicadores aplicados sobre bird2_chance: no grave ele aparece
        // 40% do valor do knob, no agudo 160%. No centro vale exatamente 1,0,
        // que é o que faz o knob significar o que diz.
        //
        // A inclinação existe porque o bem-te-vi é +817 cents mais agudo e
        // sofre menos ao ser transposto pra cima, faz sentido ele aparecer
        // mais quando você sobe o braço.
        float bird2_tilt_lo = 0.40f;  // multiplicador em E2
        float bird2_tilt_hi = 1.60f;  // multiplicador em A5
        float chance_f_lo   = 82.41f; // E2
        float chance_f_hi   = 880.0f; // A5

        // Força um pássaro só, pra comparar de ouvido. 0 = sorteia normalmente.
        int force_bird = 0;  // 1 = sempre uirapuru, 2 = sempre bem-te-vi
    };

    /** Inicializa com o sample principal (uirapuru).
     *
     * O segundo pássaro é opcional: sem ele o motor funciona exatamente como
     * antes. Chame SetSecondBird() depois pra ligar o sorteio.
     */
    void Init(float sample_rate, const float* bird, int bird_len);

    /** Carrega o sample do segundo pássaro (bem-te-vi). */
    void SetSecondBird(const float* bird, int bird_len);
    bool has_second_bird() const { return !bird2_.empty(); }
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
    float transposition_cents() const { return cents_smooth_; }

    /** Qual pássaro está tocando agora (pra conferir o sorteio). */
    bool  on_second_bird() const { return want_bird2_; }
    int   onset_count() const { return onsets_; }
    int   bird2_count() const { return picks2_; }
    float chance_now() const { return chance_now_; }

    /** Modo frase: ataques que chegaram com o pássaro ocupado e foram
     *  ignorados. É a medida de quanto o espaçamento está filtrando. */
    int   ignored_count() const { return ignorados_; }
    /** Modo frase: true enquanto uma chamada está no ar. */
    bool  singing() const { return cantando_; }
    /** Quantas frases a segmentação achou em cada canto. */
    int   phrase_count(int bird) const {
        return (int)(bird == 2 ? frases2_.size() : frases_.size());
    }
    /** Início e duração de uma frase, em amostras (pra conferência). */
    void  phrase_at(int bird, int i, int& ini, int& len) const {
        const std::vector<Frase>& f = (bird == 2 ? frases2_ : frases_);
        ini = f[i].ini;
        len = f[i].len;
    }

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

    float TargetCents(float played_hz) const;

    /** Chance de o bem-te-vi responder a uma nota nesta altura, 0..1. */
    float ChanceOfBird2(float played_hz) const;

    /** Sorteio próprio (xorshift32).
     *
     * Não usamos rand(): ele tem estado global e não é seguro de chamar da
     * thread de áudio. Este é determinístico, sem alocação, e roda igual no
     * desktop e na Daisy.
     */
    float NextRandom();

    uirapuru::GranularPlayer gran_;   // uirapuru
    uirapuru::GranularPlayer gran2_;  // bem-te-vi
    PitchTracker            pitch_;
    std::vector<float>      bird_;
    std::vector<float>      bird2_;

    float sr_ = 48000.0f;
    Params p_;

    float cents_smooth_ = 0.0f;
    float cents_coeff_  = 0.0f;
    float env_          = 0.0f;
    float env_atk_      = 0.0f;
    float env_rel_      = 0.0f;
    bool  have_pitch_   = false;

    // Diferença constante de cents entre os dois pássaros. Como a frequência
    // alvo é a mesma, basta um deslocamento fixo, assim eles nunca se
    // desencontram. Calculado no Init.
    float cents2_offset_ = 0.0f;

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
    float latched_cents_ = 0.0f;
    int   congela_em_    = 0;
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

    // Sorteio e crossfade entre os dois pássaros.
    unsigned rng_        = 0x13579bdfu;
    bool     want_bird2_ = false;
    float    xfade_      = 0.0f;  // 0 = uirapuru, 1 = bem-te-vi
    float    xfade_coeff_ = 0.0f;

    // --- Modo frase: chamada e descanso -----------------------------------
    std::vector<Frase> frases_, frases2_;
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
    int   onsets_     = 0;
    int   ignorados_  = 0;
    int   picks2_     = 0;
    float chance_now_ = 0.0f;
};
