#pragma once
// ============================================================
//  radio.h  -  Motor de Radio LoRa (SX1262 + Cap LoRa-1262)
// ============================================================
#include <RadioLib.h>
#include "config.h"
#include "meshtastic_freq.h"
#include "meshtastic_proto.h"
#include "ui.h"   // ? LoRaConfig esta definido en ui.h

enum class RadioState : uint8_t {
    Uninitialized,
    Idle,
    Receiving,
    Transmitting,
    Error,
    Sleep,
};

enum class RadioResult : int8_t {
    OK           =  0,
    ErrInit      = -1,
    ErrTimeout   = -2,
    ErrCRC       = -3,
    ErrNoPacket  = -4,
    ErrTX        = -5,
};

// RawPacket definido en meshtastic_proto.h

bool        radio_init(const LoRaConfig& cfg);
void        radio_apply_config(const LoRaConfig& cfg);
RadioResult radio_start_receive();
RadioResult radio_transmit(const uint8_t* data, size_t len);
bool        radio_packet_available();
RadioResult radio_read_packet(RawPacket& pkt);
void        radio_sleep();
void        radio_wake();
RadioState  radio_get_state();
float       radio_get_freq_mhz();

extern SX1262 g_radio;
