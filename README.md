# PoisonMesh by Teno

Herramienta de pentesting para redes **Meshtastic**, diseñada para el **M5Stack Cardputer ADV** con el modulo **Cap LoRa-1262 (SX1262)**.

> **ADVERTENCIA LEGAL**: Esta herramienta es exclusivamente para pentesting en redes propias o con autorización explicita del propietario.
> Originalmente pensado para responsible disclosure ante los developers de Meshtastic...

---

## Por motivos obvios el código no se encuentra disponible hasta la fecha

---

## Ataques implementados

| # | Nombre | Descripción |
|---|---|---|
| 1 | Flood Nodes | Inyeccion masiva de nodos fantasma con nombres y hardware aleatorio |
| 2 | MeshGrow | Inyeccion periodica de nodos con posicion aleatoria (toggle ON/OFF) |
| 3 | Namechange | Renombrar un nodo existente con placa a eleccion |
| 4 | Position Poison | Reubicar un nodo en destino predefinido (Malvinas, Egipto, Australia, Roma) |
| 5 | MeshMove | Reubicacion automatica de todos los nodos descubiertos, renombrando como "Moved" |
| 6 | PKI Poison | CVE-2025-55293: borrar la clave publica de un nodo |

---

## Hardware requerido

| Componente | Modelo |
|---|---|
| Unidad principal | M5Stack Cardputer ADV (ESP32-S3) |
| Modulo LoRa | Cap LoRa-1262 (SX1262, 850-960 MHz) |

### Pines SD (rockyou.txt opcional)
| Senal | GPIO |
|---|---|
| SCK | 40 |
| MISO | 39 |
| MOSI | 14 |
| CS | 12 |

---

## Compilación

1. Instalar VS Code + extension PlatformIO IDE
2. Abrir esta carpeta como proyecto
3. Build + Upload
4. Configurar region/preset en el Setup LoRa al arrancar

---

## Controles

| Tecla | Accion |
|---|---|
| `;` | ? Subir |
| `.` | ? Bajar |
| `,` | ? Valor anterior |
| `/` | ? Valor siguiente |
| `Enter` | Confirmar |
| `` ` `` | Escape / Salir |

---

## Créditos

- Investigacion y desarrollo: **Teno**
- Referencias: meshtastic/firmware, CVE-2025-55293, CVE-2024-51500


