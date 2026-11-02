#pragma once
// ============================================================
//  meshtastic_parser.h  -  Parser de paquetes recibidos
//  Decodifica el header y el User/Position de paquetes sniffeados
// ============================================================
#include "meshtastic_proto.h"
#include <string.h>

// ---- Nodo descubierto en la red --------------------------
struct DiscoveredNode {
    uint32_t node_id;
    char     long_name[40];
    char     short_name[8];
    float    lat;
    float    lon;
    int32_t  alt;
    uint8_t  public_key[32];
    uint8_t  pubkey_len;
    int16_t  rssi;
    float    snr;
    uint32_t last_seen;    // millis()
    bool     has_position;
    bool     has_pubkey;
};

// ---- Reader de protobuf (decodificador) ------------------
class ProtoReader {
public:
    const uint8_t* buf;
    size_t len;
    size_t pos;

    ProtoReader(const uint8_t* b, size_t l) : buf(b), len(l), pos(0) {}

    bool at_end() const { return pos >= len; }

    bool read_varint(uint64_t& val) {
        val = 0;
        int shift = 0;
        while (pos < len) {
            uint8_t b = buf[pos++];
            val |= (uint64_t)(b & 0x7F) << shift;
            if (!(b & 0x80)) return true;
            shift += 7;
            if (shift >= 64) return false;
        }
        return false;
    }

    // Lee un fixed32/sfixed32 (wire type 5): 4 bytes little-endian.
    bool read_fixed32(uint32_t& val) {
        if (pos + 4 > len) return false;
        val =  (uint32_t)buf[pos]
            | ((uint32_t)buf[pos + 1] << 8)
            | ((uint32_t)buf[pos + 2] << 16)
            | ((uint32_t)buf[pos + 3] << 24);
        pos += 4;
        return true;
    }

    bool read_tag(uint32_t& field, uint8_t& wire_type) {
        uint64_t tag_val;
        if (!read_varint(tag_val)) return false;
        field     = (uint32_t)(tag_val >> 3);
        wire_type = (uint8_t)(tag_val & 0x07);
        return field > 0;
    }

    bool skip_field(uint8_t wire_type) {
        uint64_t tmp;
        switch (wire_type) {
            case 0: return read_varint(tmp);
            case 1: if (pos + 8 > len) return false; pos += 8; return true;
            case 2: {
                if (!read_varint(tmp)) return false;
                if (pos + tmp > len)  return false;
                pos += (size_t)tmp;
                return true;
            }
            case 5: if (pos + 4 > len) return false; pos += 4; return true;
            default: return false;
        }
    }

    bool read_bytes(uint8_t* out, size_t out_cap, size_t& out_len) {
        uint64_t sz;
        if (!read_varint(sz)) return false;
        if (sz > out_cap || pos + sz > len) { pos += sz; out_len = 0; return false; }
        memcpy(out, buf + pos, (size_t)sz);
        out_len = (size_t)sz;
        pos += (size_t)sz;
        return true;
    }

    bool read_string(char* out, size_t out_cap) {
        uint64_t sz;
        if (!read_varint(sz)) return false;
        size_t copy = (sz < out_cap - 1) ? (size_t)sz : out_cap - 1;
        memcpy(out, buf + pos, copy);
        out[copy] = '\0';
        pos += (size_t)sz;
        return true;
    }

    bool read_sub_reader(ProtoReader& sub) {
        uint64_t sz;
        if (!read_varint(sz)) return false;
        if (pos + sz > len)   return false;
        sub = ProtoReader(buf + pos, (size_t)sz);
        pos += (size_t)sz;
        return true;
    }
};

// ---- Parsear header del paquete LoRa ---------------------
inline bool mesh_parse_header(const uint8_t* data, size_t len,
    MeshPacketHeader& hdr)
{
    if (len < MESH_HEADER_SIZE) return false;
    memcpy(&hdr, data, MESH_HEADER_SIZE);
    return true;
}

// ---- Parsear Data wrapper --------------------------------
struct MeshData {
    uint32_t portnum;
    uint8_t  payload[MESH_MAX_PAYLOAD];
    size_t   payload_len;
};

inline bool mesh_parse_data(const uint8_t* data, size_t len, MeshData& out) {
    if (len < MESH_HEADER_SIZE) return false;

    ProtoReader r(data + MESH_HEADER_SIZE, len - MESH_HEADER_SIZE);
    out.portnum     = 0;
    out.payload_len = 0;

    while (!r.at_end()) {
        uint32_t field; uint8_t wtype;
        if (!r.read_tag(field, wtype)) break;
        if      (field == 1 && wtype == 0) { uint64_t v; r.read_varint(v); out.portnum = (uint32_t)v; }
        else if (field == 2 && wtype == 2) { r.read_bytes(out.payload, sizeof(out.payload), out.payload_len); }
        else r.skip_field(wtype);
    }
    return out.portnum != 0;
}

// ---- Parsear User (PORTNUM_NODEINFO) ---------------------
inline bool mesh_parse_user(const uint8_t* payload, size_t len,
    DiscoveredNode& node)
{
    ProtoReader r(payload, len);
    while (!r.at_end()) {
        uint32_t field; uint8_t wtype;
        if (!r.read_tag(field, wtype)) break;
        switch (field) {
            case 2: r.read_string(node.long_name,  sizeof(node.long_name));  break;
            case 3: r.read_string(node.short_name, sizeof(node.short_name)); break;
            // public_key es el CAMPO 8 del User (mesh.proto); el 7 es 'role'.
            case 8: {
                size_t klen;
                if (r.read_bytes(node.public_key, 32, klen)) {
                    node.pubkey_len = (uint8_t)klen;
                    node.has_pubkey = (klen > 0);
                }
                break;
            }
            default: r.skip_field(wtype); break;
        }
    }
    return strlen(node.long_name) > 0;
}

// ---- Parsear Position (PORTNUM_POSITION) -----------------
inline bool mesh_parse_position(const uint8_t* payload, size_t len,
    DiscoveredNode& node)
{
    ProtoReader r(payload, len);
    int32_t lat_i = 0, lon_i = 0;

    while (!r.at_end()) {
        uint32_t field; uint8_t wtype;
        if (!r.read_tag(field, wtype)) break;
        uint64_t v; uint32_t fx;
        switch (field) {
            // latitude_i / longitude_i son sfixed32 (wire type 5, 4 bytes LE)
            case 1: if (wtype == 5 && r.read_fixed32(fx)) lat_i = (int32_t)fx;
                    else r.skip_field(wtype); break;
            case 2: if (wtype == 5 && r.read_fixed32(fx)) lon_i = (int32_t)fx;
                    else r.skip_field(wtype); break;
            // altitude es int32 (varint, wire type 0)
            case 3: r.read_varint(v); node.alt = (int32_t)(uint32_t)v; break;
            default: r.skip_field(wtype); break;
        }
    }

    if (lat_i == 0 && lon_i == 0) return false;
    node.lat = (float)lat_i / 1e7f;
    node.lon = (float)lon_i / 1e7f;
    node.has_position = true;
    return true;
}
