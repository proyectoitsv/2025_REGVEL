/*
  Modulo3_GPS_Bluetooth_Map.ino
  Módulo de prueba (Core 0) - GPS (NMEA) + Bluetooth SPP + Mapa CSV + decisión de límite + comunicación entre cores

  Requisitos y características:
  - Funciones públicas y firmas compatibles con proyecto: gps_init(), gps_process_serial(), map_load_from_spiffs(),
    map_find_nearest_zone(), decide_speed_limit_and_send(), bluetooth_init(), intercore_send_msg()
  - Autocontenible: puede compilarse y probarse solo. Si no hay GPS físico, acepta NMEA por monitor serie.
  - Incluye implementación mínima de parseo NMEA (GPRMC / GPGGA) para obtener lat/lon y fix.
  - Mapas: carga desde SPIFFS (si listado disponible) o usa un mapa embebido de ejemplo (array de zonas).
  - Comunicación inter-core: stub intercore_send_msg() que imprime por Serial o envía por Serial1 a otro ESP; en producción reemplazar por cola FreeRTOS.
  - Bluetooth: inicializa Bluetooth Classic SPP (BluetoothSerial). Permite enviar información de posición y recibir comandos simples.
  - Ejemplos de comandos por Serial (monitor):
      - "GPS <raw_nmea_line>" -> inyecta NMEA para debug
      - "LISTMAP" -> imprime mapa cargado
      - "LOADMAP" -> intenta cargar mapa desde SPIFFS
      - "TESTSEND" -> fuerza decisión y envío de mensaje inter-core
  - NOTA: Este sketch asume que el GPS está en Serial1 (TX1/RX1). Ajustar pins y baud si necesitás otro puerto.
*/

#include <Arduino.h>
#include "BluetoothSerial.h"
#include "SPIFFS.h"    // opcional: para cargar mapa CSV desde filesystem

// --------------------------- Configuración de pines / puertos ---------------------------
#define GPS_SERIAL Serial1
#define GPS_RX_PIN 16    // RX1 pin (a TX del módulo GPS)
#define GPS_TX_PIN 17    // TX1 pin (a RX del módulo GPS)
#define GPS_BAUD 9600

BluetoothSerial SerialBT; // Bluetooth SPP

// --------------------------- Tipos y estructuras ---------------------------
struct GPSFix {
  bool valid;
  double lat;    // grados decimales (positivo norte)
  double lon;    // grados decimales (positivo este)
  float speed_knots; // si disponible
  unsigned long timestamp_ms;
};

struct MapZone {
  // Zona simple definida como rectángulo geográfico para pruebas (lat_min, lat_max, lon_min, lon_max)
  // En aplicación real usar polígonos o radios circulares.
  double lat_min;
  double lat_max;
  double lon_min;
  double lon_max;
  uint16_t speed_limit_kmh; // límite de velocidad en la zona
  const char *name;         // nombre de la vía/zona
};

// --------------------------- Mapa embebido de ejemplo (fallback) ---------------------------
static MapZone embedded_map[] = {
  // lat_min, lat_max, lon_min, lon_max, limit, "nombre"
  { -34.6100, -34.6000, -58.4200, -58.4100, 60, "Av. Ejemplo 1" },
  { -34.6200, -34.6150, -58.4300, -58.4250, 40, "Calle Prueba" },
  { -34.6300, -34.6250, -58.4400, -58.4350, 80, "Ruta Demo" }
};
static const size_t embedded_map_size = sizeof(embedded_map) / sizeof(embedded_map[0]);

// Mapa cargado (puede apuntar a embedded_map o a datos cargados desde SPIFFS)
static MapZone *map_zones = embedded_map;
static size_t map_zones_count = embedded_map_size;

// --------------------------- Inter-core messaging (stub) ---------------------------
struct InterCoreMsg {
  bool inside_zone;
  uint16_t speed_limit;
  bool valid;
};

