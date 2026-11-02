// ============================================================
//  keyboard.cpp  -  Implementacion del teclado Cardputer ADV
//
//  Basado en el trabajo de Teno (2026):
//  https://github.com/TenoTrash/M5-CardputerADV-USB-Keyboard
//
//  Usa HID keycodes raw del hardware para maxima fiabilidad.
//  Los keycodes fisicos del teclado del Cardputer ADV son:
//
//  Capa normal:
//    `;`  ? HID 0x33    `.`  ? HID 0x37
//    `,`  ? HID 0x36    `/`  ? HID 0x38
//
//  Capa FN (Fn presionado):
//    Fn + 0x33 (;) ? flecha ARRIBA   (0x52)
//    Fn + 0x37 (.) ? flecha ABAJO    (0x51)
//    Fn + 0x36 (,) ? flecha IZQ      (0x50)
//    Fn + 0x38 (/) ? flecha DER      (0x4F)
//    Fn + 0x1E..0x2E ? F1..F12
//    Fn + 0x2C (Space) ? Insert
//    ` (0x35) ? ESC
//
//  Mapa para PoisonMesh:
//    Navegacion lista  ? Fn+; (arriba) y Fn+. (abajo)
//    Cambio de valor   ? Fn+, (izq)   y Fn+/ (der)
//    Confirmar         ? Enter
//    Borrar/Volver     ? Backspace
//    Escape/Stop       ? ` (backtick)  o  Fn + Backspace
//
//  Sin Fn, ; . , / son caracteres imprimibles (escribibles en texto).
// ============================================================
#include "keyboard.h"
#include <M5Cardputer.h>

void keyboard_init() {
    // Inicializado en setup() via M5Cardputer.begin()
}

// ---- Traduccion de raw key ? KeyEvent ----------------------
static KeyEvent _parse_key() {
    KeyEvent ev = { Key::None, 0 };

    auto& kb = M5Cardputer.Keyboard;
    if (!kb.isChange()) return ev;
    if (!kb.isPressed()) return ev;

    auto ks = kb.keysState();

    // --- Enter ---
    if (ks.enter) {
        ev.type = Key::Enter;
        return ev;
    }

    // --- Backspace (tecla del) + Escape (Fn+del / backtick) ---
    //     El driver expone el borrar como ks.backspace. Fn+Backspace
    //     se reporta como ks.del (KEY_DELETE).
    if (ks.del) {
        ev.type = Key::Escape;
        return ev;
    }
    if (ks.backspace) {
        ev.type = Key::Backspace;
        return ev;
    }

    // --- Flechas de direccion ---
    //     Solo con Fn (el driver usa los campos nativos up/down/left/right).
    //     Sin Fn, ; . , / son caracteres imprimibles (escribibles en texto).
    if (ks.up)    { ev.type = Key::Up;    return ev; }
    if (ks.down)  { ev.type = Key::Down;  return ev; }
    if (ks.left)  { ev.type = Key::Left;  return ev; }
    if (ks.right) { ev.type = Key::Right; return ev; }

    // --- Escape (Fn + backtick) ---
    if (ks.esc) { ev.type = Key::Escape; return ev; }

    // --- Backtick como Escape (alternativo, sin Fn) ---
    if (!ks.word.empty() && ks.word[0] == '`') {
        ev.type = Key::Escape;
        return ev;
    }

    // --- Caracter imprimible ---
    //     ks.word ya viene decodificado por M5Cardputer (SHIFT/caps aplicados).
    //     ; . , / y el resto ahora llegan como caracteres normales.
    if (!ks.word.empty()) {
        char c = ks.word[0];
        if (c >= 32 && c < 127) {
            ev.type = Key::Char;
            ev.ch   = c;
            return ev;
        }
    }

    ev.type = Key::Unknown;
    return ev;
}

// ---- API publica -------------------------------------------

bool keyboard_available() {
    M5Cardputer.update();
    return M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed();
}

KeyEvent keyboard_read() {
    M5Cardputer.update();
    return _parse_key();
}

KeyEvent keyboard_wait() {
    KeyEvent ev = { Key::None, 0 };
    while (ev.type == Key::None) {
        M5Cardputer.update();
        ev = _parse_key();
        delay(10);
    }
    return ev;
}

// ---- Editor de strings in-place ----------------------------
// Llama en cada iteracion del loop.
// Retorna:
//   1  -> confirmado (Enter)
//   0  -> sin accion / caracter insertado / borrado
//   -1 -> cancelado (Escape / backtick)
int keyboard_edit_string(char* buf, uint8_t maxLen, uint8_t* cursorLen) {
    M5Cardputer.update();
    KeyEvent ev = _parse_key();

    if (ev.type == Key::None)       return 0;
    if (ev.type == Key::Enter)      return 1;
    if (ev.type == Key::Escape)     return -1;

    if (ev.type == Key::Backspace) {
        if (*cursorLen > 0) {
            (*cursorLen)--;
            buf[*cursorLen] = '\0';
        }
        return 0;
    }

    if (ev.type == Key::Char) {
        if (*cursorLen < maxLen - 1) {
            buf[(*cursorLen)++] = ev.ch;
            buf[*cursorLen]     = '\0';
        }
        return 0;
    }

    return 0;
}
