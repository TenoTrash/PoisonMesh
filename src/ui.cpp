// ============================================================
//  ui.cpp  -  Interfaz de usuario PoisonMesh-Cardputer
//  Setup LoRa  +  Menu Principal de ataques
// ============================================================
#include "ui.h"
#include "keyboard.h"
#include "meshtastic_freq.h"
#include "nodedb.h"
#include "radio.h"
#include <M5Unified.h>
#include "screensaver.h"

#include "nodedb.h"
#include "radio.h"
#include <M5Unified.h>
#include "screensaver.h"
#include <M5GFX.h>

// ---- Estado global -----------------------------------------
LoRaConfig g_loraCfg = {
    .regionIdx = DEFAULT_REGION_IDX,
    .presetIdx = DEFAULT_PRESET_IDX,
    .slot      = 20,   // default US LongFast = slot 20
    .freqMHz   = 906.875f
};

// Canvas de doble buffer para evitar flickering
static M5Canvas canvas(&M5.Display);

// ============================================================
//  HELPERS DE DIBUJO
// ============================================================

void ui_init() {
    M5.Display.setRotation(1);        // landscape
    M5.Display.setBrightness(160);
    M5.Display.fillScreen(COL_BG);

    canvas.createSprite(LCD_WIDTH, LCD_HEIGHT);
    canvas.setTextDatum(MC_DATUM);
    canvas.setFont(&fonts::FreeSans9pt7b);
}

void ui_draw_header(const char* title) {
    canvas.fillRect(0, 0, LCD_WIDTH, UI_HEADER_H, COL_HEADER_BG);
    // Usar fuente pequena built-in (6x8px) para que todo entre en 18px
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(1);
    // "DM" (izquierda, acento) + separador + titulo (centro) en una sola linea
    canvas.setTextColor(COL_ACCENT);
    canvas.setTextDatum(ML_DATUM);
    canvas.drawString("PM|", 3, UI_HEADER_H / 2);
    canvas.setTextColor(COL_TEXT);
    canvas.setTextDatum(MC_DATUM);
    canvas.drawString(title, LCD_WIDTH / 2, UI_HEADER_H / 2);
    // Version abreviada (derecha, dim)
    canvas.setTextColor(COL_TEXT_DIM);
    canvas.setTextDatum(MR_DATUM);
    int bat_pct = M5.Power.getBatteryLevel();
    char bat_str[8]; snprintf(bat_str, sizeof(bat_str), "%d%%", bat_pct);
    uint16_t bat_col = bat_pct > 50 ? COL_OK : bat_pct > 20 ? COL_WARNING : COL_DANGER;
    canvas.setTextColor(bat_col);
    canvas.drawString(bat_str, LCD_WIDTH - 2, UI_HEADER_H / 2);
    // Linea separadora
    canvas.drawFastHLine(0, UI_HEADER_H - 1, LCD_WIDTH, COL_ACCENT);
}

void ui_draw_footer_freq(float freqMHz) {
    int y = LCD_HEIGHT - UI_FOOTER_H;
    canvas.fillRect(0, y, LCD_WIDTH, UI_FOOTER_H, COL_HEADER_BG);
    canvas.setTextColor(COL_OK);
    canvas.setTextDatum(ML_DATUM);
    canvas.setTextSize(1);

    char buf[48];
    if (freqMHz > 0.0f) {
        snprintf(buf, sizeof(buf), "  Freq: %.3f MHz", freqMHz);
    } else {
        snprintf(buf, sizeof(buf), "  Freq: -- Seleccionar region --");
        canvas.setTextColor(COL_WARNING);
    }
    canvas.drawString(buf, 0, y + UI_FOOTER_H / 2);
}

void ui_draw_status(const char* msg, uint16_t color) {
    int y = LCD_HEIGHT - UI_FOOTER_H;
    canvas.fillRect(0, y, LCD_WIDTH, UI_FOOTER_H, COL_HEADER_BG);
    canvas.setTextColor(color);
    canvas.setTextDatum(MC_DATUM);
    canvas.drawString(msg, LCD_WIDTH / 2, y + UI_FOOTER_H / 2);
}

