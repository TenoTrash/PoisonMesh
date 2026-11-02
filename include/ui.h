#pragma once
// ============================================================
//  ui.h  -  Capa de interfaz de usuario (M5GFX)
// ============================================================
#include <M5Unified.h>
#include "config.h"

// ---- Estado compartido de configuracion LoRa ---------------
struct LoRaConfig {
    uint8_t  regionIdx;   // indice en el array REGIONS[]
    uint8_t  presetIdx;   // indice en el array PRESETS[]
    uint16_t slot;        // 1..numSlots (0 = auto)
    float    freqMHz;     // frecuencia calculada
};

extern LoRaConfig g_loraCfg;

// ---- Funciones de pantalla ---------------------------------
void ui_init();
void ui_draw_header(const char* title);
void ui_draw_footer_freq(float freqMHz);
void ui_draw_status(const char* msg, uint16_t color = COL_TEXT);
void ui_clear_content();

// Pantalla de configuracion LoRa (Fase 1)
void screen_lora_setup();

// Pantalla de seleccion de ataque (menu principal)
uint8_t screen_main_menu();
