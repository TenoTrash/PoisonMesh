#pragma once
// screen_toggle.h - Toggle de pantalla con boton G0 (Boot button)
// Presionar G0 alterna entre pantalla ON y OFF
#include <M5Unified.h>

#define SCREEN_TOGGLE_PIN 0   // G0 = Boot button del ESP32-S3

static bool _screen_on       = true;
static bool _last_btn_state  = true;   // HIGH = no presionado (pull-up)

inline void screen_toggle_init() {
    pinMode(SCREEN_TOGGLE_PIN, INPUT_PULLUP);
}

// Llamar en cada iteracion del loop principal
// Detecta flanco de bajada (presion del boton)
inline void screen_toggle_tick() {
    bool btn = digitalRead(SCREEN_TOGGLE_PIN);
    // Flanco bajada: boton presionado
    if (!btn && _last_btn_state) {
        _screen_on = !_screen_on;
        if (_screen_on) {
            M5.Display.wakeup();
            M5.Display.setBrightness(160);
        } else {
            M5.Display.setBrightness(0);
            M5.Display.sleep();
        }
    }
    _last_btn_state = btn;
}
