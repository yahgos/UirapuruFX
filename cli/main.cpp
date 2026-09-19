// UirapuruFX: renderizador offline.
//
//   uirapuru render <passaro.wav> <guitarra.wav> <saida.wav> [opções]
//   uirapuru render <passaro.wav> --synth <saida.wav> [opções]
//   uirapuru analyse <passaro.wav>
//
// Roda o mesmo BirdEngine que roda no pedal, então o que você ouve aqui é o que
// o pedal faz. Toda opção é em tempo de execução, dá pra comparar A/B sem
// recompilar nada.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>

#include "bird_engine.h"
#include "wav.h"

namespace {

void usage() {
    // Nome de comando e de opção em inglês, como o resto do código. O que é
    // texto pra você ler fica em português.
    printf(
        "uso:\n"
        "  uirapuru render <passaro.wav> <guitarra.wav> <saida.wav> [opcoes]\n"
        "  uirapuru render <passaro.wav> --synth <saida.wav> [opcoes]\n"
        "  uirapuru analyse <passaro.wav>            dados basicos do arquivo\n"
        "  uirapuru phrases <passaro.wav> [ms]       mostra as frases detectadas\n"
        "  uirapuru pitchtest <qualquer.wav>         confere o detector de altura\n"
        "  uirapuru chordtest <qualquer.wav>         o detector diante de acordes\n"
        "  uirapuru birdstats <uirap.wav> <btv.wav> <mau.wav>  confere o sorteio\n"
        "\nentrada de teste (guitarra sintetica):\n"
        "  --synth             escala pronta, pra quando nao tem gravacao\n"
        "  --tone <hz>         nota parada, pra medir a afinacao isolada\n"
        "  --chord <nome>      e5|emaj|eopen|amopen|ce|note\n"
        "  --progression       nota -> power chord -> acorde aberto -> nota\n"
        "  --climb             sobe de E3 ate D6, mostra o teto agindo\n"
        "  --solo <nps>        corrida rapida a N notas por segundo\n"        "  --strum <bpm>       levada com troca de acorde (Am Dm E Am)\n"
        "  --bend <semis>      uma nota puxada N semitons, testa slide e bend\n"
        "\ncontroles do pedal:\n"
        "  --mix <0..1>        guitarra seca contra passaro (padrao 0,5)\n"
        "  --speed <x>         ritmo da frase (padrao 0,52, 1 = natural)\n"
        "  --grain <ms>        tamanho do grao; pequeno = tagarela (padrao 80)\n"
        "\naltura:\n"
        "  --octaves <n>       oitavas acima da sua nota (padrao 2)\n"
        "  --ceiling <hz>      acima daqui o alvo desce uma oitava (padrao 4000)\n"
        "  --floor <hz>        abaixo daqui o alvo sobe uma oitava (padrao 550)\n"
        "  --no-ceiling        desliga o teto\n"
        "  --no-floor          desliga o piso\n"
        "  --no-follow         congela o passaro na altura natural dele\n"
        "  --latch             prende a altura no ataque e segura (padrao)\n"
        "  --glide             acompanha bend e slide continuamente\n"
        "  --step              comportamento antigo: pula de semitom em semitom\n"
        "\nmodo frase (uma chamada por ataque, depois descanso):\n"
        "  --calls             uma frase inteira por ataque, depois silencio\n"
        "  --chords            responde por ACORDE, nao por batida\n"
        "  --chord-stable <ms> quanto a harmonia precisa firmar (padrao 400)\n"
        "  --chord-hold        no modo acorde, canto continuo em vez de frase\n"
        "  --continuous        o passaro canta sem parar (padrao)\n"
        "  --rest <ms>         descanso entre chamadas (padrao 400)\n"
        "  --call-max <ms>     teto de uma chamada, medido no arquivo (padrao 1500)\n"
        "  --call-min <ms>     menor pedaco que conta como frase (padrao 150)\n"
        "\nos tres passaros:\n"
        "  o primeiro posicional e' o uirapuru, casa em 1823 Hz. quem responde a\n"
        "  cada nota depende de qual casa esta mais perto do alvo.\n"
        "  --mau <wav>         carrega o Mau, o grave (casa em 790 Hz)\n"
        "  --bird2 <wav>       carrega o bem-te-vi, o agudo (casa em 2853 Hz)\n"
        "  --variety <0..1>    o quanto os passaros se misturam (padrao 0,55)\n"
        "  --ref-mau <hz>      casa do Mau\n"
        "  --ref <hz>          casa do uirapuru\n"
        "  --ref2 <hz>         casa do bem-te-vi\n"
        "  --gain-mau <x>      volume do Mau (padrao 0,157; 0,177 no modo frase)\n"
        "  --gain2 <x>         volume do bem-te-vi (padrao 0,262; 0,443 no frase)\n"
        "  --force-mau         sempre o Mau\n"
        "  --force-uira        sempre o uirapuru\n"
        "  --force-bemtevi     sempre o bem-te-vi\n"
        "\noutros:\n"
        "  --bird-rate <hz>    simula guardar o passaro nesta taxa (ex: 24000)\n"
        "  --phrase <n>        1|2|3 pra usar uma frase so do arquivo\n"
        "  --dynamics <0..1>   o quanto o passaro segue a forca do seu toque\n"
        "  --no-quantize       o mesmo que --glide\n"
        "  --normalize         sobe o passaro pro fundo de escala antes de usar\n"
        "  --seed <n>          fixa o sorteio, pra rodar sempre igual\n"
        "  --trace             imprime altura detectada e transposicao no tempo\n"
        "  --onset-env <ms>    queda do envelope do detector (bancada de teste)\n"
        "  --onset-rel <ms>    queda do limiar do detector (bancada de teste)\n");
}

float arg_f(const char* v) { return (float)atof(v); }

/** Alinha uma coluna de texto contando LETRAS, não bytes.
 *
 * O %-24s do printf conta bytes, e em UTF-8 um "í" ocupa dois. Com os nomes
 * traduzidos as tabelas saíam tortas. Isto conta só os bytes que iniciam um
 * caractere (os de continuação têm o padrão 10xxxxxx) e completa o resto com
 * espaços.
 */
std::string col(const std::string& txt, int largura) {
    int letras = 0;
    for (unsigned char c : txt)
        if ((c & 0xC0) != 0x80) letras++;
    std::string r = txt;
    for (int i = letras; i < largura; i++) r += ' ';
    return r;
}

// Limites das frases da gravação que eu uso, em segundos. Serve pro --phrase,
// que toca uma frase só em vez do arquivo inteiro. Com outro arquivo estes
// números não querem dizer nada: rode `uirapuru phrases` no seu, que a
// segmentação automática acha as frases de verdade.
struct Phrase { float start, end; };
const Phrase kPhrases[3] = {{0.00f, 3.31f}, {3.67f, 6.57f}, {7.00f, 8.19f}};

void normalize(std::vector<float>& v) {
    float peak = 0.0f;
    for (float s : v) peak = fmaxf(peak, fabsf(s));
    if (peak <= 1e-6f) return;
    const float g = 0.98f / peak;
    for (float& s : v) s *= g;
}

// Bate um acorde dentro do buffer: uma serra por corda, levemente
// desencontradas pra soar palhetado e não ligado de uma vez, cada uma
// decaindo como corda de verdade.
void add_chord(std::vector<float>& buf, int sr, float t0, float dur, const float* f, int n) {
    for (int k = 0; k < n; k++) {
        const float onset = t0 + 0.014f * k;   // espalhamento da batida
        const int   start = (int)(onset * sr);
        const int   len   = (int)(dur * sr);
        float       phase = 0.0f;
        // Um tiquinho de desafinação por corda evita aquele som sintético.
        const float freq = f[k] * (1.0f + 0.0007f * ((k % 3) - 1));
        for (int i = 0; i < len; i++) {
            const size_t idx = (size_t)(start + i);
            if (idx >= buf.size()) break;
            const float t   = i / (float)sr;
            const float env = expf(-t * 1.1f) * (1.0f - expf(-t * 400.0f));
            phase += freq / sr;
            if (phase >= 1.0f) phase -= 1.0f;
            buf[idx] += 0.45f / n * (2.0f * phase - 1.0f) * env;
        }
    }
}

struct NamedChord { const char* name; int n; float f[6]; };
static const NamedChord kChords[] = {
    {"e5",     2, {82.41f, 123.47f}},
    {"emaj",   3, {82.41f, 103.83f, 123.47f}},
    {"eopen",  6, {82.41f, 123.47f, 164.81f, 207.65f, 246.94f, 329.63f}},
    {"amopen", 5, {110.0f, 164.81f, 220.0f, 261.63f, 329.63f}},
    {"ce",     2, {261.63f, 329.63f}},
    {"note",   1, {82.41f}},
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) { usage(); return 1; }
    const std::string cmd = argv[1];
    std::string err;

