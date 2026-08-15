#pragma once

#include <cstdint>

struct GreWord {
    const char* word;
    const char* pos;      // "adj." / "n." / "v." / "adv."
    const char* meaning;  // short gloss
    const char* example;  // one sentence
};

extern const GreWord GRE_WORDS[];
extern const uint16_t GRE_WORD_COUNT;
