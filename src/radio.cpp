// ============================================================
//  radio.cpp  -  Motor de Radio (SX1262 + Cap LoRa-1262)
// ============================================================
//
//  NOTA SOBRE EL ANTENNA SWITCH (PI4IOE P0 / FM8625H):
//
//  El PI4IOE esta en el mismo bus I2C que el BMI270 y el ES8311.
//  M5Unified mantiene un lock de FreeRTOS sobre Wire que impide
//  acceso directo - el Wire.endTransmission() retorna error 5.
//
//  Sin embargo, el hardware pone P0 = HIGH por defecto al
//  energizar, entonces el switch ya esta activo.
//
//  Para manejar correctamente el switching TX/RX usamos
//  radio.setDio2AsRfSwitch(true): DIO2 del SX1262 controla
//  el switch de antena directamente sin pasar por I2C.
//  Esto es lo que recomienda MeshCore para este hardware.
// ============================================================
#include "radio.h"
#include <SPI.h>
#include <M5Unified.h>

SX1262 g_radio = new Module(LORA_NSS, LORA_IRQ, LORA_RST, LORA_BUSY);

static RadioState    s_state   = RadioState::Uninitialized;
static float         s_freqMHz = 0.0f;
static volatile bool s_rxFlag  = false;

static void IRAM_ATTR _rx_isr() {
    s_rxFlag = true;
}

// ---- Init completo ----------------------------------------
bool radio_init(const LoRaConfig& cfg) {
    Serial.println("[Radio] Iniciando...");
    s_state = RadioState::Error;

    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);

    const MeshPreset& preset = PRESETS[cfg.presetIdx];
    const MeshRegion& region = REGIONS[cfg.regionIdx];

    float freqMHz = (cfg.freqMHz > 0.0f)
        ? cfg.freqMHz
        : calc_freq_mhz(cfg.regionIdx, cfg.presetIdx, cfg.slot);

    int8_t txPower = (int8_t)region.maxPower;
    if (txPower > 22) txPower = 22;

    Serial.printf("[Radio] %.3f MHz | BW:%.0fkHz | SF:%u | CR:4/%u | %ddBm\n",
        freqMHz, preset.bwKHz, preset.sf, preset.cr, txPower);

    int state = g_radio.begin(
        freqMHz,
        preset.bwKHz,
        preset.sf,
        preset.cr,
        0x2B,       // sync word Meshtastic publico
        txPower,
        16,         // preamble length (Meshtastic estandar)
        1.6f,       // TCXO voltage del Cap LoRa-1262
        false       // DC-DC regulator
    );

    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("[Radio] ERROR begin: %d\n", state);
        return false;
    }

    // DIO2 controla el switch de antena TX/RX del FM8625H
    // Esto reemplaza la necesidad de manejar el PI4IOE por I2C
    g_radio.setDio2AsRfSwitch(true);
    g_radio.setPreambleLength(16);  // Meshtastic usa 16 (no 8 default)

    g_radio.setCurrentLimit(140);
    g_radio.setCRC(2);
    g_radio.setDio1Action(_rx_isr);

    s_freqMHz = freqMHz;
    s_state   = RadioState::Idle;
    Serial.println("[Radio] SX1262 OK - DIO2 RF switch activo");
    return true;
}

// ---- Cambiar config sin reinit ----------------------------
void radio_apply_config(const LoRaConfig& cfg) {
    if (s_state == RadioState::Uninitialized) return;
    float freqMHz = calc_freq_mhz(cfg.regionIdx, cfg.presetIdx, cfg.slot);
    const MeshPreset& preset = PRESETS[cfg.presetIdx];
    g_radio.setFrequency(freqMHz);
    g_radio.setBandwidth(preset.bwKHz);
    g_radio.setSpreadingFactor(preset.sf);
    g_radio.setCodingRate(preset.cr);
    s_freqMHz = freqMHz;
}

// ---- RX promiscuo -----------------------------------------
RadioResult radio_start_receive() {
    if (s_state == RadioState::Uninitialized) return RadioResult::ErrInit;
    s_rxFlag = false;
    int state = g_radio.startReceive();
    if (state != RADIOLIB_ERR_NONE) return RadioResult::ErrInit;
    s_state = RadioState::Receiving;
    return RadioResult::OK;
}

// ---- TX ---------------------------------------------------
RadioResult radio_transmit(const uint8_t* data, size_t len) {
    if (s_state == RadioState::Uninitialized) return RadioResult::ErrInit;
    bool wasRx = (s_state == RadioState::Receiving);
    s_state = RadioState::Transmitting;

    int state = g_radio.transmit(const_cast<uint8_t*>(data), len);

    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("[Radio] TX error: %d\n", state);
        if (wasRx) radio_start_receive();
        return RadioResult::ErrTX;
    }

    if (wasRx) radio_start_receive();
    else s_state = RadioState::Idle;
    return RadioResult::OK;
}

// ---- Leer paquete -----------------------------------------
bool radio_packet_available() { return s_rxFlag; }

RadioResult radio_read_packet(RawPacket& pkt) {
    if (!s_rxFlag) return RadioResult::ErrNoPacket;
    s_rxFlag = false;

    pkt.len = g_radio.getPacketLength();
    if (pkt.len == 0 || pkt.len > sizeof(pkt.data)) {
        g_radio.startReceive();
        return RadioResult::ErrCRC;
    }

    int state = g_radio.readData(pkt.data, pkt.len);
    pkt.rssi      = g_radio.getRSSI();
    pkt.snr       = g_radio.getSNR();
    pkt.timestamp = millis();

    g_radio.startReceive();
    return (state == RADIOLIB_ERR_NONE || state == RADIOLIB_ERR_CRC_MISMATCH)
        ? RadioResult::OK : RadioResult::ErrCRC;
}

// ---- Sleep / Wake -----------------------------------------
void radio_sleep() {
    if (s_state == RadioState::Sleep) return;
    g_radio.sleep();
    s_state = RadioState::Sleep;
}

void radio_wake() {
    if (s_state != RadioState::Sleep) return;
    g_radio.standby();
    s_state = RadioState::Idle;
}

RadioState radio_get_state()    { return s_state; }
float      radio_get_freq_mhz() { return s_freqMHz; }
