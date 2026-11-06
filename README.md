# PoisonMesh by Teno

Herramienta de pentesting para redes **Meshtastic**, diseñada para el **M5Stack Cardputer ADV** con el modulo **Cap LoRa-1262 (SX1262)**.

> ?? **ADVERTENCIA LEGAL**: Esta herramienta es exclusivamente para
> pentesting en redes propias o con autorización explicita del propietario.
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
| 5 | Mesh Move | Reubicacion automatica de los nodos descubiertos, renombrando como "Moved" |
| 6 | Mesh Impersonator | Suplantar un nodo para enviar mensajes de texto (predefinidos o personalizados, con flood opcional) |
| 7 | PKI Poison | CVE-2025-55293: borrar la clave publica de un nodo |
| 8 | ChattyMesh | Suplantacion continua de nodos aleatorios con mensajes de texto aleatorios (toggle ON/OFF, delay aleatorio 2-6s) |
| 9 | GhostMesh | Nodo fantasma persistente (nombre manual + clave X25519 derivada del id) para suplantar mensajes durante toda la sesion |
| 10 | NullMeshGrow | CVE-2026-42566: inyeccion de nodos con owner (`long_name`/`short_name`) malformado (bytes nulos, espacios y UTF-8 roto). Cada nodo genera su par X25519, envia NODEINFO x2 cada 5s y crea un nodo nuevo a los 15s (toggle ON/OFF) |

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
| `Fn` + `;` | ? Subir (navegar lista) |
| `Fn` + `.` | ? Bajar (navegar lista) |
| `Fn` + `,` | ? Valor anterior |
| `Fn` + `/` | ? Valor siguiente |
| `Enter` | Confirmar / toggle ON-OFF |
| `Backspace` | Borrar caracter (en texto) |
| `` ` `` | Escape / Salir |
| `Fn` + `Backspace` | Escape (alternativo) |

> **Nota**: las flechas de direccion funcionan **solo con `Fn`**. Sin `Fn`,
> `;` `.` `,` `/` son caracteres escribibles (utiles en los editores de texto,
> como Namechange o el mensaje personalizado del Mesh Impersonator).

---

## Créditos

- Investigacion y desarrollo: **Teno**
- Referencias: meshtastic/firmware, CVE-2025-55293, CVE-2024-51500


