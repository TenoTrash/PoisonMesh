#pragma once
// ============================================================
//  meshtastic_proto.h  -  Estructuras y serializacion manual
//  del protocolo Meshtastic sobre el aire (LoRa)
//
//  Basado en:
//    - meshtastic/protobufs (mesh.proto)
//    - htotoo/DarkMesh (logica de ataques)
//    - CVE-2025-55293 (PKI Poison)
//    - CVE-2024-51500 (DDoS amplification)
//
//  Formato del paquete LoRa de Meshtastic:
//  ???????????????????????????????????????????
//  ?  PacketHeader (16 bytes, sin cifrar)    ?
//  ?    to       : uint32  (4 bytes)         ?
//  ?    from     : uint32  (4 bytes)         ?
//  ?    id       : uint32  (4 bytes)         ?
//  ?    flags    : uint8   (hop_limit|want_ack?
//  ?    channel  : uint8                     ?
//  ?    next_hop : uint8                     ?
//  ?    relay_node: uint8                    ?
//  ???????????????????????????????????????????
//  ?  Payload (cifrado con AES-CTR o claro)  ?
//  ?  Protobuf serializado de Data           ?
//  ???????????????????????????????????????????
//
//  NOTA: Para pentesting enviamos paquetes SIN CIFRAR
//  (portnum + payload en texto claro, sync word 0x2B)
//  que los clientes Meshtastic con canal publico
//  (PSK default) pueden ver y procesar.
// ============================================================

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <Arduino.h>

// ---- Constantes del protocolo ----------------------------
#define MESH_BROADCAST_ADDR     0xFFFFFFFFUL  // broadcast
#define MESH_DEFAULT_HOP_LIMIT  7   // maximo alcance en la malla (antes 3)
#define MESH_MAX_HOP_LIMIT      7
#define MESH_HEADER_SIZE        16
#define MESH_MAX_PAYLOAD        237           // 253 - 16 header

// Tabla de channel hashes para cada preset con PSK default
// hash = XOR de todos los bytes de la PSK + XOR de todos los chars del nombre
// Calculado en meshtastic/firmware src/mesh/Channels.cpp ? getHash()
// Verificado contra firmware 2.7.x
static const uint8_t MESH_CHANNEL_HASHES[] = {
    0x0E,  // 0: ShortTurbo
    0x70,  // 1: ShortFast
    0x77,  // 2: ShortSlow
    0x1F,  // 3: MediumFast
    0x18,  // 4: MediumSlow
    0x08,  // 5: LongFast    ? default AMBA
    0x09,  // 6: LongModerate
    0x0F,  // 7: LongSlow
    0x37,  // 8: VeryLongSlow
};

inline uint8_t mesh_channel_hash(uint8_t preset_idx) {
    if (preset_idx >= sizeof(MESH_CHANNEL_HASHES)) return 0x08;
    return MESH_CHANNEL_HASHES[preset_idx];
}

// ---- Port Numbers (portnum) ------------------------------
// Define que tipo de mensaje lleva el payload
#define PORTNUM_UNKNOWN         0
#define PORTNUM_TEXT_MESSAGE    1
#define PORTNUM_REMOTE_HARDWARE 2
#define PORTNUM_POSITION        3
#define PORTNUM_NODEINFO        4
#define PORTNUM_ROUTING         5
#define PORTNUM_ADMIN           6
#define PORTNUM_MAP_REPORT      67
#define PORTNUM_WAYPOINT        250
#define PORTNUM_TRACEROUTE      70

// ---- Header del paquete (16 bytes, little-endian) --------
struct __attribute__((packed)) MeshPacketHeader {
    uint32_t to;          // destino (0xFFFFFFFF = broadcast)
    uint32_t from;        // origen (node ID del atacante)
    uint32_t id;          // ID unico del paquete
    uint8_t  flags;       // bits 0-2: hop_limit, bit3: want_ack, bit4: via_mqtt
    uint8_t  channel;     // indice de canal (0 = primario)
    uint8_t  next_hop;    // usado por el router
    uint8_t  relay_node;  // nodo que retransmitio
};

// ---- Generador de Node IDs aleatorios --------------------
inline uint32_t mesh_random_node_id() {
    // IDs validos: 0x00000001 - 0xFFFFFFFE
    uint32_t id = esp_random();
    if (id == 0 || id == 0xFFFFFFFF) id = 0xDEADBEEF;
    return id;
}

inline uint32_t mesh_random_packet_id() {
    return esp_random();
}

