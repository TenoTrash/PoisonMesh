# PoisonMesh by Teno - Descripcion de Ataques

## Introducción

PoisonMesh es una herramienta de pentesting para redes Meshtastic desarrollada para demostrar vulnerabilidades de autenticación en el protocolo LoRa mesh.

Corre en el M5Stack Cardputer ADV con el modulo Cap LoRa-1262 (SX1262).

Todos los ataques explotan la ausencia de autenticación de origen en el protocolo Meshtastic. Los paquetes LoRa no incluyen firmas digitales que permitan verificar que el emisor es quien dice ser.

AVISO LEGAL: Esta herramienta es exclusivamente para pentesting en redes propias o con autorizacion explicita del propietario. Fue desarrollada para responsible disclosure ante el equipo de Meshtastic.

---

## 1. Flood Nodes

### Objetivo
Demostrar que es posible poblar artificialmente una red Meshtastic con nodos inexistentes, degradando la confiabilidad de la lista de nodos y del mapa.

### Como funciona
PoisonMesh genera nodos fantasma con IDs aleatorios, nombres aleatorios y coordenadas GPS aleatorias en cualquier punto del planeta. Cada nodo es enviado como un paquete NodeInfo (PORTNUM=4) valido, correctamente cifrado con la PSK del canal y con todos los campos del protobuf User correctamente tipados.

El usuario configura la cantidad de nodos a enviar (1-1000) y la duración del ataque. El firmware calcula automaticamente el tiempo mínimo necesario según el preset LoRa seleccionado para garantizar que todos los paquetes sean transmitidos sin colisiones.

Cada nodo generado usa un modelo de hardware distinto (TBEAM, HELTEC, RAK4631, etc.) para que la inyección sea visualmente variada en los clientes.

### Impacto observable
- La lista de nodos en la app Meshtastic crece con decenas o cientos de entradas en segundos
- Los nodos reales quedan enterrados entre el ruido

### Parametros configurables
- Cantidad de nodos (default: 50)
- Duración en segundos (mínimo calculado automaticamente por preset)

---

## 2. MeshGrow

### Objetivo
Demostrar un ataque de flood continuo y silencioso que crece en el tiempo, simulando como una red podría ser gradualmente comprometida sin una intervención visible e inmediata.

### Cómo funciona
MeshGrow es un ataque de toggle ON/OFF. Cuando esta activo, inyecta un nuevo nodo fantasma cada 10 segundos, con un ID, nombre y coordenadas GPS aleatorios.
Cada nodo generado incluye tanto un paquete NodeInfo como un paquete Position, por lo que aparece en la lista Y en el mapa desde el primer momento.

A diferencia del Flood Nodes, el ritmo pausado de MeshGrow hace que el crecimiento parezca orgánico, lo que dificulta detectar el ataque en tiempo real.

El ataque puede detenerse y reanudarse sin perder el contador de nodos enviados.

### Impacto observable
- La red crece lentamente pero de forma continua
- Cada 10 segundos aparece un nodo nuevo en el mapa
- El mapa se va poblando globalmente de forma progresiva

### Parametros configurables
- Toggle ON/OFF con Enter
- Intervalo fijo de 10 segundos (hardcodeado para la demo)

---

## 3. Namechange

### Objetivo
Demostrar que cualquier nodo de la red puede ser renombrado sin autorización, comprometiendo la identidad de los participantes.

### Funcionamiento
El usuario selecciona un nodo existente de la lista del sniffer (nodos reales descubiertos en la red), ingresa un nombre largo y un nombre corto arbitrarios, y selecciona el modelo de hardware que se reportara.

PoisonMesh envia repetidamente paquetes NodeInfo con el node_id de la victima como campo `from`, pero con los nuevos nombres en el payload. Cada nodo de la red que recibe estos paquetes actualiza su base de datos local con los datos del atacante.

El modelo de hardware seleccionado cambia el icono del nodo en la app Meshtastic, lo que permite hacer el cambio más o menos discreto.