void ui_clear_content() {
    canvas.fillRect(0, UI_CONTENT_Y, LCD_WIDTH, UI_CONTENT_H, COL_BG);
}

static void _push() {
    canvas.pushSprite(0, 0);
}

// ============================================================
//  PANTALLA 1 - SETUP LORA
// ============================================================
//
//  Layout:
//   [Header] "Setup LoRa"
//   ????????????????????????????????
//   ?  Region:  [? US ?]           ?
//   ?  Preset:  [? LongFast ?]     ?
//   ?  Slot:    [? 20 (1-104) ?]   ?
//   ?  (HW: ? Compatible)          ?
//   ????????????????????????????????
//   [Footer]  Freq: 906.875 MHz
//
//  Controles:
//   Fn+Up/Down  = cambiar parametro seleccionado
//   Fn+Left(,) / Fn+Right(/) = cambiar valor
//   Enter    = confirmar e ir al menu principal
// ============================================================

static const char* SETUP_LABELS[] = { "Region", "Preset", "Slot" };
static const uint8_t SETUP_ITEMS  = 3;

static void _draw_setup(uint8_t selected) {
    canvas.fillSprite(COL_BG);
    ui_draw_header("Setup LoRa");

    uint16_t numSlots = calc_num_slots(g_loraCfg.regionIdx, g_loraCfg.presetIdx);
    bool compatible   = region_hw_compatible(g_loraCfg.regionIdx);
    g_loraCfg.freqMHz = calc_freq_mhz(g_loraCfg.regionIdx, g_loraCfg.presetIdx, g_loraCfg.slot);

    int rowH = (UI_CONTENT_H - 4) / (SETUP_ITEMS + 1);
    int startY = UI_CONTENT_Y + 4;

    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextSize(1);

    for (uint8_t i = 0; i < SETUP_ITEMS; i++) {
        int y = startY + i * rowH;
        bool sel = (i == selected);

        // Fondo de fila seleccionada
        if (sel) {
            canvas.fillRect(0, y, LCD_WIDTH, rowH - 2, COL_SEL_BG);
        }

        // Etiqueta
        canvas.setTextColor(sel ? COL_ACCENT : COL_TEXT_DIM);
        canvas.setTextDatum(ML_DATUM);
        canvas.drawString(SETUP_LABELS[i], 6, y + rowH / 2);

        // Valor
        char val[32] = "";
        if (i == 0) snprintf(val, sizeof(val), "< %s >", REGIONS[g_loraCfg.regionIdx].name);
        else if (i == 1) snprintf(val, sizeof(val), "< %s >", PRESETS[g_loraCfg.presetIdx].name);
        else            snprintf(val, sizeof(val), "< %u / %u >", g_loraCfg.slot, numSlots);

        canvas.setTextColor(sel ? COL_TEXT : COL_TEXT);
        canvas.setTextDatum(MR_DATUM);
        canvas.drawString(val, LCD_WIDTH - 6, y + rowH / 2);
    }

    // Fila de compatibilidad de hardware
    int y = startY + SETUP_ITEMS * rowH;
    if (!compatible) {
        canvas.setTextColor(COL_DANGER);
        canvas.setTextDatum(ML_DATUM);
        canvas.drawString("! HW: 433MHz fuera rango SX1262", 6, y + rowH / 2);
    } else {
        canvas.setTextColor(COL_OK);
        canvas.setTextDatum(ML_DATUM);
        canvas.drawString(" Para M5 Cap LoRa SX1262", 6, y + rowH / 2);
    }

    // Instrucciones rapidas (en footer, en lugar de la frecuencia mientras edita)
    if (selected < SETUP_ITEMS) {
        ui_draw_footer_freq(g_loraCfg.freqMHz);
    }

    _push();
}