// En producción: reemplazar por xQueueSend() o intercore_send() que use la cola FreeRTOS
void intercore_send_msg_stub(const InterCoreMsg &msg) {
  // 1) Impresión para debug
  Serial.printf(">> intercore_send_msg_stub: inside=%s limit=%u valid=%s\n",
                msg.inside_zone ? "DENTRO" : "FUERA", msg.speed_limit, msg.valid ? "TRUE":"FALSE");
  // 2) También lo enviamos por Bluetooth para debug remoto
  if (SerialBT.hasClient()) {
    char buf[64];
    snprintf(buf, sizeof(buf), "ICMSG inside=%s limit=%u\n",
             msg.inside_zone ? "1" : "0", msg.speed_limit);
    SerialBT.print(buf);
  }
  // 3) (opcional) enviar a Serial1 para test con otro microcontrolador
  // GPS_SERIAL.println("ICMSG:" + String(msg.inside_zone ? "1" : "0") + "," + String(msg.speed_limit));
}

// Wrapper público usado por el resto del sistema (misma firma)
void intercore_send_msg(const InterCoreMsg &msg) {
  intercore_send_msg_stub(msg);
}

// --------------------------- Funciones Bluetooth ---------------------------
/**
 * @brief Inicializa Bluetooth SPP (nombre "RegVel_GPS")
 * En el sistema principal, Bluetooth se usa para debugging y para enviar datos / recibir comandos.
 */
void bluetooth_init() {
  if (!SerialBT.begin("RegVel_GPS")) {
    Serial.println("ERROR: Bluetooth no pudo inicializarse.");
  } else {
    Serial.println("Bluetooth inicializado (RegVel_GPS)");
  }
}

/**
 * @brief Procesa comandos recibidos por Bluetooth (similares a los del monitor serie)
 */
void bluetooth_handle_rx() {
  if (!SerialBT.available()) return;
  String line = SerialBT.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;

  Serial.printf("[BT] comando: %s\n", line.c_str());

  // Por simplicidad reutilizamos el parser de comandos del Serial monitor (ver abajo)
  // Llamamos a la misma función que maneja Serial para mantener comportamiento idéntico.
  // Convertimos a mayúsculas donde convenga o separamos tokens.
  // Aquí simplemente reenviamos al Serial local para manejarlo en el mismo parser:
  Serial.println(line);
}

// --------------------------- Funciones GPS (NMEA) ---------------------------
/**
 * @brief Convierte un campo NMEA (lat o lon) en formato ddmm.mmmm + hemi a grados decimales
 * ejemplo: "3412.3456", 'S' -> -34.20576
 */
double nmea_to_decimal_degrees(const String &field, char hemi) {
  if (field.length() < 4) return 0.0;
  // ddmm.mmmm (lat) o dddmm.mmmm (lon). Encontrar punto
  int dotpos = field.indexOf('.');
  int deg_digits = (dotpos == -1) ? 2 : (dotpos > 4 ? 3 : 2); // crude rule
  // Mejor: lat -> 2 deg digits, lon -> 3 -> detectar por longitud
  // Si la longitud total > 7 asumimos lon (3 dígitos)
  if (field.length() > 7) deg_digits = 3;
  String degs = field.substring(0, deg_digits);
  String mins = field.substring(deg_digits);
  double d = degs.toDouble();
  double m = mins.toDouble();
  double dec = d + (m / 60.0);
  if (hemi == 'S' || hemi == 'W') dec = -dec;
  return dec;
}

/**
 * @brief Parsea una línea NMEA y actualiza un GPSFix (sólo GPRMC y GPGGA relevantes)
 * - Mantiene firma similar a gps_parse_nmea(String)
 */