### Impacto observable
- El nodo víctima aparece con el nombre nuevo en todos los clientes
- El icono del nodo puede cambiar si se selecciona un hardware diferente
- El cambio persiste hasta que el nodo real re-anuncie su NodeInfo (generalmente cada 15-30 minutos)

### Parametros configurables
- Nodo víctima (seleccionado de la lista del sniffer)
- Nombre largo (hasta 39 caracteres)
- Nombre corto (hasta 4 caracteres)
- Modelo de hardware (lista de 12 opciones)
- Duración del ataque en segundos

---

## 4. Position Poison

### Objetivo
Demostrar que la ubicación GPS de cualquier nodo puede ser falsificada, con implicaciones criticas para operaciones que dependen de la geolocalización.

### Como funciona
El usuario selecciona un nodo víctima de la lista del sniffer y elige un destino predefinido de la lista de ubicaciones. PoisonMesh envia repetidamente paquetes Position (PORTNUM=3) con el node_id de la victima como `from` y las coordenadas del destino en el payload.

Los campos del protobuf Position usan tipos `sfixed32` para latitud y longitud (wire type 5, 4 bytes fixed) y `fixed32` para el timestamp. El timestamp se genera como `1735689600 + uptime_seconds`, lo que garantiza que sea siempre mayor al ultimo timestamp conocido del nodo y sea aceptado por el firmware receptor.

### Destinos disponibles
- Islas Malvinas (-51.79 / -59.52)
- Piramides de Egipto (+29.97 / +31.13)
- Australia centro (-25.27 / +133.77)
- Roma, Italia (+41.89 / +12.49)

### Impacto observable
- El nodo víctima desaparece de su ubicacion real en el mapa
- Aparece un pin en la ubicación falsa con el nombre del nodo real
- El cambio persiste mientras el ataque esta activo y compite con los paquetes de posicion reales del nodo (si tiene GPS activo)

### Parametros configurables
- Nodo víctima (seleccionado de la lista del sniffer)
- Destino (lista de 4 opciones)
- Duración del ataque en segundos

---

## 5. Mesh Move

### Objetivo
Demostrar un ataque automatizado que reubica progresivamente todos los nodos descubiertos en la red, combinando Position Poison con Namechange de forma secuencial, sin intervencion manual del atacante.

### Como funciona
Mesh Move es un ataque pasivo/activo. Cuando esta activo, el radio permanece en modo RX escuchando la red. Por cada nodo que descubre, espera 1 minuto y luego, cada 30 segundos, envia dos paquetes para ese nodo:

1. Un paquete NodeInfo con nombre largo "Moved" y nombre corto "MOV", indicando visualmente que el nodo fue comprometido.
2. Un paquete Position con las coordenadas del destino actual (rotativo entre Malvinas, Egipto, Australia y Roma).

El ataque itera ciclicamente por todos los nodos del NodeDB. Si hay 10 nodos, los ataca de a uno, cada 30 segundos, rotando entre los 4 destinos disponibles.

El radio vuelve a RX despues de cada TX, permitiendo que el sniffer siga descubriendo nuevos nodos durante el ataque.

### Impacto observable
- En el mapa aparecen todos los nodos renombrados como "Moved" y dispersos por el mundo
- La pantalla muestra que nodo se esta atacando en este momento y hacia que destino
- El contador de ataques enviados crece en tiempo real

### Parametros configurables
- Toggle ON/OFF con Enter
- El timing (1 minuto de warmup, 30 segundos entre ataques) es fijo para la demo

---

## 6. Mesh Impersonator

### Objetivo
Demostrar que cualquier nodo puede enviar mensajes de texto usando la identidad de otro participante de la red, sin posibilidad de verificación por parte del receptor.

### Como funciona
El usuario selecciona un nodo víctima de la lista del sniffer y elige uno de los cuatro mensajes de prueba disponibles. PoisonMesh construye un paquete de texto plano (PORTNUM=1) con el node_id de la victima como `from` y el mensaje en el payload.

