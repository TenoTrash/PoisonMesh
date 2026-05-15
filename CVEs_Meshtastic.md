# CVEs Relacionados con Meshtastic

## Introducción

Meshtastic es un proyecto de comunicación LoRa mesh de código abierto ampliamente adoptado por comunidades de radioaficionados, equipos de emergencia, eventos y redes comunitarias. Su arquitectura descentralizada y sin autenticación fuerte ha dado lugar a múltiples vulnerabilidades documentadas.

Aquí se describen los CVEs públicamente conocidos y vulnerabilidades relevantes del protocolo al momento de la investigacion (2025-2026).

---

## CVE-2024-51500 — LoRa Packet Amplification / DDoS

**Severidad:** Alta (CVSS 7.5)
**Afecta:** Meshtastic firmware < 2.5.0
**Tipo:** Amplificación de paquetes / Denegación de servicio

### Descripción

Un atacante puede enviar un paquete LoRa especialmente construido con el campo `from` seteado a `0xFFFFFFFF` (broadcast address) y `hop_limit` en su valor máximo (7). Cada nodo que recibe este paquete lo rebroadcastea automaticamente sin verificar la legitimidad del origen, generando una tormenta de retransmisiones que satura el espectro de radio y deja la red inoperativa.

### Mecanismo técnico

El firmware procesa paquetes broadcast sin validar que el campo `from` sea una dirección de nodo válida. El algoritmo de flood control usa el par `(from, packet_id)` para evitar retransmisiones duplicadas, pero con `from=0xFFFFFFFF` cada paquete con un `packet_id` único es tratado como nuevo, forzando retransmisión en cadena.

### Impacto

- Saturación del canal LoRa en toda la red alcanzable
- Pérdida de comunicaciones legítimas durante el ataque
- Consumo acelerado de batería en todos los nodos afectados
- Radio efectivo de impacto proporcional al `hop_limit`

### Estado del parche

Mitigado parcialmente en 2.5.x mediante validación adicional del campo `from`.
La mitigación completa requiere cambios en el protocolo de flooding.

---

## CVE-2025-55293 — PKI Public Key Poisoning

**Severidad:** Media-Alta (CVSS 6.8)
**Afecta:** Meshtastic firmware 2.5.x - 2.7.x con PKI habilitado
**Tipo:** Corrupción de estado de seguridad / Spoofing de identidad criptográfica

### Descripcion

Meshtastic implementó un sistema de cifrado asimétrico (XEdDSA/X25519) en versiones 2.5+. Sin embargo, el mecanismo de actualización de claves públicas en el NodeDB no requiere autenticación: cualquier nodo puede enviar un mensaje `NodeInfo` con el `node_id` de otro nodo y una `public_key` vacia o falsa.

El firmware receptor acepta esta actualización y marca al nodo afectado como "sin clave" o "clave comprometida", mostrando una alerta visual en los clientes (ícono rojo / advertencia de seguridad) y deshabilitando el cifrado punto a punto
para ese nodo.

### Mecanismo técnico

El campo `public_key` (field 8 del User proto) se almacena sin verificación de firma. El receptor no puede distinguir entre un `NodeInfo` enviado por el nodo real y uno enviado por un atacante que conoce el `node_id` de la víctima.

La unica protección existente es el campo `HAS_XEDDSA_SIGNED` que el firmware setea una vez que el nodo envio al menos un mensaje firmado. Sin embargo, este flag solo existe en el estado local del receptor y puede ser reseteado si el receptor reinicia o si el atacante satura el NodeDB.

### Impacto

- El nodo víctima aparece con alerta de seguridad (icono rojo) en todos los clientes cercanos que reciban el paquete malicioso
- Se deshabilita el cifrado E2E para ese nodo hasta que recupere su posición en el NodeDB con su clave real
- Permite ataques de degradación: forzar comunicaciones en texto claro

### Estado del parche

En evaluación por el equipo de Meshtastic. No hay parche disponible a la fecha de esta investigación que impida completamente el ataque sin rediseñar el protocolo de actualización de NodeInfo.

---

## Vulnerabilidad sin CVE asignado — NodeInfo Spoofing (Identity Spoofing)

**Severidad:** Alta
**Afecta:** Todas las versiones de Meshtastic
**Tipo:** Suplantación de identidad de nodo

### Descripción

