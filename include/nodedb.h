#pragma once
// ============================================================
//  nodedb.h  -  Base de datos de nodos descubiertos
//  Fase 4: escucha pasiva de la red Meshtastic
// ============================================================
#include "meshtastic_parser.h"
#include "meshtastic_proto.h"

#define NODEDB_MAX_NODES  32   // maximo nodos en RAM

class NodeDB {
public:
    DiscoveredNode nodes[NODEDB_MAX_NODES];
    uint8_t        count = 0;

    // Retorna el indice del nodo, o -1 si no existe
    int find(uint32_t node_id) const {
        for (int i = 0; i < count; i++)
            if (nodes[i].node_id == node_id) return i;
        return -1;
    }

    // Agrega o actualiza un nodo. Retorna su indice.
    int upsert(uint32_t node_id) {
        int idx = find(node_id);
        if (idx >= 0) return idx;
        if (count >= NODEDB_MAX_NODES) {
            // Descartar el mas viejo (indice 0, rotar)
            memmove(&nodes[0], &nodes[1],
                    sizeof(DiscoveredNode) * (NODEDB_MAX_NODES - 1));
            count = NODEDB_MAX_NODES - 1;
        }
        idx = count++;
        memset(&nodes[idx], 0, sizeof(DiscoveredNode));
        nodes[idx].node_id = node_id;
        return idx;
    }

    // Procesa un paquete raw recibido por el radio
    // Retorna true si se actualizo algun nodo
    bool process_packet(const RawPacket& pkt) {
        if (pkt.len < MESH_HEADER_SIZE) return false;

        MeshPacketHeader hdr;
        if (!mesh_parse_header(pkt.data, pkt.len, hdr)) return false;

        // Ignorar broadcasts como origen y paquetes propios
        if (hdr.from == MESH_BROADCAST_ADDR) return false;
        if (hdr.from == 0) return false;

        // Descifrar el payload antes de parsear
        // AES-CTR es simetrico: la misma funcion cifra y descifra
        // Copiamos los bytes cifrados y los desciframos in-place
        uint8_t decrypted[256];
        size_t payload_len = pkt.len - MESH_HEADER_SIZE;
        if (payload_len == 0 || payload_len > sizeof(decrypted)) return false;
        memcpy(decrypted, pkt.data + MESH_HEADER_SIZE, payload_len);
        mesh_crypt_payload(decrypted, payload_len, hdr.id, hdr.from);

        // Construir buffer completo (header + payload descifrado) para el parser
        uint8_t decrypted_pkt[256];
        memcpy(decrypted_pkt, pkt.data, MESH_HEADER_SIZE);
        memcpy(decrypted_pkt + MESH_HEADER_SIZE, decrypted, payload_len);

        MeshData data;
        if (!mesh_parse_data(decrypted_pkt, pkt.len, data)) {
            Serial.printf("[NodeDB] parse_data FAIL from=0x%08X len=%u\n",
                hdr.from, pkt.len);
            return false;
        }
        Serial.printf("[NodeDB] OK from=0x%08X portnum=%lu paylen=%u\n",
            hdr.from, data.portnum, data.payload_len);

        int idx = upsert(hdr.from);
        nodes[idx].rssi      = (int16_t)pkt.rssi;
        nodes[idx].snr       = pkt.snr;
        nodes[idx].last_seen = pkt.timestamp;

        bool updated = false;

        if (data.portnum == PORTNUM_NODEINFO && data.payload_len > 0) {
            updated = mesh_parse_user(data.payload, data.payload_len, nodes[idx]);
        }
        else if (data.portnum == PORTNUM_POSITION && data.payload_len > 0) {
            updated = mesh_parse_position(data.payload, data.payload_len, nodes[idx]);
        }

        // Si no tenemos nombre todavia, poner el node ID como nombre
        if (strlen(nodes[idx].long_name) == 0) {
            snprintf(nodes[idx].long_name, sizeof(nodes[idx].long_name),
                     "!%08x", hdr.from);
            snprintf(nodes[idx].short_name, sizeof(nodes[idx].short_name),
                     "%04x", (uint16_t)hdr.from);
        }

        return true;
    }

    void clear() { count = 0; }
};

// Instancia global
extern NodeDB g_nodedb;
