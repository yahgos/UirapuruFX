#pragma once
#include <string>
#include <vector>

// Leitura e escrita de WAV mono, o mínimo necessário pro renderizador offline.
// Lê PCM de 16/24/32 bits e float de 32, sempre rebaixando pra mono; escreve
// PCM de 16 bits.
struct Audio {
    std::vector<float> samples;  // mono, -1..1
    int sample_rate = 48000;
};

bool wav_read(const std::string& path, Audio& out, std::string& err);
bool wav_write(const std::string& path, const Audio& in, std::string& err);

// Simula guardar o sample numa taxa menor (como ficaria na flash do pedal) e
// tocar de volta na taxa do motor: dizima e depois interpola. Isso reproduz a
// perda de fidelidade pra você julgar de ouvido.
void simulate_storage_rate(Audio& audio, int stored_rate);
