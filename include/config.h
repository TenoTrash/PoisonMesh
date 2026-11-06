#pragma once
// ============================================================
//  config.h  -  Constantes globales de PoisonMesh-Cardputer
// ============================================================

// ---- Pines SPI LoRa (Cap LoRa-1262) ------------------------
#define LORA_SCK   40
#define LORA_MOSI  14
#define LORA_MISO  39
#define LORA_NSS    5
#define LORA_RST    3
#define LORA_IRQ    4
#define LORA_BUSY   6

// ---- Pines GPS (UART) --------------------------------------
#define GPS_RX 13
#define GPS_TX 15

// ---- Pines I2C (PI4IOE5V6408 + HY2.0) ---------------------
#define I2C_SDA  8
#define I2C_SCL  9

// ---- Direccion I2C del expansor de pines -------------------
#define PI4IO_ADDR 0x43   // Direccion tipica del PI4IOE5V6408

// ---- Pantalla Cardputer ADV --------------------------------
#define LCD_WIDTH  240
#define LCD_HEIGHT 135

// ---- Parametros de UI --------------------------------------
#define UI_HEADER_H   18  // px de altura del header
#define UI_FOOTER_H   16  // px de altura del footer (frecuencia / status)
#define UI_CONTENT_Y  (UI_HEADER_H + 2)
#define UI_CONTENT_H  (LCD_HEIGHT - UI_HEADER_H - UI_FOOTER_H - 4)

// ---- Colores (RGB565) --------------------------------------
#define COL_BG         0x0000   // negro
#define COL_HEADER_BG  0x001F   // azul oscuro
#define COL_ACCENT     0x07FF   // cian
#define COL_TEXT       0xFFFF   // blanco
#define COL_TEXT_DIM   0x7BEF   // gris claro
#define COL_SEL_BG     0x0410   // verde oscuro (seleccion)
#define COL_WARNING    0xFBE0   // amarillo
#define COL_DANGER     0xF800   // rojo
#define COL_OK         0x07E0   // verde

// ---- Version del firmware ----------------------------------
#define FW_VERSION "1.5.5"
#define APP_NAME   "PoisonMesh"
