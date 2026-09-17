#include "wav.h"
#include <cstdio>
#include <cstring>
#include <cmath>

namespace {
struct Reader {
    FILE* f;
    bool u32(uint32_t& v) { return fread(&v, 4, 1, f) == 1; }
    bool u16(uint16_t& v) { return fread(&v, 2, 1, f) == 1; }
};
}  // namespace

bool wav_read(const std::string& path, Audio& out, std::string& err) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { err = "cannot open " + path; return false; }
    Reader r{f};

    char riff[4], wave[4];
    uint32_t sz;
    if (fread(riff, 1, 4, f) != 4 || !r.u32(sz) || fread(wave, 1, 4, f) != 4 ||
        memcmp(riff, "RIFF", 4) || memcmp(wave, "WAVE", 4)) {
        fclose(f); err = "not a RIFF/WAVE file"; return false;
    }

    uint16_t fmt = 0, channels = 1, bits = 16;
    uint32_t rate = 48000;
    bool have_fmt = false;

    while (true) {
        char id[4];
        uint32_t chunk_sz;
        if (fread(id, 1, 4, f) != 4 || !r.u32(chunk_sz)) break;

        if (!memcmp(id, "fmt ", 4)) {
            uint16_t block, dummy16;
            uint32_t byterate;
            r.u16(fmt); r.u16(channels); r.u32(rate);
            r.u32(byterate); r.u16(block); r.u16(bits);
            if (chunk_sz > 16) fseek(f, chunk_sz - 16, SEEK_CUR);
            (void)dummy16;
            have_fmt = true;
        } else if (!memcmp(id, "data", 4)) {
            if (!have_fmt) { fclose(f); err = "data before fmt"; return false; }
            const int bytes = bits / 8;
            const size_t frames = chunk_sz / (bytes * channels);
            out.samples.resize(frames);
            out.sample_rate = (int)rate;

            std::vector<unsigned char> raw(chunk_sz);
            if (fread(raw.data(), 1, chunk_sz, f) != chunk_sz) {
                fclose(f); err = "short data chunk"; return false;
            }
            for (size_t i = 0; i < frames; i++) {
                double acc = 0.0;
                for (int c = 0; c < channels; c++) {
                    const unsigned char* p = &raw[(i * channels + c) * bytes];
                    double v = 0.0;
                    if (fmt == 3 && bits == 32) {
                        float fv; memcpy(&fv, p, 4); v = fv;
                    } else if (bits == 16) {
                        int16_t s; memcpy(&s, p, 2); v = s / 32768.0;
                    } else if (bits == 24) {
                        int32_t s = (p[0] << 8) | (p[1] << 16) | (p[2] << 24);
                        v = (s >> 8) / 8388608.0;
                    } else if (bits == 32) {
                        int32_t s; memcpy(&s, p, 4); v = s / 2147483648.0;
                    } else if (bits == 8) {
                        v = (p[0] - 128) / 128.0;
                    }
                    acc += v;
                }
                out.samples[i] = (float)(acc / channels);
            }
            fclose(f);
            return true;
        } else {
            fseek(f, (chunk_sz + 1) & ~1u, SEEK_CUR);
        }
    }
    fclose(f);
    err = "no data chunk found";
    return false;
}

bool wav_write(const std::string& path, const Audio& in, std::string& err) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { err = "cannot create " + path; return false; }
    const uint32_t data_bytes = (uint32_t)(in.samples.size() * 2);
    const uint32_t riff_sz = 36 + data_bytes;
    const uint16_t channels = 1, bits = 16, block = 2, fmt = 1;
    const uint32_t rate = (uint32_t)in.sample_rate, byterate = rate * block;
    const uint32_t fmt_sz = 16;

    fwrite("RIFF", 1, 4, f); fwrite(&riff_sz, 4, 1, f); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); fwrite(&fmt_sz, 4, 1, f);
    fwrite(&fmt, 2, 1, f); fwrite(&channels, 2, 1, f); fwrite(&rate, 4, 1, f);
    fwrite(&byterate, 4, 1, f); fwrite(&block, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data_bytes, 4, 1, f);

    for (float s : in.samples) {
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        int16_t v = (int16_t)lrintf(s * 32767.0f);
        fwrite(&v, 2, 1, f);
    }
    fclose(f);
    return true;
}

void simulate_storage_rate(Audio& audio, int stored_rate) {
    if (stored_rate <= 0 || stored_rate >= audio.sample_rate) return;
    const double ratio = (double)audio.sample_rate / stored_rate;

    // Anti-alias antes de dizimar: um polo simples mais ou menos no novo Nyquist.
    const double fc = stored_rate * 0.45;
    const double a = 1.0 - exp(-2.0 * M_PI * fc / audio.sample_rate);
    double lp = 0.0;
    std::vector<float> filtered(audio.samples.size());
    for (size_t i = 0; i < audio.samples.size(); i++) {
        lp += a * (audio.samples[i] - lp);
        filtered[i] = (float)lp;
    }

    // Dizima pra taxa de armazenamento...
    const size_t stored_len = (size_t)(audio.samples.size() / ratio);
    std::vector<float> stored(stored_len);
    for (size_t i = 0; i < stored_len; i++) stored[i] = filtered[(size_t)(i * ratio)];

    // ...e depois interpola de volta pra cima, como o pedal faria ao carregar.
    for (size_t i = 0; i < audio.samples.size(); i++) {
        const double pos = i / ratio;
        const size_t i0 = (size_t)pos;
        const size_t i1 = i0 + 1 < stored_len ? i0 + 1 : i0;
        const double frac = pos - i0;
        audio.samples[i] = i0 < stored_len
            ? (float)(stored[i0] * (1.0 - frac) + stored[i1] * frac) : 0.0f;
    }
}
