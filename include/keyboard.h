#pragma once
// ============================================================
//  keyboard.h  -  Abstraccion del teclado Cardputer ADV
//
//  Mapa de teclas para PoisonMesh:
//
//  Navegacion (SOLO con Fn):
//    Fn + ;  ?  Up    (subir en lista)
//    Fn + .  ?  Down  (bajar en lista)
//    Fn + ,  ?  Left  (valor anterior)
//    Fn + /  ?  Right (valor siguiente)
//
//  Accion:
//    Enter          ?  Confirmar / toggle
//    Backspace      ?  Borrar caracter (en texto) / Volver
//    `  (backtick)  ?  Escape - Detener ataque / Salir
//    Fn + Backspace ?  Escape (alternativo)
//
//  Sin Fn, `;` `.` `,` `/` son caracteres imprimibles (escribibles en texto).
// ============================================================
#include <M5Unified.h>

enum class Key : uint8_t {
    None = 0,
    Char,        // caracter imprimible ? ver KeyEvent.ch
    Enter,
    Backspace,
    Escape,      // ` o Fn+Backspace ? detener ataque / salir
    Up,          // Fn+;
    Down,        // Fn+.
    Left,        // Fn+,
    Right,       // Fn+/
    Unknown,
};

struct KeyEvent {
    Key  type;
    char ch;     // valido solo si type == Key::Char
};

// ---- API ---------------------------------------------------
void     keyboard_init();
KeyEvent keyboard_read();           // no bloqueante
bool     keyboard_available();
KeyEvent keyboard_wait();           // bloqueante

// Editor de texto inline: llamar en cada iteracion del loop.
// Retorna 1=Enter, 0=sin accion, -1=Escape.
int keyboard_edit_string(char* buf, uint8_t maxLen, uint8_t* cursorLen);