GPSFix gps_parse_nmea(const String &nmea_line) {
  GPSFix fix;
  fix.valid = false;
  fix.lat = 0.0;
  fix.lon = 0.0;
  fix.speed_knots = 0.0;
  fix.timestamp_ms = millis();

  String line = nmea_line;
  line.trim();
  if (line.length() == 0) return fix;

  // Validar inicio $
  if (line[0] != '$') return fix;

  // Quitar checksum si existe
  int asterisk = line.indexOf('*');
  String payload = (asterisk > 0) ? line.substring(1, asterisk) : line.substring(1);
  // Tokenizar por comma
  int idx = payload.indexOf(',');
  String sentence = (idx > 0) ? payload.substring(0, idx) : payload;

  // GPRMC -> recommended minimum
  if (payload.startsWith("GPRMC")) {
    // Formato: GPRMC,hhmmss.sss,A,llll.ll,a,yyyyy.yy,a,x.x,xxx.x,ddmmyy,magvar,E*cs
    // Campos: 1=time, 2=status A/V, 3=lat,4=N/S,5=lon,6/E/W,7=sog(knots), ...
    // Splitting
    std::vector<String> toks;
    int start = 0;
    for (int i = 0; i <= (int)payload.length(); ++i) {
      if (i == (int)payload.length() || payload[i] == ',') {
        toks.push_back(payload.substring(start, i));
        start = i + 1;
      }
    }
    if (toks.size() >= 8) {
      String status = toks[2];
      if (status == "A") {
        String latf = toks[3];
        char latH = toks[4].length() > 0 ? toks[4][0] : 'N';
        String lonf = toks[5];
        char lonH = toks[6].length() > 0 ? toks[6][0] : 'E';
        String sog = toks[7];

        double latd = nmea_to_decimal_degrees(latf, latH);
        double lond = nmea_to_decimal_degrees(lonf, lonH);
        fix.lat = latd;
        fix.lon = lond;
        fix.valid = true;
        fix.speed_knots = sog.toFloat();
        fix.timestamp_ms = millis();
      }
    }
    return fix;
  }

  // GPGGA -> provides lat/lon even without RMC
  if (payload.startsWith("GPGGA")) {
    // GPGGA,hhmmss.ss,llll.ll,a,yyyyy.yy,a,quality,numSV,HDOP,alt,units,sep,units,dgpsAge,dgpsId
    std::vector<String> toks;
    int start = 0;
    for (int i = 0; i <= (int)payload.length(); ++i) {
      if (i == (int)payload.length() || payload[i] == ',') {
        toks.push_back(payload.substring(start, i));
        start = i + 1;
      }
    }
    if (toks.size() >= 6) {
      String latf = toks[2];
      char latH = toks[3].length() > 0 ? toks[3][0] : 'N';
      String lonf = toks[4];
      char lonH = toks[5].length() > 0 ? toks[5][0] : 'E';
      if (latf.length() > 0 && lonf.length() > 0) {
        fix.lat = nmea_to_decimal_degrees(latf, latH);
        fix.lon = nmea_to_decimal_degrees(lonf, lonH);
        fix.valid = true;
        fix.timestamp_ms = millis();
      }
    }
    return fix;
  }

  return fix;
}

/**
 * @brief Inicializa el puerto serie del GPS (Serial1).
 * Firma: gps_init()
 */
void gps_init(uint32_t baud = GPS_BAUD, int rxPin = GPS_RX_PIN, int txPin = GPS_TX_PIN) {
  GPS_SERIAL.begin(baud, SERIAL_8N1, rxPin, txPin);
  Serial.printf("gps_init(): Serial1 iniciado en %u baudios (RX=%d TX=%d)\n", baud, rxPin, txPin);
}

/**
 * @brief Procesa líneas NMEA disponibles del GPS (no bloqueante).
 * - Llama a gps_parse_nmea() por cada línea.
 * - Mantiene la última posición válida en memoria (last_fix).
 */
static GPSFix last_fix;
void gps_process_serial() {
  static String line = "";
  while (GPS_SERIAL.available()) {
    char c = GPS_SERIAL.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (line.length() > 0) {
        GPSFix fx = gps_parse_nmea(line);
        if (fx.valid) {
          last_fix = fx;
          Serial.printf("GPS fix: lat=%.6f lon=%.6f speed_kn=%.2f\n", fx.lat, fx.lon, fx.speed_knots);
        } else {
          // No válido pero log para debug si hace falta
          // Serial.printf("NMEA parse no válido: %s\n", line.c_str());
        }
      }
      line = "";
    } else {
      line += c;
      // proteger contra líneas muy largas
      if (line.length() > 200) line = "";
    }
  }
}

/**
 * @brief Permite inyectar NMEA por monitor serie para simular un GPS.
 * Comando Serial: "GPS <raw_nmea_line>"
 */
void gps_inject_line(const String &nmea_line) {
  GPSFix fx = gps_parse_nmea(nmea_line);
  if (fx.valid) {
    last_fix = fx;
    Serial.printf("Injected GPS fix: lat=%.6f lon=%.6f\n", fx.lat, fx.lon);
  } else {
    Serial.println("Injected NMEA no válido o sin fix.");
  }
}

