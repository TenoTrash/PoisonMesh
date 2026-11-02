#pragma once
// ============================================================
//  meshtastic_freq.h  -  Regiones, presets y calculo de freq.
//  Fuente: https://meshtastic.org/docs/overview/radio-settings/
// ============================================================
#include <stdint.h>

// ---- Preset (modem) ----------------------------------------
struct MeshPreset {
    const char* name;
    float       bwKHz;    // ancho de banda en kHz
    uint8_t     sf;       // spreading factor
    uint8_t     cr;       // coding rate (5=4/5, 6=4/6, 7=4/7, 8=4/8)
};

// Presets oficiales Meshtastic (orden = Short_Turbo ? Very_Long_Slow)
static const MeshPreset PRESETS[] = {
    // nombre          BW      SF   CR
    { "ShortTurbo",   500.0f, 7,   5 },
    { "ShortFast",    250.0f, 7,   5 },
    { "ShortSlow",    250.0f, 8,   6 },
    { "MediumFast",   250.0f, 9,   7 },
    { "MediumSlow",   250.0f, 10,  7 },
    { "LongFast",     250.0f, 11,  8 },   // ? DEFAULT (indice 5)
    { "LongModerate", 125.0f, 11,  8 },
    { "LongSlow",     125.0f, 12,  8 },
    { "VeryLongSlow",  62.5f, 12,  8 },
};
static const uint8_t NUM_PRESETS = sizeof(PRESETS) / sizeof(PRESETS[0]);
static const uint8_t DEFAULT_PRESET_IDX = 5;  // LongFast

// ---- Region LoRa -------------------------------------------
struct MeshRegion {
    const char* name;
    float       freqStart; // MHz (inicio del band)
    float       freqEnd;   // MHz (fin del band)
    uint8_t     maxPower;  // dBm maximo
};

// Regiones soportadas por el SX1262 (850-960 MHz)
// CN (470 MHz) queda fuera del hardware; se incluye con advertencia.
static const MeshRegion REGIONS[] = {
    // nombre     inicio     fin      pwr
    { "US",       902.0f,  928.0f,   30 },
    { "EU_868",   869.4f,  869.65f,  16 },
    { "EU_433",   433.0f,  434.0f,   12 },  // fuera de rango SX1262 (advertencia)
    { "ANZ",      915.0f,  928.0f,   30 },
    { "RU",       868.7f,  869.2f,   20 },
    { "KR",       920.0f,  923.0f,   23 },
    { "TW",       920.0f,  925.0f,   30 },
    { "IN",       865.0f,  867.0f,   30 },
    { "NZ_865",   864.0f,  868.0f,   36 },
    { "TH",       920.0f,  925.0f,   16 },
    { "UA_868",   868.0f,  868.6f,   14 },
    { "UA_433",   433.0f,  434.0f,   12 },  // fuera de rango SX1262 (advertencia)
    { "MY_919",   919.0f,  924.0f,   20 },
    { "MY_433",   433.0f,  434.0f,   12 },  // fuera de rango SX1262 (advertencia)
    { "SG_923",   917.0f,  925.0f,   16 },
    { "JP",       920.8f,  927.8f,   16 },
};
static const uint8_t NUM_REGIONS = sizeof(REGIONS) / sizeof(REGIONS[0]);
static const uint8_t DEFAULT_REGION_IDX = 0; // US

// ---- Regiones fuera de rango del SX1262 (solo aviso) -------
// El SX1262 opera entre 850-960 MHz.
// Las regiones 433 MHz (EU_433, UA_433, MY_433) no son compatibles
// con el hardware Cap LoRa-1262 del M5Stack.

// ============================================================
//  calc_freq_mhz()
//
//  Calcula la frecuencia central para un slot dado.
//  Formula: freqStart + (bwKHz/1000) * (slot - 0.5)
//  donde slot ? [1, numSlots]
//
//  Si slot == 0 ? usa el slot por defecto de la region (slot 20 en US/LongFast)
// ============================================================
inline uint16_t calc_num_slots(uint8_t regionIdx, uint8_t presetIdx) {
    if (regionIdx >= NUM_REGIONS || presetIdx >= NUM_PRESETS) return 1;
    const MeshRegion& r = REGIONS[regionIdx];
    const MeshPreset& p = PRESETS[presetIdx];
    float bandMHz = r.freqEnd - r.freqStart;
    float bwMHz   = p.bwKHz / 1000.0f;
    if (bwMHz <= 0.0f) return 1;
    return (uint16_t)(bandMHz / bwMHz);
}

inline float calc_freq_mhz(uint8_t regionIdx, uint8_t presetIdx, uint16_t slot) {
    if (regionIdx >= NUM_REGIONS || presetIdx >= NUM_PRESETS) return 0.0f;
    const MeshRegion& r = REGIONS[regionIdx];
    const MeshPreset& p = PRESETS[presetIdx];
    uint16_t numSlots = calc_num_slots(regionIdx, presetIdx);
    if (numSlots < 1) numSlots = 1;
    // slot 0 ? default (slot 1 para la mayoria; excepto US LongFast donde es 20)
    if (slot == 0 || slot > numSlots) slot = 1;
    float bwMHz = p.bwKHz / 1000.0f;
    return r.freqStart + bwMHz * ((float)slot - 0.5f);
}

// Retorna true si la region es compatible con el SX1262 (850-960 MHz)
inline bool region_hw_compatible(uint8_t regionIdx) {
    if (regionIdx >= NUM_REGIONS) return false;
    const MeshRegion& r = REGIONS[regionIdx];
    return (r.freqStart >= 850.0f && r.freqEnd <= 960.0f);
}