    Audio bird;
    if (!wav_read(argv[2], bird, err)) { printf("erro: %s\n", err.c_str()); return 1; }

    if (cmd == "analyse") {
        float peak = 0.0f;
        for (float s : bird.samples) peak = fmaxf(peak, fabsf(s));
        printf("pássaro: %zu amostras @ %d Hz (%.3f s), pico %.3f\n", bird.samples.size(),
               bird.sample_rate, bird.samples.size() / (float)bird.sample_rate, peak);
        return 0;
    }
    if (cmd == "phrases") {
        // Mostra as frases que a segmentacao do motor achou. E' a tabela que o
        // modo frase usa pra escolher o que tocar em cada ataque.
        const float min_ms = argc > 3 ? atof(argv[3]) : 150.0f;
        BirdEngine e;
        BirdEngine::Params pp;
        pp.frase_min_ms = min_ms;
        e.Init((float)bird.sample_rate);
        e.SetParams(pp);
        e.SetBird(1, bird.samples.data(), (int)bird.samples.size());
        const float sr = (float)bird.sample_rate;
        printf("%s   %.2f s   piso %.0f ms\n", argv[2], bird.samples.size() / sr, min_ms);
        printf("%-4s %9s %9s %9s\n", "n", "inicio", "dur(ms)", "pausa(ms)");
        int fim_ant = -1;
        double soma = 0.0;
        for (int i = 0; i < e.phrase_count(1); i++) {
            int ini, len;
            e.phrase_at(1, i, ini, len);
            const float p_ms = fim_ant < 0 ? -1.0f : (ini - fim_ant) / sr * 1000.0f;
            printf("%-4d %8.3fs %9.0f ", i + 1, ini / sr, len / sr * 1000.0f);
            if (p_ms < 0) printf("%9s\n", "-"); else printf("%9.0f\n", p_ms);
            soma += len / sr * 1000.0f;
            fim_ant = ini + len;
        }
        printf("\n%d frases   soma %.2f s (%.0f%% do arquivo)\n", e.phrase_count(1),
               soma / 1000.0, 100.0 * soma / 1000.0 / (bird.samples.size() / sr));
        return 0;
    }
    if (cmd == "pitchtest") {
        // Joga serras sintéticas de altura conhecida e vê o que volta.
        // Serra e não senoide, porque guitarra é rica em harmônicos e um
        // detector que só funciona em tom puro não serviria pra nada aqui.
        const float sr = 48000.0f;
        const float notes[8] = {82.41f,  110.0f,  146.83f, 196.0f,
                                246.94f, 329.63f, 440.0f,  659.25f};
        const char* names[8] = {"E2", "A2", "D3", "G3", "B3", "E4", "A4", "E5"};
        printf("%-5s %10s %10s %9s %7s\n", "nota", "esperado", "detectado", "erro", "conf");
        int bad = 0;
        for (int n = 0; n < 8; n++) {
            PitchTracker pt;
            pt.Init(sr);
            float phase = 0.0f;
            for (int i = 0; i < (int)(sr * 0.5f); i++) {
                phase += notes[n] / sr;
                if (phase >= 1.0f) phase -= 1.0f;
                pt.Process(0.4f * (2.0f * phase - 1.0f));
            }
            const float got = pt.frequency();
            const float cents = 1200.0f * log2f(got / notes[n]);
            const bool ok = fabsf(cents) < 50.0f;
            if (!ok) bad++;
            printf("%-5s %9.2f %10.2f %+7.0f c %7.2f  %s\n", names[n], notes[n], got, cents,
                   pt.confidence(), ok ? "ok" : "FAIL");
        }
        printf("\n%d de 8 dentro de 50 cents\n", 8 - bad);
        return bad ? 1 : 0;
    }
    if (cmd == "chordtest") {
        // O que um detector monofônico faz de verdade quando você toca um
        // acorde? Sintetiza acordes de guitarra como soma de serras e observa
        // a altura detectada ao longo do tempo, tanto no que ele trava quanto
        // no quão firme fica.
        const float sr = 48000.0f;
        struct Chord { const char* name; int n; float f[6]; };
        const Chord chords[] = {
            {"E5 power (E2+B2)",       2, {82.41f, 123.47f}},
            {"tríade de E maior",          3, {82.41f, 103.83f, 123.47f}},
            {"E aberto (6 cordas)",     6, {82.41f, 123.47f, 164.81f, 207.65f, 246.94f, 329.63f}},
            {"A menor aberto",           5, {110.0f, 164.81f, 220.0f, 261.63f, 329.63f}},
            {"díade C+E (terça)",         2, {261.63f, 329.63f}},
            {"nota solta E2 (referência)",   1, {82.41f}},
        };
        printf("%s %10s %8s %8s %7s  %s\n", col("acorde", 24).c_str(), "detectado", "min", "max", "conf", "veredito");
        for (const auto& ch : chords) {
            PitchTracker pt;
            pt.Init(sr);
            float ph[6] = {0, 0, 0, 0, 0, 0};
            std::vector<float> ests;
            float conf_sum = 0.0f;
            int   conf_n = 0;
            for (int i = 0; i < (int)(sr * 1.5f); i++) {
                float s = 0.0f;
                for (int k = 0; k < ch.n; k++) {
                    ph[k] += ch.f[k] / sr;
                    if (ph[k] >= 1.0f) ph[k] -= 1.0f;
                    s += (2.0f * ph[k] - 1.0f);
                }
                s *= 0.4f / ch.n;
                if (pt.Process(s) && i > (int)(sr * 0.2f) && pt.stable_ready()) {
                    ests.push_back(pt.stable_frequency());
                    conf_sum += pt.confidence();
                    conf_n++;
                }
            }
            if (ests.empty()) { printf("%s   (nenhuma estimativa)\n", col(ch.name, 24).c_str()); continue; }
            std::sort(ests.begin(), ests.end());
            const float med = ests[ests.size() / 2];
            const float lo = ests.front(), hi = ests.back();
            const float conf = conf_n ? conf_sum / conf_n : 0.0f;
            const float spread_semis = 12.0f * log2f(hi / lo);
            const char* verdict = spread_semis < 0.5f ? "firme"
                                : spread_semis < 2.0f ? "treme um pouco" : "PULA PRA TUDO QUANTO E LADO";
            printf("%s %9.1fHz %7.0f %8.0f %7.2f  %s (%.1f semitons de espalhamento)\n", col(ch.name, 24).c_str(), med, lo, hi,
                   conf, verdict, spread_semis);
        }
        return 0;
    }
    if (cmd == "birdstats") {
        // Verifica o sorteio sem depender de ouvido: dispara centenas de
        // ataques em varias alturas e conta quem respondeu.
        //
        // argv[2] e' o uirapuru (lido no comeco do main), depois o bem-te-vi e
        // o Mau. Essa ordem mantem os comandos antigos funcionando.
        if (argc < 5) {
            printf("uso: uirapuru birdstats <uirapuru.wav> <bem-te-vi.wav> <mau.wav>\n");
            return 1;
        }
        Audio b2, b0;
        if (!wav_read(argv[3], b2, err)) { printf("erro: %s\n", err.c_str()); return 1; }
        if (!wav_read(argv[4], b0, err)) { printf("erro: %s\n", err.c_str()); return 1; }

        const float sr = 48000.0f;
        const struct { const char* nome; float hz; } alvos[] = {
            {"E2", 82.41f},  {"G2", 98.0f},   {"E3", 164.81f}, {"G3", 196.0f},
            {"C4", 261.63f}, {"E4", 329.63f}, {"A4", 440.0f},  {"C5", 523.25f},
            {"E5", 659.25f}, {"A5", 880.0f}};

        // Monta um motor com os tres cantos carregados.
        auto monta = [&](BirdEngine& e, const BirdEngine::Params& pp, unsigned semente) {
            e.Init(sr);
            e.SetParams(pp);
            e.SetBird(0, b0.samples.data(), (int)b0.samples.size());
            e.SetBird(1, bird.samples.data(), (int)bird.samples.size());
            e.SetBird(2, b2.samples.data(), (int)b2.samples.size());
            e.SetSeed(semente);
        };
        // 400 palhetadas: 0,25 s de nota e 0,25 s de silencio, pra rearmar.
        auto toca = [&](BirdEngine& e, float hz, int vezes) {
            const int nota = (int)(sr * 0.25f), pausa = (int)(sr * 0.25f);
            float ph = 0.0f;
            for (int k = 0; k < vezes; k++) {
                for (int i = 0; i < nota; i++) {
                    ph += hz / sr;
                    if (ph >= 1.0f) ph -= 1.0f;
                    e.Process(0.4f * (2.0f * ph - 1.0f));
                }
                for (int i = 0; i < pausa; i++) e.Process(0.0f);
            }
        };

        printf("Quem responde, por altura. Variedade no padrao.\n");
        printf("%-5s %7s %5s %19s %19s\n", "nota", "alvo", "K",
               "medido (Mau/uir/btv)", "peso  (Mau/uir/btv)");
        for (const auto& t : alvos) {
            BirdEngine e;
            BirdEngine::Params pp;
            monta(e, pp, 12345);
            toca(e, t.hz, 400);
            const int n = e.onset_count();
            printf("%-5s %6.0fHz %5d   %5.0f%% %5.0f%% %5.0f%%   %5.0f%% %5.0f%% %5.0f%%\n",
                   t.nome, e.target_hz(), e.octaves_now(),
                   n ? 100.0f * e.pick_count(0) / n : 0.0f,
                   n ? 100.0f * e.pick_count(1) / n : 0.0f,
                   n ? 100.0f * e.pick_count(2) / n : 0.0f,
                   100.0f * e.weight_now(0), 100.0f * e.weight_now(1),
                   100.0f * e.weight_now(2));
        }

        // O knob de variedade significa o que diz? Ele promete que em 0 sempre
        // sai o passaro mais perto de casa, e que subindo os vizinhos entram.
        // Medimos a media sobre o braco todo.
        printf("\nKnob de variedade, media sobre as %zu alturas acima:\n",
               sizeof(alvos) / sizeof(alvos[0]));
        printf("%10s %11s %9s %10s\n", "variedade", "dominante", "segundo", "terceiro");
        const float vars[] = {0.0f, 0.3f, 0.55f, 0.75f, 1.0f};
        for (float v : vars) {
            double d = 0, s2 = 0, s3 = 0;
            int notas = 0;
            for (const auto& t : alvos) {
                BirdEngine e;
                BirdEngine::Params pp;
                pp.variedade = v;
                monta(e, pp, 999);
                toca(e, t.hz, 200);
                const int n = e.onset_count();
                if (!n) continue;
                float f[3] = {100.0f * e.pick_count(0) / n,
                              100.0f * e.pick_count(1) / n,
                              100.0f * e.pick_count(2) / n};
                std::sort(f, f + 3, std::greater<float>());
                d += f[0]; s2 += f[1]; s3 += f[2]; notas++;
            }
            if (notas)
                printf("%9.0f%% %10.0f%% %8.0f%% %9.0f%%\n", v * 100.0f,
                       d / notas, s2 / notas, s3 / notas);
        }
        return 0;
    }
    if (cmd != "render") { usage(); return 1; }

