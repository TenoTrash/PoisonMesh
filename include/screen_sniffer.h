#pragma once
// ============================================================
//  screen_sniffer.h  -  Pantalla de sniffer pasivo
//  Muestra nodos descubiertos en tiempo real
// ============================================================
#include "nodedb.h"
#include "radio.h"
#include "keyboard.h"
#include "ui.h"
#include <M5Unified.h>

// Dibuja la lista de nodos en pantalla
// Retorna el indice del nodo seleccionado, o -1 si ESC
inline int screen_node_list(const char* title,
                             bool selectable = false) {
    uint8_t selected   = 0;
    uint8_t scrollOff  = 0;
    const uint8_t ROW_H     = 20;
    const uint8_t VISIBLE   = (uint8_t)(UI_CONTENT_H / ROW_H);

    static M5Canvas canvas(&M5.Display);
    canvas.createSprite(LCD_WIDTH, LCD_HEIGHT);

    auto redraw = [&]() {
        canvas.fillSprite(COL_BG);

        // Header
        canvas.fillRect(0, 0, LCD_WIDTH, UI_HEADER_H, COL_HEADER_BG);
        canvas.setFont(&fonts::Font0);
        canvas.setTextSize(1);
        canvas.setTextColor(COL_ACCENT);
        canvas.setTextDatum(ML_DATUM);
        canvas.drawString("DM|", 3, UI_HEADER_H / 2);
        canvas.setTextColor(COL_TEXT);
        canvas.setTextDatum(MC_DATUM);
        canvas.drawString(title, LCD_WIDTH / 2, UI_HEADER_H / 2);
        char cnt_buf[12];
        snprintf(cnt_buf, sizeof(cnt_buf), "%u nodos", g_nodedb.count);
        canvas.setTextColor(COL_TEXT_DIM);
        canvas.setTextDatum(MR_DATUM);
        canvas.drawString(cnt_buf, LCD_WIDTH - 2, UI_HEADER_H / 2);
        canvas.drawFastHLine(0, UI_HEADER_H - 1, LCD_WIDTH, COL_ACCENT);

        // Lista de nodos
        canvas.setFont(&fonts::FreeSans9pt7b);
        canvas.setTextSize(1);

        if (g_nodedb.count == 0) {
            canvas.setTextColor(COL_TEXT_DIM);
            canvas.setTextDatum(MC_DATUM);
            canvas.drawString("Escuchando...", LCD_WIDTH / 2,
                UI_CONTENT_Y + UI_CONTENT_H / 2);
        } else {
            for (uint8_t i = 0; i < VISIBLE; i++) {
                uint8_t idx = scrollOff + i;
                if (idx >= g_nodedb.count) break;
                const DiscoveredNode& n = g_nodedb.nodes[idx];

                int y = UI_CONTENT_Y + 2 + i * ROW_H;
                bool sel = selectable && (idx == selected);

                if (sel) canvas.fillRect(0, y, LCD_WIDTH, ROW_H - 1, COL_SEL_BG);

                // Flecha + nombre
                canvas.setTextColor(sel ? COL_ACCENT : COL_TEXT);
                canvas.setTextDatum(ML_DATUM);
                char row[48];
                snprintf(row, sizeof(row), "%s%s",
                    sel ? "> " : "  ", n.long_name);
                canvas.drawString(row, 2, y + ROW_H / 2);

                // RSSI a la derecha
                canvas.setTextColor(
                    n.rssi > -80 ? COL_OK :
                    n.rssi > -100 ? COL_WARNING : COL_DANGER);
                canvas.setTextDatum(MR_DATUM);
                char rssi_buf[8];
                snprintf(rssi_buf, sizeof(rssi_buf), "%d", n.rssi);
                canvas.drawString(rssi_buf, LCD_WIDTH - 2, y + ROW_H / 2);
            }
        }

        // Footer: frecuencia + instrucciones
        int fy = LCD_HEIGHT - UI_FOOTER_H;
        canvas.fillRect(0, fy, LCD_WIDTH, UI_FOOTER_H, COL_HEADER_BG);
        canvas.setFont(&fonts::Font0);
        canvas.setTextColor(COL_TEXT_DIM);
        canvas.setTextDatum(ML_DATUM);
        char footer[48];
        if (selectable)
            snprintf(footer, sizeof(footer), " Fn+;.=nav  Enter=sel  `=salir");
        else
            snprintf(footer, sizeof(footer), " %.3f MHz  `=salir",
                radio_get_freq_mhz());
        canvas.drawString(footer, 0, fy + UI_FOOTER_H / 2);

        canvas.pushSprite(0, 0);
    };

    while (true) {
        // Procesar paquetes recibidos
        if (radio_packet_available()) {
            RawPacket pkt;
            if (radio_read_packet(pkt) == RadioResult::OK) {
                bool updated = g_nodedb.process_packet(pkt);
                if (updated) {
                    Serial.printf("[Sniffer] Nodo actualizado. Total: %u\n",
                        g_nodedb.count);
                }
            }
        }

        redraw();

        KeyEvent ev = keyboard_read();
        if (ev.type == Key::None) { delay(50); continue; }

        if (ev.type == Key::Escape) return -1;

        if (selectable) {
            if (ev.type == Key::Up && selected > 0) {
                selected--;
                if (selected < scrollOff) scrollOff = selected;
            }
            else if (ev.type == Key::Down &&
                     selected < g_nodedb.count - 1) {
                selected++;
                if (selected >= scrollOff + VISIBLE)
                    scrollOff = selected - VISIBLE + 1;
            }
            else if (ev.type == Key::Enter && g_nodedb.count > 0) {
                return (int)selected;
            }
        }
    }
}