// ---- CRC-32 (IEEE 802.3 / zlib) -------------------------
// Mismo algoritmo que usa Meshtastic (ErriezCRC32::crc32Buffer)
// para derivar el node_num a partir de la clave publica.
// init = 0xFFFFFFFF, polinomio reflejado 0xEDB88320, xorout = 0xFFFFFFFF.
inline uint32_t crc32_ieee(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320UL & (0 - (crc & 1)));
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

// ---- Generador de par de claves X25519 (Curve25519) --------
// Usa mbedtls (ya linkeado para AES-CTR). Genera la clave privada
// y publica de 32 bytes cada una. La publica es un punto X25519
// valido, en el mismo formato que usan los nodos Meshtastic.
// Retorna true si pudo generar el par.
#include "mbedtls/ecdh.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"

inline bool mesh_x25519_keypair(uint8_t* pub_key, uint8_t* priv_key) {
    if (!pub_key || !priv_key) return false;

    mbedtls_ecdh_context ctx;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_ecdh_init(&ctx);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);

    const char* pers = "poisonmesh-ghost";
    int ok = 0;

    do {
        if (mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy,
                (const unsigned char*)pers, strlen(pers)) != 0) { ok = -1; break; }

        if (mbedtls_ecp_group_load(&ctx.grp, MBEDTLS_ECP_DP_CURVE25519) != 0) { ok = -1; break; }

        if (mbedtls_ecdh_gen_public(&ctx.grp, &ctx.d, &ctx.Q,
                mbedtls_ctr_drbg_random, &drbg) != 0) { ok = -1; break; }

        // Extraer privada (d) y publica (Q) en formato de 32 bytes
        if (mbedtls_mpi_write_binary(&ctx.d, priv_key, 32) != 0) { ok = -1; break; }

        // Para Curve25519, Q.X (coordenada u) ya es la pubkey de 32 bytes
        if (mbedtls_mpi_write_binary(&ctx.Q.X, pub_key, 32) != 0) { ok = -1; break; }

    } while (0);

    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    mbedtls_ecdh_free(&ctx);

    return (ok == 0);
}

// ============================================================
//  SERIALIZACION PROTOBUF MANUAL (wire format)
//
//  Protobuf encoding:
//    field_tag = (field_number << 3) | wire_type
//    wire_type: 0=varint, 1=64bit, 2=length-delimited, 5=32bit
//
//  Helper class para escribir en un buffer
// ============================================================
class ProtoWriter {
public:
    uint8_t* buf;
    size_t   cap;
    size_t   pos;

    ProtoWriter(uint8_t* buffer, size_t capacity)
        : buf(buffer), cap(capacity), pos(0) {}

    size_t size() const { return pos; }
    bool   ok()   const { return pos <= cap; }

    // Varint (uint32/uint64)
    bool write_varint(uint64_t val) {
        do {
            if (pos >= cap) return false;
            uint8_t b = val & 0x7F;
            val >>= 7;
            if (val) b |= 0x80;
            buf[pos++] = b;
        } while (val);
        return true;
    }

    // Tag (field_number, wire_type)
    bool write_tag(uint32_t field_number, uint8_t wire_type) {
        return write_varint((field_number << 3) | wire_type);
    }

    // Campo varint (wire_type 0)
    bool write_field_uint32(uint32_t field, uint32_t val) {
        if (val == 0) return true; // default value ? no escribir
        return write_tag(field, 0) && write_varint(val);
    }

    // Campo varint forzado (aunque sea 0)
    bool write_field_uint32_forced(uint32_t field, uint32_t val) {
        return write_tag(field, 0) && write_varint(val);
    }

    // Campo int32 con signo (wire_type 0, ZigZag NO usado en mesh)
    bool write_field_int32(uint32_t field, int32_t val) {
        if (val == 0) return true;
        return write_tag(field, 0) && write_varint((uint64_t)(uint32_t)val);
    }

    // Campo bytes/string (wire_type 2)
    bool write_field_bytes(uint32_t field, const uint8_t* data, size_t len) {
        if (len == 0) return true;
        if (!write_tag(field, 2)) return false;
        if (!write_varint(len))   return false;
        if (pos + len > cap)      return false;
        memcpy(buf + pos, data, len);
        pos += len;
        return true;
    }

    bool write_field_string(uint32_t field, const char* str) {
        return write_field_bytes(field, (const uint8_t*)str, strlen(str));
    }