El protocolo Meshtastic no implementa autenticación del origen en paquetes `NodeInfo` (PORTNUM=4). Cualquier dispositivo con acceso al canal puede enviar un paquete con el `from` del header seteado al `node_id` de cualquier nodo conocido y un payload `User` arbitrario.

El firmware receptor acepta el `NodeInfo` y actualiza su base de datos de nodos con los datos del atacante: nombre largo, nombre corto, modelo de hardware, y coordenadas GPS.

### Mecanismo técnico

La unica validación existente en firmware 2.7.x es el campo `is_licensed` (field 6 del User proto) y el campo `role` (field 7). Si el atacante conoce estos valores del nodo victima (o usa los defaults: `is_licensed=false`, `role=CLIENT`), el paquete es aceptado sin restricciones.

### Impacto

- Cambio de nombre visible de cualquier nodo en todos los clientes de la red
- Modificación del hardware model reportado (ícono en la app)
- Persistencia: el cambio dura hasta que el nodo real re-anuncie su NodeInfo

---

## Vulnerabilidad sin CVE asignado — Position Spoofing

**Severidad:** Alta
**Afecta:** Todas las versiones de Meshtastic
**Tipo:** Falsificación de ubicacion GPS

### Descripcion

Los paquetes `Position` (PORTNUM=3) no están autenticados. Un atacante puede enviar coordenadas GPS arbitrarias usando el `node_id` de un nodo victima como campo `from`, haciendo que todos los clientes de la red muestren ese nodo en una ubicación geográfica falsa.

### Mecanismo técnico

El proto `Position` usa `sfixed32` para `latitude_i` y `longitude_i` (grados * 1e7) y `fixed32` para el timestamp Unix. El firmware receptor acepta cualquier posición siempre que el timestamp sea mayor al ultimo conocido y el campo `location_source` sea válido (LOC_MANUAL=1, LOC_INTERNAL=2).

### Impacto

- Nodos aparecen en mapas en ubicaciones falsas (paises, continentes distintos)
- Afecta aplicaciones que dependen de la ubicación para enrutamiento
- Especialmente crítico en operaciones de emergencia o SAR

---

## Vulnerabilidad sin CVE asignado — Text Message Spoofing

**Severidad:** Media-Alta
**Afecta:** Todas las versiones de Meshtastic
**Tipo:** Suplantación en mensajería

### Descripcion

Los mensajes de texto (PORTNUM=1) no estan autenticados a nivel de protocolo. El campo `from` del header puede ser cualquier `node_id`, permitiendo enviar mensajes que aparecen como si hubieran sido enviados por otro nodo de la red.

### Mecanismo técnico

El payload de PORTNUM=1 es texto UTF-8 plano sin protobuf. La única "autenticación" es el cifrado AES-CTR con la PSK del canal, que protege el contenido del mensaje pero no la identidad del emisor: cualquiera que conozca la PSK puede enviar mensajes como cualquier nodo.

### Impacto

- Mensajes falsos atribuidos a usuarios reales
- Posibles consecuencias sociales en redes comunitarias activas
- Desinformación en operaciones de emergencia

---

## Consideraciones sobre el diseño del protocolo

Las vulnerabilidades descritas comparten una causa raiz común: **el protocolo Meshtastic fue disenado priorizando la facilidad de adopción y el rendimiento en canales LoRa de bajo ancho de banda, sacrificando la autenticación de origen**.

Los canales LoRa tienen limitaciones severas:
- Ancho de banda: 125-500 kHz
- Airtime por paquete: 300ms a 9 segundos segun preset
- Duty cycle regulatorio: 1-10% en muchas regiones

Agregar firmas criptográficas (XEdDSA, 64 bytes) a cada paquete multiplica el overhead de forma significativa. El equipo de Meshtastic ha optado por implementar PKI opcionalmente (2.5+) en lugar de mandatoriamente, lo que crea una superficie de ataque híbrida donde coexisten nodos firmados y no firmados.

---

## Referencias

- Meshtastic Security Policy: https://meshtastic.org/docs/overview/security/
- Meshtastic Firmware GitHub: https://github.com/meshtastic/firmware
- Meshtastic Protobufs: https://github.com/meshtastic/protobufs
- DarkMesh (referencia de investigacion): https://github.com/htotoo/DarkMesh
- NVD CVE-2024-51500: https://nvd.nist.gov/vuln/detail/CVE-2024-51500
- NVD CVE-2025-55293: https://nvd.nist.gov/vuln/detail/CVE-2025-55293