// --------------------------- Funciones mapa (CSV) ---------------------------
/**
 * @brief Intenta cargar mapa desde SPIFFS en /map.csv (formato: lat_min,lat_max,lon_min,lon_max,limit,name)
 * Si no hay SPIFFS o falla, deja el mapa apuntando al embedded_map.
 *
 * Firma: map_load_from_spiffs()
 */
bool map_load_from_spiffs(const char *path = "/map.csv") {
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS no disponible; usando mapa embebido.");
    map_zones = embedded_map;
    map_zones_count = embedded_map_size;
    return false;
  }
  if (!SPIFFS.exists(path)) {
    Serial.println("map.csv no encontrado en SPIFFS; usando mapa embebido.");
    map_zones = embedded_map;
    map_zones_count = embedded_map_size;
    return false;
  }

  File f = SPIFFS.open(path, FILE_READ);
  if (!f) {
    Serial.println("No se pudo abrir map.csv; usando mapa embebido.");
    map_zones = embedded_map;
    map_zones_count = embedded_map_size;
    return false;
  }

  // Para simplicidad cargamos en un buffer dinámico de MapZone (limitado a N líneas)
  const size_t MAX_LINES = 64;
  static MapZone parsed[MAX_LINES];
  size_t parsed_count = 0;

  while (f.available() && parsed_count < MAX_LINES) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    // formato CSV simple: lat_min,lat_max,lon_min,lon_max,limit,name
    std::vector<String> toks;
    int start = 0;
    for (int i = 0; i <= (int)line.length(); ++i) {
      if (i == (int)line.length() || line[i] == ',') {
        toks.push_back(line.substring(start, i));
        start = i + 1;
      }
    }
    if (toks.size() >= 6) {
      parsed[parsed_count].lat_min = toks[0].toDouble();
      parsed[parsed_count].lat_max = toks[1].toDouble();
      parsed[parsed_count].lon_min = toks[2].toDouble();
      parsed[parsed_count].lon_max = toks[3].toDouble();
      parsed[parsed_count].speed_limit_kmh = (uint16_t) toks[4].toInt();
      // NOTE: nombre: en este ejemplo no guardamos string dinámico; apuntamos a c_str de String (no ideal)
      // Para pruebas esto alcanza; en producción guardá en vector<string> o estructura gestionada.
      parsed[parsed_count].name = strdup(toks[5].c_str());
      parsed_count++;
    }
  }
  f.close();

  if (parsed_count == 0) {
    Serial.println("map.csv vacío o sin líneas válidas; usando mapa embebido.");
    map_zones = embedded_map;
    map_zones_count = embedded_map_size;
    return false;
  }

  // Apuntar a parsed buffer (vive en memoria estática)
  map_zones = parsed;
  map_zones_count = parsed_count;
  Serial.printf("Mapa cargado desde SPIFFS con %u zonas.\n", (unsigned)parsed_count);
  return true;
}

/**
 * @brief Imprime zonas del mapa cargado (para debug)
 */
void map_print_zones() {
  Serial.printf("Map zones count = %u\n", (unsigned)map_zones_count);
  for (size_t i = 0; i < map_zones_count; ++i) {
    MapZone &z = map_zones[i];
    Serial.printf("Zone %u: lat[%.6f..%.6f] lon[%.6f..%.6f] limit=%u name=%s\n",
                  (unsigned)i, z.lat_min, z.lat_max, z.lon_min, z.lon_max, z.speed_limit_kmh, z.name);
  }
}

/**
 * @brief Busca si la coordenada (lat,lon) está dentro de alguna zona del mapa.
 * Retorna true y el índice de la zona encontrada, o false si no hay coincidencia.
 * Firma: map_find_zone_for_coord(lat, lon, &out_zone_index)
 */
bool map_find_zone_for_coord(double lat, double lon, size_t *out_zone_index) {
  for (size_t i = 0; i < map_zones_count; ++i) {
    MapZone &z = map_zones[i];
    if (lat >= z.lat_min && lat <= z.lat_max && lon >= z.lon_min && lon <= z.lon_max) {
      if (out_zone_index) *out_zone_index = i;
      return true;
    }
  }
  return false;
}

