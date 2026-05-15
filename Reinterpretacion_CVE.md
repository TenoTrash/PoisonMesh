# PoisonMesh: Reinterpretación de CVEs en Meshtastic 2.7.x

## Por que estos ataques siguen siendo efectivos

PoisonMesh fue desarrollado como herramienta de responsible disclosure para demostrar ante el equipo de desarrollo de Meshtastic que las vulnerabilidades de autenticación de identidad y posicion NO fueron completamente mitigadas en las versiones 2.5.x - 2.7.x, a pesar de la introducción del sistema PKI.

Este documento explica por qué cada ataque sigue siendo efectivo en el firmware mas reciente disponible al momento de la investigacion (2.7.x).

---

## CVE-2024-51500 — DDoS por amplificación

### Estado segun el equipo de Meshtastic
"Mitigado en 2.5.x mediante validacion del campo from."

### Por que sigue siendo efectivo
La mitigación implementada valida que `from != 0x00000000` pero no valida que `from` sea un nodo conocido en el NodeDB. Un paquete con `from=0xFFFFFFFF` y `hop_limit=7` sigue siendo procesado y retransmitido por nodos en modo ROUTER o CLIENT que tengan `rebroadcast_mode != NONE`.

PoisonMesh no implementa este ataque en su versión de presentación porque el impacto en redes de terceros es indiscriminado. Se documenta aqui para completar el análisis.

---

## CVE-2025-55293 — PKI Public Key Poisoning

### Estado según el equipo de Meshtastic
"El sistema PKI en 2.5+ protege las comunicaciones punto a punto."

### Por qué sigue siendo efectivo

**El sistema PKI de Meshtastic es opt-in y no mandatorio.**

En firmware 2.7.x, el NodeInfoModule procesa paquetes `NodeInfo` entrantes y actualiza la `public_key` almacenada localmente para ese `node_id` sin verificar que el paquete fue enviado por el nodo real.

La única proteccion es el flag `HAS_XEDDSA_SIGNED` que se setea la primera vez que un nodo envia un mensaje firmado. Pero:

1. Si el atacante envia el paquete PKI Poison ANTES de que el nodo real haya enviado su primer mensaje firmado, el flag no esta seteado y el ataque funciona sin restricciones.

2. Si el nodo víctima reinicia, pierde el flag temporalmente.

3. La advertencia visual (ícono rojo) que genera el ataque es suficiente para demostrar la vulnerabilidad ante una auditoría técnica.

**Linea de codigo responsable en firmware 2.7.x:**
`NodeInfoModule.cpp: if (user.public_key.size > 0) { nodeDB->updateUser(...) }`

La actualizacion se hace sin firma digital ni verificación de origen.

---

## Vulnerabilidad sin CVE — NodeInfo Spoofing (Namechange)

### Estado segun el equipo de Meshtastic
"Los nodos pueden identificarse por su node_id único."

### Por que sigue siendo efectivo

En firmware 2.7.x, el único filtro implementado en el NodeInfoModule es:

```
if (user.is_licensed != owner.is_licensed) { WARN + skip }
```

Una vez que el atacante envía `is_licensed=false` (el valor por defecto de cualquier nodo no licenciado) y `role=CLIENT` (field 7 del User proto), el paquete es aceptado y el nombre del nodo es actualizado en todos los clientes que lo reciban.

**El campo `node_id` no tiene mecanismo de autenticación.** Cualquier dispositivo puede enviar un paquete con cualquier `from`.

La resistencia al ataque que introdujo la validacion de `is_licensed` en 2.6.x fue facilmente superada una vez que se identifico el campo exacto del protobuf (field 6 del User).

---

## Vulnerabilidad sin CVE — Position Spoofing

### Estado segun el equipo de Meshtastic
No hay mitigación documentada en 2.7.x para este vector.

### Por qué sigue siendo efectivo

El PositionModule en firmware 2.7.x acepta cualquier paquete `Position` cuyo timestamp sea mayor al ultimo conocido. El timestamp lo controla el atacante.

PoisonMesh usa `1735689600 + millis()/1000` como timestamp (Unix time base de 2025-01-01 más el uptime del dispositivo), lo que garantiza que el timestamp sea siempre aceptado como mas reciente.

Los campos `sfixed32` para lat/lon (wire type 5, 4 bytes fixed) deben estar correctamente tipados o el firmware los descarta con `wrong wire type`. PoisonMesh implementa la codificacion correcta.

---

## Vulnerabilidad sin CVE — Text Message Spoofing (Mesh Impersonator)

### Estado segun el equipo de Meshtastic
No hay mitigación documentada. El PKI protege mensajes E2E pero no mensajes de canal publico.

### Por qué sigue siendo efectivo

Los mensajes de canal publico (PORTNUM=1) usan cifrado simetrico AES-CTR con la PSK del canal. La PSK es la misma para todos los nodos del canal y es conocida por el atacante (o es la PSK default publica).

El campo `from` del header no esta protegido por el cifrado. El receptor descifra el payload y lo atribuye al nodo identificado por `from` sin verificación adicional.

En firmware 2.7.x con PKI habilitado, los mensajes directos (directed messages) si pueden tener autenticación. Pero los mensajes de broadcast al canal no.

---

## La superficie de ataque persiste

La introduccion del sistema PKI en Meshtastic 2.5+ mejora la seguridad de las comunicaciones punto a punto, pero no resuelve las vulnerabilidades de autenticacion de identidad y posición en el protocolo de difusión.

Esto se debe a una limitacion de la arquitectura: **el protocolo mesh de Meshtastic usa flooding anónimo como mecanismo de propagación**, lo que significa que cualquier nodo con acceso al canal puede inyectar paquetes con cualquier identidad de origen.

Resolver esto completamente requeriría:

1. Firmas digitales en todos los paquetes de NodeInfo y Position
2. Un mecanismo de revocación de claves
3. Un sistema de confianza para claves públicas
4. Mayor overhead en cada paquete LoRa (impacto en duty cycle y latencia)

PoisonMesh demuestra que estos vectores son explotables en condiciones reales de laboratorio con hardware de bajo costo y firmware disponible públicamente.