    // --- lê os argumentos ---
    std::string guitar_path, out_path;
    bool synth = false;
    float tone_hz = 0.0f;
    std::string chord_name;
    bool progression = false;
    bool climb = false;
    float strum_bpm = 0.0f;
    float solo_nps = 0.0f;
    float bend_semis = 0.0f;
    std::string bird2_path, mau_path;
    unsigned seed = 0;
    bool trace = false;
    int bird_rate = 0, phrase = 0;
    bool do_normalize = false;
    BirdEngine::Params p;

    std::vector<std::string> pos;
    for (int i = 3; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : "0"; };
        if (a == "--synth")             synth = true;
        else if (a == "--tone")         { synth = true; tone_hz = arg_f(next()); }
        else if (a == "--chord")        { synth = true; chord_name = next(); }
        else if (a == "--progression")  { synth = true; progression = true; }
        else if (a == "--climb")        { synth = true; climb = true; }
        else if (a == "--strum")        { synth = true; strum_bpm = arg_f(next()); }
        else if (a == "--solo")         { synth = true; solo_nps = arg_f(next()); }
        else if (a == "--bend")         { synth = true; bend_semis = arg_f(next()); }
        else if (a == "--bird-rate")    bird_rate = atoi(next());
        else if (a == "--phrase")       phrase = atoi(next());
        else if (a == "--speed")        p.speed = arg_f(next());
        else if (a == "--mix")          p.mix = arg_f(next());
        else if (a == "--grain")        p.grain_ms = arg_f(next());
        else if (a == "--octaves")      p.octave_offset = atoi(next());
        else if (a == "--ref")          p.casa_hz[1] = arg_f(next());
                else if (a == "--dynamics")     p.dynamics = arg_f(next());
        else if (a == "--no-follow")    p.pitch_follow = false;
        else if (a == "--no-quantize")  p.modo_altura = BirdEngine::Params::DESLIZA;
        else if (a == "--latch")        p.modo_altura = BirdEngine::Params::TRAVA;
        else if (a == "--glide")        p.modo_altura = BirdEngine::Params::DESLIZA;
        else if (a == "--step")         p.modo_altura = BirdEngine::Params::DEGRAU;
        else if (a == "--calls")       p.modo_disparo = BirdEngine::Params::FRASE;
        else if (a == "--continuous")     p.modo_disparo = BirdEngine::Params::CONTINUO;
        else if (a == "--chords")         p.modo_disparo = BirdEngine::Params::ACORDE;
        else if (a == "--chord-stable" && i + 1 < argc) p.acorde_estavel_ms = atof(argv[++i]);
        else if (a == "--chord-hold")     p.acorde_com_frase = false;
        else if (a == "--rest" && i + 1 < argc)    p.espaco_ms = atof(argv[++i]);
        else if (a == "--call-max" && i + 1 < argc) p.frase_max_ms = atof(argv[++i]);
        else if (a == "--call-min" && i + 1 < argc) p.frase_min_ms = atof(argv[++i]);
        // Mexem nos DOIS pares de uma vez: aqui é bancada de teste, e foi
        // com estas flags que a tabela de notas/segundo foi levantada.
        else if (a == "--onset-rel" && i + 1 < argc)
            { p.onset_rel_ms = p.onset_rel_frase_ms = atof(argv[++i]); }
        else if (a == "--onset-env" && i + 1 < argc)
            { p.onset_env_rel_ms = p.onset_env_frase_ms = atof(argv[++i]); }
        else if (a == "--no-ceiling")   p.teto_hz = 0.0f;
        else if (a == "--bird2")        bird2_path = next();
        else if (a == "--mau")          mau_path = next();
        else if (a == "--variety")      p.variedade = arg_f(next());
        else if (a == "--floor")        p.piso_hz = arg_f(next());
        else if (a == "--no-floor")     p.piso_hz = 0.0f;
        else if (a == "--force-mau")     p.force_bird = 1;
        else if (a == "--force-uira")    p.force_bird = 2;
        else if (a == "--force-bemtevi") p.force_bird = 3;
        else if (a == "--ref-mau")      p.casa_hz[0] = arg_f(next());
        else if (a == "--ref2")         p.casa_hz[2] = arg_f(next());
        else if (a == "--gain-mau")     { p.ganho[0] = p.ganho_frase[0] = arg_f(next()); }
        else if (a == "--gain2")        { p.ganho[2] = p.ganho_frase[2] = arg_f(next()); }
        else if (a == "--seed")         seed = (unsigned)atoi(next());
        else if (a == "--ceiling")      p.teto_hz = arg_f(next());
        else if (a == "--normalize")    do_normalize = true;
        else if (a == "--trace")        trace = true;
        else if (a.rfind("--", 0) == 0) { printf("opção desconhecida %s\n", a.c_str()); return 1; }
        else pos.push_back(a);
    }

    if (synth) {
        if (pos.size() != 1) { usage(); return 1; }
        out_path = pos[0];
    } else {
        if (pos.size() != 2) { usage(); return 1; }
        guitar_path = pos[0];
        out_path = pos[1];
    }

    // --- prepara o sample do pássaro ---
    if (phrase >= 1 && phrase <= 3) {
        const size_t a = (size_t)(kPhrases[phrase - 1].start * bird.sample_rate);
        const size_t b = (size_t)(kPhrases[phrase - 1].end * bird.sample_rate);
        if (b <= bird.samples.size() && a < b)
            bird.samples = std::vector<float>(bird.samples.begin() + a, bird.samples.begin() + b);
    }
    if (do_normalize) normalize(bird.samples);
    if (bird_rate > 0) simulate_storage_rate(bird, bird_rate);

    // --- prepara a entrada de guitarra ---
    Audio guitar;
    if (synth) {
        // Uma serra meio palhetada subindo uma escala, pra ouvir o pássaro
        // acompanhar a altura sem precisar de gravação nenhuma.
        guitar.sample_rate = bird.sample_rate;
        const int sr = guitar.sample_rate;
        if (bend_semis != 0.0f) {
            // Uma nota só, subindo continuamente N semitons em 2 s. É o caso do
            // bend e do slide: a altura nunca para de mudar.
            const float f0 = 220.0f;
            guitar.samples.assign((size_t)(sr * 3), 0.0f);
            float ph = 0.0f;
            for (size_t i = 0; i < guitar.samples.size(); i++) {
                const float t = i / (float)sr;
                const float sweep = t < 2.0f ? (t / 2.0f) : 1.0f;
                const float f = f0 * powf(2.0f, bend_semis * sweep / 12.0f);
                const float env = 1.0f - expf(-t * 300.0f);
                ph += f / sr;
                if (ph >= 1.0f) ph -= 1.0f;
                guitar.samples[i] = 0.45f * (2.0f * ph - 1.0f) * env;
            }
        } else if (solo_nps > 0.0f) {
            // Solo rapido: uma corrida de escala a N notas por segundo. E' o
            // caso que embola o efeito, e o unico jeito de medir o espacamento
            // e' com um input assim.
            const float sc[8] = {220.0f,  246.94f, 261.63f, 293.66f,
                                 329.63f, 349.23f, 392.0f,  440.0f};
            const int   n     = 32;                    // notas na corrida
            const float passo = 1.0f / solo_nps;
            guitar.samples.assign((size_t)(sr * (0.2f + n * passo + 1.0f)), 0.0f);
            for (int i = 0; i < n; i++) {
                const float one[1] = {sc[i % 8] * (i < 16 ? 1.0f : 2.0f)};
                // nota dura 90% do passo, pra ficar um respiro entre ataques
                add_chord(guitar.samples, sr, 0.1f + i * passo, passo * 0.9f, one, 1);
            }
        } else if (strum_bpm > 0.0f) {
            // Levada com troca de acorde, pra testar o modo acorde.
            //
            // Precisa existir dentro do projeto: sem ela nao da' pra verificar
            // que batida repetida do mesmo acorde gera UMA resposta so'. As
            // entradas de acorde que ja' existiam batem sempre o mesmo acorde,
            // entao nao exercitam a troca de harmonia.
            const float col = 60.0f / strum_bpm / 2.0f;   // colcheia
            struct Levada { int n; float f[6]; };
            const Levada prog[4] = {
                {5, {110.0f, 164.81f, 220.0f, 261.63f, 329.63f}},           // Am
                {4, {146.83f, 220.0f, 293.66f, 349.23f, 0.0f, 0.0f}},       // Dm
                {6, {82.41f, 123.47f, 164.81f, 207.65f, 246.94f, 329.63f}}, // E
                {5, {110.0f, 164.81f, 220.0f, 261.63f, 329.63f}},           // Am
            };
            // 1 = acento, 0.6 = fraca, 0 = nao bate
            const float pad[8] = {1.0f, 0.0f, 0.6f, 0.6f, 1.0f, 0.0f, 0.6f, 0.6f};
            guitar.samples.assign((size_t)(sr * (4 * 8 * col + 1.5f)), 0.0f);
            float t = 0.2f;
            for (int c = 0; c < 4; c++)
                for (int b = 0; b < 8; b++) {
                    // add_chord ja' espalha as cordas no tempo, que e' o que
                    // faz uma batida so' gerar varios ataques.
                    if (pad[b] > 0.0f)
                        add_chord(guitar.samples, sr, t, 1.2f, prog[c].f, prog[c].n);
                    t += col;
                }
        } else if (climb) {
            // Sobe o braço: E3 até D6. É aqui que o problema das notas agudas
            // aparece, e onde dá pra ouvir o teto agindo.
            const float sc[10] = {164.81f, 220.0f, 261.63f, 329.63f, 440.0f,
                                  523.25f, 659.25f, 880.0f, 987.77f, 1174.66f};
            guitar.samples.assign((size_t)(sr * 10 * 1.2f), 0.0f);
            for (int i = 0; i < 10; i++) {
                const float one[1] = {sc[i]};
                add_chord(guitar.samples, sr, 0.1f + i * 1.2f, 1.15f, one, 1);
            }
        } else if (progression) {
            // nota -> power chord -> acorde aberto -> acorde aberto -> nota.
            // Dá pra ouvir o pássaro travar nas notas claras e ficar firme
            // durante os acordes que ele não consegue resolver.
            guitar.samples.assign((size_t)(sr * 12), 0.0f);
            const float e2[1]   = {82.41f};
            const float a2[1]   = {110.0f};
            add_chord(guitar.samples, sr, 0.2f,  2.0f, e2, 1);
            add_chord(guitar.samples, sr, 2.5f,  2.0f, kChords[0].f, kChords[0].n);  // e5
            add_chord(guitar.samples, sr, 5.0f,  2.0f, kChords[2].f, kChords[2].n);  // eopen
            add_chord(guitar.samples, sr, 7.5f,  2.0f, kChords[3].f, kChords[3].n);  // amopen
            add_chord(guitar.samples, sr, 10.0f, 1.8f, a2, 1);
        } else if (!chord_name.empty()) {
            const NamedChord* c = nullptr;
            for (const auto& k : kChords)
                if (chord_name == k.name) c = &k;
            if (!c) { printf("acorde desconhecido '%s'\n", chord_name.c_str()); return 1; }
            guitar.samples.assign((size_t)(sr * 8), 0.0f);
            for (int rep = 0; rep < 4; rep++)   // bate algumas vezes
                add_chord(guitar.samples, sr, 0.2f + rep * 2.0f, 1.9f, c->f, c->n);
        } else if (tone_hz > 0.0f) {
            // Serra constante numa só altura: dá pra verificar a transposição
            // sem a melodia do próprio pássaro atrapalhar a medição.
            guitar.samples.resize((size_t)(sr * 4));
            float ph = 0.0f;
            for (size_t i = 0; i < guitar.samples.size(); i++) {
                ph += tone_hz / sr;
                if (ph >= 1.0f) ph -= 1.0f;
                guitar.samples[i] = 0.4f * (2.0f * ph - 1.0f);
            }
        } else {
        const float notes[6] = {82.41f, 110.0f, 146.83f, 196.0f, 261.63f, 329.63f};
        guitar.samples.resize((size_t)(sr * 6));
        float phase = 0.0f;
        for (int n = 0; n < 6; n++) {
            const float f = notes[n];
            for (int i = 0; i < sr; i++) {
                const float t = i / (float)sr;
                const float env = expf(-t * 2.5f);
                phase += f / sr;
                if (phase >= 1.0f) phase -= 1.0f;
                guitar.samples[n * sr + i] = 0.5f * (2.0f * phase - 1.0f) * env;
            }
        }
        }
    } else {
        if (!wav_read(guitar_path, guitar, err)) { printf("erro: %s\n", err.c_str()); return 1; }
    }

    if (guitar.sample_rate != bird.sample_rate)
        printf("aviso: guitarra em %d Hz, pássaro em %d Hz - renderizando na taxa da guitarra\n",
               guitar.sample_rate, bird.sample_rate);

    // --- renderiza ---
    //
    // SetParams antes dos SetBird de proposito: o piso de segmentacao e as
    // casas precisam estar no lugar quando cada canto for carregado e cortado
    // em frases.
    BirdEngine engine;
    engine.Init((float)guitar.sample_rate);
    engine.SetParams(p);
    if (seed) engine.SetSeed(seed);

    // O primeiro posicional e' o uirapuru (indice 1), o que mantem todo comando
    // antigo funcionando. Mau e bem-te-vi entram por flag.
    engine.SetBird(1, bird.samples.data(), (int)bird.samples.size());

    Audio bird2, mau;
    if (!bird2_path.empty()) {
        if (!wav_read(bird2_path, bird2, err)) { printf("erro: %s\n", err.c_str()); return 1; }
        if (bird_rate > 0) simulate_storage_rate(bird2, bird_rate);
        engine.SetBird(2, bird2.samples.data(), (int)bird2.samples.size());
    }
    if (!mau_path.empty()) {
        if (!wav_read(mau_path, mau, err)) { printf("erro: %s\n", err.c_str()); return 1; }
        if (bird_rate > 0) simulate_storage_rate(mau, bird_rate);
        engine.SetBird(0, mau.samples.data(), (int)mau.samples.size());
    }

    Audio out;
    out.sample_rate = guitar.sample_rate;
    out.samples.resize(guitar.samples.size());
    const size_t trace_every = (size_t)(guitar.sample_rate / 10);  // 10 Hz
    if (trace) printf("\n%8s %12s %14s\n", "tempo", "detectado", "transposição");
    for (size_t i = 0; i < guitar.samples.size(); i++) {
        out.samples[i] = engine.Process(guitar.samples[i]);
        if (trace && i % trace_every == 0) {
            const float hz = engine.detected_hz();
            printf("%7.1fs %10.1fHz %+12.0fc\n", i / (float)guitar.sample_rate, hz,
                   engine.transposition_cents());
        }
    }
    if (trace) printf("\n");

    if (!wav_write(out_path, out, err)) { printf("erro: %s\n", err.c_str()); return 1; }

    printf("pássaro : %.2f s", bird.samples.size() / (float)bird.sample_rate);
    if (phrase) printf(" (frase %d)", phrase);
    if (bird_rate) printf(" [guardado a %d Hz]", bird_rate);
    printf("\nguitarra: %.2f s %s\n", guitar.samples.size() / (float)guitar.sample_rate,
           synth ? "[escala sintética]" : guitar_path.c_str());
    printf("ajustes : velocidade %.2f  mistura %.2f  grão %.0f ms  segue altura %s  oitavas +%.0f\n",
           p.speed, p.mix, p.grain_ms, p.pitch_follow ? "sim" : "nao", (float)p.octave_offset);
    printf("final   : detectou %.1f Hz  transposição %+.0f cents\n", engine.detected_hz(),
           engine.transposition_cents());
    printf("alvo    : %.0f Hz  (%+d oitavas sobre a nota)   piso %.0f  teto %.0f\n",
           engine.target_hz(), engine.octaves_now(), p.piso_hz, p.teto_hz);
    {
        static const char* kNomes[3] = {"Mau", "uirapuru", "bem-te-vi"};
        const int n = engine.onset_count();
        printf("pássaros: %d ataques   ", n);
        for (int i = 0; i < 3; i++) {
            if (!engine.has_bird(i)) continue;
            printf("%s %.0f%%   ", kNomes[i],
                   n ? 100.0 * engine.pick_count(i) / n : 0.0);
        }
        printf("\nvariedade: %.0f%%\n", p.variedade * 100.0f);
    }
    if (p.modo_disparo == BirdEngine::Params::FRASE) {
        static const char* kNomes[3] = {"Mau", "uirapuru", "bem-te-vi"};
        printf("frases  :");
        for (int i = 0; i < 3; i++)
            if (engine.has_bird(i)) printf(" %s %d", kNomes[i], engine.phrase_count(i));
        printf("   espaço %.0f ms   teto %.0f ms\n", p.espaco_ms, p.frase_max_ms);
        printf("chamada: %d disparadas, %d ataques ignorados por estar ocupado\n",
               engine.onset_count(), engine.ignored_count());
    }
    printf("gravou %s\n", out_path.c_str());
    return 0;
}