void screen_lora_setup() {
    uint8_t selected = 0;

    while (true) {
        _draw_setup(selected);
        delay(20);

        KeyEvent ev = keyboard_read();
        if (ev.type == Key::None) continue;

        uint16_t numSlots = calc_num_slots(g_loraCfg.regionIdx, g_loraCfg.presetIdx);

        if (ev.type == Key::Up) {
            if (selected > 0) selected--;
        }
        else if (ev.type == Key::Down) {
            if (selected < SETUP_ITEMS - 1) selected++;
        }
        else if (ev.type == Key::Left) {
            // Left = valor anterior
            if (selected == 0) {
                g_loraCfg.regionIdx = (g_loraCfg.regionIdx == 0)
                    ? NUM_REGIONS - 1 : g_loraCfg.regionIdx - 1;
                g_loraCfg.slot = 1;
            } else if (selected == 1) {
                g_loraCfg.presetIdx = (g_loraCfg.presetIdx == 0)
                    ? NUM_PRESETS - 1 : g_loraCfg.presetIdx - 1;
                g_loraCfg.slot = 1;
            } else {
                if (g_loraCfg.slot > 1) g_loraCfg.slot--;
            }
        }
        else if (ev.type == Key::Right) {
            // Right = valor siguiente
            if (selected == 0) {
                g_loraCfg.regionIdx = (g_loraCfg.regionIdx + 1) % NUM_REGIONS;
                g_loraCfg.slot = 1;
            } else if (selected == 1) {
                g_loraCfg.presetIdx = (g_loraCfg.presetIdx + 1) % NUM_PRESETS;
                g_loraCfg.slot = 1;
            } else {
                numSlots = calc_num_slots(g_loraCfg.regionIdx, g_loraCfg.presetIdx);
                if (g_loraCfg.slot < numSlots) g_loraCfg.slot++;
            }
        }
        else if (ev.type == Key::Enter) {
            // Confirmar y avanzar
            g_loraCfg.freqMHz = calc_freq_mhz(
                g_loraCfg.regionIdx, g_loraCfg.presetIdx, g_loraCfg.slot);
            break;
        }
    }
}

// ============================================================
//  PANTALLA 2 - MENU PRINCIPAL DE ATAQUES
// ============================================================

static const char* ATTACK_NAMES[] = {
    "1. Flood Nodes",
    "2. MeshGrow",
    "3. Namechange",
    "4. Position Poison",
    "5. Mesh Move",
    "6. Mesh Impersonator",
    "7. PKI Poison",
    "8. ChattyMesh",
    "9. GhostMesh",
    "10. NullMeshGrow",
    "11. Setup LoRa",
};
static const uint8_t NUM_ATTACKS = sizeof(ATTACK_NAMES) / sizeof(ATTACK_NAMES[0]);