// --------------------------- Lógica de decisión y envío a Core 1 ---------------------------
/**
 * @brief Decide si estamos "inside_zone" y cuál es el speed_limit, basándose en la última posición GPS.
 * - Si dentro de una zona del mapa: inside = true, speed_limit = zone.limit
 * - Si no: inside = false, speed_limit = 0
 * - Envía mensaje inter-core con intercore_send_msg()
 *
 * Firma: decide_speed_limit_and_send()
 */
void decide_speed_limit_and_send() {
  InterCoreMsg msg;
  msg.valid = false;
  msg.inside_zone = false;
  msg.speed_limit = 0;

  if (!last_fix.valid) {
    Serial.println("decide_speed_limit_and_send(): sin fix GPS valido.");
    // opcional: enviar msg fuera de zona
    msg.valid = true;
    msg.inside_zone = false;
    msg.speed_limit = 0;
    intercore_send_msg(msg);
    return;
  }

  size_t idx;
  if (map_find_zone_for_coord(last_fix.lat, last_fix.lon, &idx)) {
    msg.valid = true;
    msg.inside_zone = true;
    msg.speed_limit = map_zones[idx].speed_limit_kmh;
    Serial.printf("Decisión: DENTRO zona '%s' limit=%u km/h\n", map_zones[idx].name, msg.speed_limit);
  } else {
    msg.valid = true;
    msg.inside_zone = false;
    msg.speed_limit = 0;
    Serial.println("Decisión: FUERA de zonas del mapa.");
  }

  intercore_send_msg(msg);
}

// --------------------------- Manejo de comandos por Serial (monitor) ---------------------------
/*
 Comandos disponibles (escribe en monitor serie y presiona Enter):
  - GPS <nmea_line>     -> inyecta NMEA string para simular GPS
  - LOADMAP             -> intenta cargar /map.csv desde SPIFFS
  - LISTMAP             -> imprime las zonas cargadas
  - TESTSEND            -> fuerza decidir y enviar mensaje inter-core (usa last_fix)
  - HELP                -> lista comandos
*/
void handle_serial_commands_module3(const String &line_raw) {
  String line = line_raw;
  line.trim();
  if (line.length() == 0) return;

  if (line.startsWith("GPS ")) {
    String payload = line.substring(4);
    gps_inject_line(payload);
    return;
  }
  if (line.equalsIgnoreCase("LOADMAP")) {
    map_load_from_spiffs("/map.csv");
    return;
  }
  if (line.equalsIgnoreCase("LISTMAP")) {
    map_print_zones();
    return;
  }
  if (line.equalsIgnoreCase("TESTSEND")) {
    decide_speed_limit_and_send();
    return;
  }
  if (line.equalsIgnoreCase("HELP")) {
    Serial.println("Comandos: GPS <line>, LOADMAP, LISTMAP, TESTSEND, HELP");
    return;
  }
  Serial.printf("Comando desconocido: %s\n", line.c_str());
}

// --------------------------- Setup / Loop ---------------------------
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("=== Module3 GPS+Bluetooth+Map (Core0) - prueba ===");

  // Inicializaciones
  bluetooth_init();
  gps_init(GPS_BAUD, GPS_RX_PIN, GPS_TX_PIN);

  // Intentar cargar mapa desde SPIFFS; si falla, usar embedded_map (map_load_from_spiffs lo maneja)
  map_load_from_spiffs("/map.csv");

  // estado inicial
  last_fix.valid = false;

  Serial.println("Listo. Enviar 'HELP' para ver comandos.");
}

void loop() {
  // 1) Procesar comandos recibidos por Bluetooth (reenvían a Serial para manejo unificado)
  bluetooth_handle_rx();

  // 2) Procesar datos del GPS (Serial1) si hay
  gps_process_serial();

  // 3) Comprobar input desde el monitor serie (Serial) y ejecutar comandos
  while (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    handle_serial_commands_module3(line);
  }

  // 4) Política de decisión periódica (cada 1s por ejemplo)
  static unsigned long last_decision_ms = 0;
  unsigned long now = millis();
  if (now - last_decision_ms >= 1000) {
    last_decision_ms = now;
    decide_speed_limit_and_send();
  }

  // pequeña espera no bloqueante
  delay(10);
}