A diferencia de los otros ataques, Mesh Impersonator no usa un loop de repetición: envia el mensaje exactamente una vez y muestra el resultado. Esto es intencional para que la demostracion sea controlada y trazable.

El payload de PORTNUM=1 es texto UTF-8 plano (sin protobuf), cifrado con la PSK del canal. El receptor descifra el texto y lo atribuye al nodo identificado por el campo `from` del header.

### Mensajes disponibles
1. Técnico: "[TEST] PoisonMesh by Teno | Este mensaje es una prueba de seguridad"
2. Demo: "[DEMO] Este mensaje fue enviado por PoisonMesh, NO por este nodo"
3. CVE: "[CVE-DEMO] Meshtastic permite spoofing de mensajes | PoisonMesh by Teno"
4. Impacto: "[POISONMESH] Spoofing activo | Este nodo fue suplantado | by Teno"

Todos los mensajes incluyen identificacion explicita de la herramienta para que la demostracion sea trazable y no genere confusion en los participantes.

### Impacto observable
- El mensaje aparece en la app Meshtastic con el nombre, icono y node_id del nodo seleccionado como victima
- El mensaje es indistinguible de un mensaje enviado por el nodo real

### Parametros configurables
- Nodo victima (seleccionado de la lista del sniffer)
- Mensaje (seleccionado de la lista de 4 opciones)

---

## 7. PKI Poison

### Objetivo
Demostrar la vulnerabilidad CVE-2025-55293: la posibilidad de corromper la clave pública de un nodo en el NodeDB de los clientes cercanos, generando alertas de seguridad y degradando el cifrado punto a punto.

### Funcionamiento
PoisonMesh envía un paquete NodeInfo con el node_id de la victima como `from` y una clave publica vacia (32 bytes en cero) en el campo `public_key` (field 8 del User proto).

Los clientes Meshtastic que reciben este paquete actualizan su registro del nodo victima con la clave vacía, marcandolo como "sin clave pública" o "clave comprometida". En la UI de los clientes, esto se muestra como un candado rojo o una advertencia de seguridad junto al nombre del nodo.

Adicionalmente, el cifrado punto a punto hacia ese nodo queda deshabilitado hasta que el nodo real vuelva a anunciar su NodeInfo con la clave correcta.

### Impacto observable
- El nodo víctima aparece con alerta visual de seguridad (icono rojo) en todos los clientes que reciban el paquete
- El cifrado E2E queda degradado para ese nodo
- El efecto persiste hasta el próximo anuncio de NodeInfo del nodo real

### Parametros configurables
- Nodo víctima (seleccionado de la lista del sniffer)
- Duracion del ataque en segundos

---

## Sniffer pasivo (funcion de base)

### Objetivo
No es un ataque en si mismo, sino la función que habilita todos los ataques dirigidos (Namechange, Position Poison, Mesh Move, Impersonator, PKI Poison).

### Arquitectura interna del sniffer
El radio SX1262 permanece en modo RX continuo mientras el menu principal esta visible. Cada paquete recibido es descifrado con la PSK del canal y parseado. Los campos `from`, `long_name`, `short_name`, `public_key`, `lat`, `lon` y `rssi` se almacenan en el NodeDB interno (hasta 32 nodos).

El menú principal muestra en tiempo real cuantos nodos hay en la base de datos. Los ataques dirigidos presentan esta lista al usuario para seleccionar el objetivo.

### Informacion visible por nodo
- Nombre largo
- RSSI (indicador de calidad de senal, en color: verde/amarillo/rojo)
- Número de nodo en la lista / total

---

## Control de pantalla

El botón G0 (boton fisico lateral del Cardputer ADV) actua como interruptor de la pantalla en cualquier momento de la ejecucion, incluyendo durante los ataques. Esto permite apagar el display para conservar bateria durante periodos de espera sin interrumpir los ataques en curso.