static void _draw_menu(uint8_t selected, uint8_t scrollOffset) {
    canvas.fillSprite(COL_BG);

    // Header con contador de nodos en vivo
    canvas.fillRect(0, 0, LCD_WIDTH, UI_HEADER_H, COL_HEADER_BG);
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(1.2);
    canvas.setTextColor(COL_ACCENT);
    canvas.setTextDatum(ML_DATUM);
    canvas.drawString("PM|", 3, UI_HEADER_H / 2);
    canvas.setTextColor(COL_TEXT);
    canvas.setTextDatum(MC_DATUM);
    canvas.drawString("Menu Principal", LCD_WIDTH / 2, UI_HEADER_H / 2);
    canvas.setTextSize(1);
    // Nodos en rango (derecha del header)
    char node_buf[16];
    if (g_nodedb.count > 0) {
        snprintf(node_buf, sizeof(node_buf), "%u nodos", g_nodedb.count);
        canvas.setTextColor(COL_OK);
    } else {
        snprintf(node_buf, sizeof(node_buf), "escuchando");
        canvas.setTextColor(COL_TEXT_DIM);
    }
    canvas.setTextDatum(MR_DATUM);
    canvas.drawString(node_buf, LCD_WIDTH - 2, UI_HEADER_H / 2);
    canvas.drawFastHLine(0, UI_HEADER_H - 1, LCD_WIDTH, COL_ACCENT);

    int rowH = 18;
    uint8_t visibleRows = UI_CONTENT_H / rowH;
    int startY = UI_CONTENT_Y + 2;

    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextSize(1);

    for (uint8_t i = 0; i < visibleRows; i++) {
        uint8_t idx = scrollOffset + i;
        if (idx >= NUM_ATTACKS) break;

        int y = startY + i * rowH;
        bool sel = (idx == selected);

        if (sel) {
            canvas.fillRect(0, y, LCD_WIDTH, rowH - 1, COL_SEL_BG);
            canvas.setTextColor(COL_ACCENT);
        } else {
            canvas.setTextColor(idx == NUM_ATTACKS - 1 ? COL_TEXT_DIM : COL_TEXT);
        }

        canvas.setTextDatum(ML_DATUM);
        // Flecha indicadora
        if (sel) canvas.drawString("> ", 2, y + rowH / 2);
        canvas.drawString(ATTACK_NAMES[idx], 16, y + rowH / 2);


    }

    // Indicador de scroll
    if (NUM_ATTACKS > visibleRows) {
        if (scrollOffset > 0) {
            canvas.setTextColor(COL_ACCENT);
            canvas.setTextDatum(MR_DATUM);
            canvas.drawString("+", LCD_WIDTH - 2, startY + rowH / 2);
        }
        if (scrollOffset + visibleRows < NUM_ATTACKS) {
            canvas.setTextColor(COL_ACCENT);
            canvas.setTextDatum(MR_DATUM);
            canvas.drawString("+", LCD_WIDTH - 2,
                startY + (visibleRows - 1) * rowH + rowH / 2);
        }
    }

    // Footer: frecuencia + indicador sniffer activo
    int fy = LCD_HEIGHT - UI_FOOTER_H;
    canvas.fillRect(0, fy, LCD_WIDTH, UI_FOOTER_H, COL_HEADER_BG);
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(1);
    canvas.setTextColor(COL_OK);
    canvas.setTextDatum(ML_DATUM);
    char footer_buf[48];
    snprintf(footer_buf, sizeof(footer_buf),
        " %.3f MHz  [RX]", g_loraCfg.freqMHz);
    canvas.drawString(footer_buf, 0, fy + UI_FOOTER_H / 2);
    _push();
}

// Retorna el indice seleccionado (0-6 = ataques, 7 = volver a setup)
uint8_t screen_main_menu() {
    uint8_t  selected    = 0;
    uint8_t  scrollOff   = 0;
    uint8_t  visibleRows = UI_CONTENT_H / 18;
    uint32_t last_draw   = 0;
    uint8_t  last_count  = 255;  // forzar primer dibujo

    while (true) {
        // Redibujar solo si cambia el conteo o cada 500ms
        bool count_changed = (g_nodedb.count != last_count);
        bool time_elapsed  = (millis() - last_draw > 500);

        if (count_changed || time_elapsed) {
            _draw_menu(selected, scrollOff);
            last_draw  = millis();
            last_count = g_nodedb.count;
        }

        // Procesar paquetes radio en background
        if (radio_packet_available()) {
            RawPacket pkt;
            if (radio_read_packet(pkt) == RadioResult::OK)
                g_nodedb.process_packet(pkt);
        }

        screen_toggle_tick();
        delay(20);

        KeyEvent ev = keyboard_read();
        if (ev.type == Key::None) continue;

        
        // Redibujar inmediatamente al navegar
        last_count = 255;

        if (ev.type == Key::Up) {
            if (selected > 0) selected--;
            if (selected < scrollOff) scrollOff = selected;
        }
        else if (ev.type == Key::Down) {
            if (selected < NUM_ATTACKS - 1) selected++;
            if (selected >= scrollOff + visibleRows)
                scrollOff = selected - visibleRows + 1;
        }
        else if (ev.type == Key::Enter) {
            return selected;
        }
    }
}