    // Sub-mensaje (wire_type 2): escribe el contenido de otro ProtoWriter
    bool write_field_message(uint32_t field, const ProtoWriter& sub) {
        return write_field_bytes(field, sub.buf, sub.size());
    }

    // Fixed32 (wire_type 5) - no usado en mesh pero por completitud
    bool write_field_fixed32(uint32_t field, uint32_t val) {
        if (!write_tag(field, 5)) return false;
        if (pos + 4 > cap) return false;
        memcpy(buf + pos, &val, 4);
        pos += 4;
        return true;
    }
};

// ============================================================
//  BUILDER DE PAQUETES MESHTASTIC
//  Construye el paquete completo = header + payload serializado
// ============================================================

// Resultado de build: buffer listo para radio_transmit()
struct MeshTxPacket {
    uint8_t data[256];
    size_t  len;
    bool    valid;
};

// ---- Cabecera del paquete --------------------------------
inline void mesh_fill_header(MeshPacketHeader& hdr,
    uint32_t from, uint32_t to,
    uint8_t  hop_limit    = MESH_DEFAULT_HOP_LIMIT,
    bool     want_ack     = false,
    uint8_t  channel_hash = 0x08)  // default LongFast; usar mesh_channel_hash(presetIdx)
{
    memset(&hdr, 0, sizeof(hdr));
    hdr.to      = to;
    hdr.from    = from;
    hdr.id      = mesh_random_packet_id();
    // flags layout (doc oficial):
    //   bits 0-2: hop_limit
    //   bit    3: want_ack
    //   bit    4: via_mqtt (siempre 0 para transmision directa)
    //   bits 5-7: hop_start (= hop_limit original, para que los nodos
    //             puedan calcular cuantos hops viajo el paquete)
    hdr.flags   = (hop_limit & 0x07)
                | (want_ack ? 0x08 : 0x00)
                | ((hop_limit & 0x07) << 5);  // hop_start = hop_limit inicial
    // channel hash: 8 = LongFast publico con PSK default
    // CRITICO: los nodos filtran paquetes con channel hash incorrecto
    hdr.channel    = channel_hash;
    // relay_node = ultimo byte del from, como hacen los nodos reales
    // (indica quien origino/retransmitio el paquete por ultima vez)
    hdr.relay_node = (uint8_t)(from & 0xFF);
}

// ---- Serializar Data wrapper (portnum + payload) ----------
//  Data {
//    portnum   = field 1 (uint32)
//    payload   = field 2 (bytes)
//    want_response = field 3 (bool)
//  }
inline size_t mesh_build_data(uint8_t* out, size_t out_cap,
    uint32_t portnum,
    const uint8_t* payload, size_t payload_len)
{
    ProtoWriter w(out, out_cap);
    w.write_field_uint32(1, portnum);
    w.write_field_bytes(2, payload, payload_len);
    return w.ok() ? w.size() : 0;
}

// ============================================================
//  CIFRADO AES-CTR (PSK default del canal LongFast publico)
//
//  La PSK default de Meshtastic es: AQ== en base64 = 0x01
//  expandida a 16 bytes: { 0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29,
//  0x07, 0x59, 0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01 }
//
//  El cifrado es AES-128-CTR con nonce = packet_id (32 bit) + from (32 bit)
//  expandidos a 128 bits como: { packet_id_LE, from_LE, 0x00...0x00 }
//
//  Usamos un XOR stream cipher simplificado basado en el mismo nonce
//  que es suficiente para compatibilidad con clientes Meshtastic reales.
//
//  Referencia: meshtastic/firmware src/mesh/CryptoEngine.cpp
// ============================================================

// PSK default de Meshtastic (canal LongFast publico)
static const uint8_t MESH_DEFAULT_PSK[16] = {
    0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
    0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01
};

// Cifra/descifra el payload usando AES-CTR con nonce derivado del header
// Como no tenemos AES en el ESP32 sin mbedTLS, usamos la implementacion
// del ESP-IDF que si esta disponible en el framework Arduino-ESP32
#include "mbedtls/aes.h"

inline void mesh_crypt_payload(uint8_t* payload, size_t len,
    uint32_t packet_id, uint32_t from_node)
{
    if (len == 0) return;

    // IV de 128 bits para AES-256-CTR (doc oficial Meshtastic):
    //
    // "The IV for each block is constructed by concatenating the NONCE
    //  as the upper 96 bits of the IV and the COUNTER as the lower 32 bits"
    //
    // NONCE (96 bits) = node_num (32 bits) + packet_id (64 bits)
    // ? el node_num va PRIMERO, luego el packet_id
    //
    // Fuente: meshtastic.org/docs/about/overview/encryption/comments/
    // "The node number concatenated with the packet number is used as the NONCE"
    //
    // Layout del IV (16 bytes, big-endian para AES):
    //   bytes  0-3 : from_node  (uint32 LE ? MSB first en el IV)
    //   bytes  4-11: packet_id  (uint64 LE ? MSB first en el IV)
    //   bytes 12-15: counter    (empieza en 0, mbedtls lo maneja)
    //
    // NOTA: mbedtls_aes_crypt_ctr trata el nonce como big-endian counter,
    // pero Meshtastic pasa el IV como bloque de 16 bytes directamente.
    // El firmware usa ESP32 hardware AES con el IV construido asi:
    //   uint8_t scratch[16] = {0};
    //   memcpy(scratch, &packet_id, 8);     // bytes 0-7: packet_id LE
    //   memcpy(scratch+8, &from_node, 4);   // bytes 8-11: from_node LE
    //   // bytes 12-15 = 0 (block counter)
    //
    // Ver CryptoEngine.cpp en meshtastic/firmware:
    //   initNonce(uint32_t fromNode, uint64_t packetId)
    //   memcpy(nonce, &packetId, 8);
    //   memcpy(nonce + 8, &fromNode, 4);
    uint8_t nonce[16] = {0};
    // bytes 0-7: packet_id como uint64 little-endian
    nonce[0] = (packet_id)       & 0xFF;
    nonce[1] = (packet_id >> 8)  & 0xFF;
    nonce[2] = (packet_id >> 16) & 0xFF;
    nonce[3] = (packet_id >> 24) & 0xFF;
    nonce[4] = 0x00;
    nonce[5] = 0x00;
    nonce[6] = 0x00;
    nonce[7] = 0x00;
    // bytes 8-11: from_node como uint32 little-endian
    nonce[8]  = (from_node)       & 0xFF;
    nonce[9]  = (from_node >> 8)  & 0xFF;
    nonce[10] = (from_node >> 16) & 0xFF;
    nonce[11] = (from_node >> 24) & 0xFF;
    // bytes 12-15 = 0x00 (block counter, mbedtls incrementa internamente)

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, MESH_DEFAULT_PSK, 128);

    uint8_t stream_block[16] = {0};
    size_t  nc_off = 0;
    uint8_t nonce_copy[16];
    memcpy(nonce_copy, nonce, 16);

    mbedtls_aes_crypt_ctr(&aes, len, &nc_off,
        nonce_copy, stream_block, payload, payload);

    mbedtls_aes_free(&aes);
}

// ---- Paquete final: header + data serializado ------------
inline MeshTxPacket mesh_build_packet(
    uint32_t from, uint32_t to,
    uint32_t portnum,
    const uint8_t* payload, size_t payload_len,
    uint8_t hop_limit    = MESH_DEFAULT_HOP_LIMIT,
    uint8_t channel_hash = 0x08)
{
    MeshTxPacket pkt;
    pkt.valid = false;
    pkt.len   = 0;

    if (MESH_HEADER_SIZE + payload_len + 4 > sizeof(pkt.data)) return pkt;

    // Header
    MeshPacketHeader hdr;
    mesh_fill_header(hdr, from, to, hop_limit, false, channel_hash);
    memcpy(pkt.data, &hdr, MESH_HEADER_SIZE);

    // Data wrapper serializado
    size_t data_len = mesh_build_data(
        pkt.data + MESH_HEADER_SIZE,
        sizeof(pkt.data) - MESH_HEADER_SIZE,
        portnum, payload, payload_len);

    if (data_len == 0) return pkt;

    // Cifrar el payload con AES-CTR (PSK default del canal publico)
    // CRITICO: sin esto los nodos descartan el paquete silenciosamente
    mesh_crypt_payload(
        pkt.data + MESH_HEADER_SIZE, data_len,
        hdr.id, hdr.from);

    pkt.len   = MESH_HEADER_SIZE + data_len;
    pkt.valid = true;
    return pkt;
}

// ---- Paquete LoRa recibido raw (resultado del sniffer) ---
// Definido aqui para evitar dependencias circulares con radio.h
struct RawPacket {
    uint8_t  data[256];
    size_t   len;
    float    rssi;
    float    snr;
    uint32_t timestamp;
};
