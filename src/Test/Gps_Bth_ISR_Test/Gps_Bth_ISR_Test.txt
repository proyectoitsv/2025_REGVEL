/* ===========================================================================
   ESP32_GPS_Optimizado_Flash.ino
   Sistema GPS optimizado para ahorro de memoria Flash
   
   OPTIMIZACIONES:
   - Sistema de logging con niveles y PROGMEM
   - Parser NMEA ligero (sin TinyGPSPlus)
   - Strings en Flash para reducir uso de SRAM
   - Timer hardware corregido para ESP32 v3.3.2
   
   ARQUITECTURA:
   - Core 0: GPS + Bluetooth (Slave) + Timer ISR + Comandos
   - Core 1: Control y procesamiento de mensajes
   - Timer: 1 Hz para lectura GPS sincronizada
   =========================================================================== */

#include <BluetoothSerial.h>
// #include <TinyGPSPlus.h> // ¡ELIMINADO!


// ============================================================================
// CONFIGURACIÓN HARDWARE
// ============================================================================

// Bluetooth HC-06/HC-05
const char BT_NAME[] PROGMEM = "ESP32_GPS_Slave";
const char* HC06_MAC_STR = "00:22:09:01:2C:9E";
const char* HC06_PIN = "1234";

// Timer Hardware
#define TIMER_INTERVAL_US 1000000  // 1 segundo
hw_timer_t *gps_timer = NULL;

// Pines
#define LED_BUILTIN 2

// GPS
#define HDOP_MAX 5.0

// FreeRTOS
#define GPS_TASK_STACK 6144    // Reducido de 8192
#define CONTROL_TASK_STACK 3072 // Reducido de 4096
#define QUEUE_LENGTH 10

// ============================================================================
// ESTRUCTURAS DE DATOS
// ============================================================================

struct ZoneMessage {
  bool inside_zone;
  uint8_t speed_limit;
  float current_speed;
  char zone_name[20];
  bool valid;
  uint32_t timestamp;
};
typedef struct {
  double lat;
  double lon;
  float speed_kmh;
  float course_deg;
  float hdop;
  uint8_t satellites;
  bool valid;
  uint32_t timestamp;
} GpsMinimal;
// ============================================================================
// VARIABLES GLOBALES
// ============================================================================

// Instancias
BluetoothSerial SerialBT;

// Estado del sistema
volatile bool timer_flag = false;
volatile bool bt_connected = false;
volatile unsigned long chars_received = 0;
volatile unsigned long last_bt_data = 0;
volatile bool system_error = false;

// Datos GPS protegidos - INICIALIZACIÓN CORREGIDA
volatile GpsMinimal current_gps = {0.0, 0.0, 0.0, 0.0, 0.0, 0, false, 0};
portMUX_TYPE gps_mux = portMUX_INITIALIZER_UNLOCKED;

// Comunicación FreeRTOS
QueueHandle_t zone_queue = NULL;
SemaphoreHandle_t bt_mutex = NULL;

// Handles de tareas
TaskHandle_t gpsTaskHandle = NULL;
TaskHandle_t controlTaskHandle = NULL;

// Modo test
bool test_mode = false;

// Contadores no bloqueantes
unsigned long last_status_print = 0;
unsigned long last_bt_check = 0;
unsigned long last_led_toggle = 0;
unsigned long last_error_blink = 0;
bool led_state = false;
bool error_led_state = false;

// Control de reinicio
volatile bool restart_requested = false;
unsigned long restart_request_time = 0;

// ============================================================================
// STRINGS EN FLASH (PROGMEM)
// ============================================================================

// Banner del sistema
const char BANNER_LINE1[] PROGMEM = "\n╔════════════════════════════════════════╗";
const char BANNER_LINE2[] PROGMEM = "║   SISTEMA GPS BLUETOOTH OPTIMIZADO    ║";
const char BANNER_LINE3[] PROGMEM = "║   Parser NMEA Ligero + PROGMEM         ║";
const char BANNER_LINE4[] PROGMEM = "║   FreeRTOS Dual Core + Timer HW        ║";
const char BANNER_LINE5[] PROGMEM = "╚════════════════════════════════════════╝";

const char ARCH_LINE1[] PROGMEM = "\nARQUITECTURA:";
const char ARCH_LINE2[] PROGMEM = "  Core 0: GPS + BT + Timer ISR";
const char ARCH_LINE3[] PROGMEM = "  Core 1: Control y procesamiento";
const char ARCH_LINE4[] PROGMEM = "  Timer: 1 Hz (cada 1 seg)";

const char CMD_LINE1[] PROGMEM = "\nCOMANDOS:";
const char CMD_LINE2[] PROGMEM = "  t - Toggle modo test";
const char CMD_LINE3[] PROGMEM = "  s - Estado del sistema";
const char CMD_LINE4[] PROGMEM = "  g - Estado GPS/BT";
const char CMD_LINE5[] PROGMEM = "  r - Reiniciar ESP32";

// Mensajes de error crítico
const char ERR_BT_INIT[] PROGMEM = "Error: Bluetooth no iniciado";
const char ERR_MUTEX[] PROGMEM = "Error: Mutex BT no creado";
const char ERR_QUEUE[] PROGMEM = "Error critico: Queue no creada";
const char ERR_TIMER[] PROGMEM = "Error: Timer no configurado";

// ============================================================================
// PROTOTIPOS
// ============================================================================

void onTimer();
void bt_init_slave();
void bt_check_connection();
bool bt_is_connected();
void gps_process_bluetooth_data();
bool gps_get_fix(GpsMinimal& fix);
void zone_check_position(const GpsMinimal& gps_data);
const char* zone_get_name(bool inside);
void system_init();
void system_show_banner();
void system_process_commands();
void system_handle_error();
void system_handle_restart();
void taskGPS(void *param);
void taskControl(void *param);
bool send_zone_message(const ZoneMessage& msg);

// ============================================================================
// SISTEMA DE LOGGING OPTIMIZADO CON PROGMEM
// ============================================================================

// Niveles de logging
enum LogLevel {
  LOG_ERROR = 0,
  LOG_INFO = 1,
  LOG_DEBUG = 2
};

// CONFIGURACIÓN: Cambiar este valor para controlar el nivel de logging
#define LOG_LEVEL LOG_INFO  // ERROR=0, INFO=1, DEBUG=2

// Strings de prefijos en Flash (PROGMEM)
const char LOG_PREFIX_ERROR[] PROGMEM = "[ERROR] ";
const char LOG_PREFIX_INFO[] PROGMEM = "[INFO] ";
const char LOG_PREFIX_DEBUG[] PROGMEM = "[DEBUG] ";

// Función de logging optimizada
void log_print(LogLevel level, const char* format, ...) {
  if (level > LOG_LEVEL) return;
  
  // Imprimir prefijo desde PROGMEM
  switch(level) {
    case LOG_ERROR: Serial.print(FPSTR(LOG_PREFIX_ERROR)); break;
    case LOG_INFO:  Serial.print(FPSTR(LOG_PREFIX_INFO)); break;
    case LOG_DEBUG: Serial.print(FPSTR(LOG_PREFIX_DEBUG)); break;
    default: break;
  }
  
  // Imprimir mensaje formateado
  char temp[256]; // Buffer temporal para formateo
  va_list args;
  va_start(args, format);
  vsnprintf_P(temp, sizeof(temp), format, args); // _P lee el 'format' desde PROGMEM
  va_end(args);
  Serial.println(temp);
}

// Macros simplificadas
#define LOG_E(format, ...) log_print(LOG_ERROR, format, ##__VA_ARGS__)
#define LOG_I(format, ...) log_print(LOG_INFO, format, ##__VA_ARGS__)
#define LOG_D(format, ...) log_print(LOG_DEBUG, format, ##__VA_ARGS__)

// Strings de mensaje de error/estado en Flash (PROGMEM)
const char ERR_TIMER_FAIL[] PROGMEM = "Fallo al inicializar el Timer HW";

const char ERR_NMEA_CHECKSUM[] PROGMEM = "Error Checksum NMEA";
const char ERR_NMEA_OVERFLOW[] PROGMEM = "Buffer NMEA desbordado";

const char STATUS_GPS_OK[] PROGMEM = "FIX GPS OK: %.6f, %.6f @ %.1f km/h, %.0f sat";
const char STATUS_GPS_WAIT[] PROGMEM = "Esperando fix GPS...";
const char STATUS_GPS_WEAK[] PROGMEM = "Señal GPS débil (HDOP > %.1f)";

const char STATUS_BT_CONNECT[] PROGMEM = "Master conectado";
const char STATUS_BT_DISCONNECT[] PROGMEM = "Master desconectado";

const char STATUS_MEMORY[] PROGMEM = "Memoria libre: %lu bytes | BT: %s"; // CORRECCIÓN: %lu


// ============================================================================
// INTERRUPCIÓN DEL TIMER
// ============================================================================

void onTimer() {
  timer_flag = true;
}

// ============================================================================
// SETUP - CON CORRECCIONES PARA ESP32 v3.3.2
// ============================================================================

void setup() {
  Serial.begin(115200);
  
  // Espera no bloqueante
  unsigned long serial_start = millis();
  while (!Serial && (millis() - serial_start < 500)) {
    // Espera activa
  }
  
  system_init();
  system_show_banner();
  
  bt_init_slave();
  
  // *** CONFIGURACIÓN TIMER CORREGIDA PARA ESP32 v3.3.2 ***
 // *** CONFIGURACIÓN TIMER CORREGIDA PARA ESP32 v3.3.2 ***
  LOG_I("Configurando Timer Hardware...");

  // El '0' es el ID del timer (de 0 a 3)
  gps_timer = timerBegin(0); 
  if (!gps_timer) {
    LOG_E(ERR_TIMER_FAIL);
    system_error = true;
    return;
  }
  
  // Adjuntar la función ISR
  timerAttachInterrupt(gps_timer, &onTimer);
  
  timerAlarm(gps_timer, TIMER_INTERVAL_US, true, 0);
 
  // Habilitar la alarma
  //timerAlarm(gps_timer);

  LOG_I("Timer: 1 Hz configurado");
  
  // Crear tareas FreeRTOS
  LOG_I("Creando tareas FreeRTOS...");
  
  xTaskCreatePinnedToCore(
    taskGPS, "GPS_BT", GPS_TASK_STACK, NULL, 2, &gpsTaskHandle, 0
  );
  
  xTaskCreatePinnedToCore(
    taskControl, "Control", CONTROL_TASK_STACK, NULL, 1, &controlTaskHandle, 1
  );
  
  LOG_I("Sistema iniciado - Esperando BT...");
}


// ============================================================================
// PARSER NMEA LIGERO (REEMPLAZA TinyGPSPlus)
// ============================================================================

#define NMEA_BUFFER_SIZE 128
char nmea_buffer[NMEA_BUFFER_SIZE];
uint8_t nmea_idx = 0;

// Utilidad para extraer campos de una cadena (similar a strtok)
const char* gps_get_field(const char* sentence, uint8_t index, char delimiter) {
  uint8_t current_index = 0;
  for (uint8_t i = 0; sentence[i] != '\0'; i++) {
    if (current_index == index) {
      return &sentence[i];
    }
    if (sentence[i] == delimiter) {
      current_index++;
    }
  }
  return NULL; // Campo no encontrado
}

// Convertir latitud/longitud NMEA a decimal
double gps_convert_coord(const char* field) {
  if (field == NULL || field[0] == '\0') return 0.0;
  
  double degrees;
  double minutes;
  char* dot_pos = (char*)strchr(field, '.');
  
  if (dot_pos == NULL || dot_pos - field < 3) return 0.0;
  
  // Separar grados (DD o DDD) de minutos (MM.MMMM)
  // Grados: longitud - 2 (ej. 3456.78 -> 34) o (12345.67 -> 123)
  uint8_t len_deg = (dot_pos - field) - 2;
  char buf_deg[4];
  strncpy(buf_deg, field, len_deg);
  buf_deg[len_deg] = '\0';
  degrees = atof(buf_deg);
  
  // Minutos (incluye el punto)
  minutes = atof(field + len_deg);
  
  return degrees + (minutes / 60.0);
}

// Procesar NMEA RMC (solo para lat, lon, velocidad, validez)
bool gps_parse_rmc(const char* sentence) {
  if (strncmp(sentence, "$GPRMC", 6) != 0) return false;

  // $GPRMC,HHMMSS.SSS,A,LAT,N/S,LON,E/W,SPD,COG,DDMMYY,MAG,V/E,MODE*CS
  // Campos: 1       2 3 4   5   6   7   8   9   10     11  12  13

  // Campo 2: Validez ('A' = OK, 'V' = Warning)
  const char* validity = gps_get_field(sentence, 2, ',');
  if (validity == NULL || validity[0] != 'A') {
    return false; // No es válido
  }
  
  GpsMinimal temp_data;
  temp_data.valid = true;

  // Campo 3: Latitud (DDMM.MMMM)
  const char* lat_field = gps_get_field(sentence, 3, ',');
  temp_data.lat = gps_convert_coord(lat_field);

  // Campo 4: Indicador N/S
  const char* ns_field = gps_get_field(sentence, 4, ',');
  if (ns_field != NULL && ns_field[0] == 'S') {
    temp_data.lat = -temp_data.lat;
  }

  // Campo 5: Longitud (DDDMM.MMMM)
  const char* lon_field = gps_get_field(sentence, 5, ',');
  temp_data.lon = gps_convert_coord(lon_field);
  
  // Campo 6: Indicador E/W
  const char* ew_field = gps_get_field(sentence, 6, ',');
  if (ew_field != NULL && ew_field[0] == 'W') {
    temp_data.lon = -temp_data.lon;
  }

  // Campo 7: Velocidad (Nudos)
  const char* speed_field = gps_get_field(sentence, 7, ',');
  temp_data.speed_kmh = atof(speed_field) * 1.852; // Nudos a km/h

  // Campo 8: Rumbo (Degrees)
  const char* course_field = gps_get_field(sentence, 8, ',');
  temp_data.course_deg = atof(course_field);
  
  // // HDOP y Satélites (requeriría GSA o GGA, que no estamos parseando, se dejan en 0 o valor por defecto)
  temp_data.hdop = 0.0;
  temp_data.satellites = 0;
  temp_data.timestamp = millis();
  
  // Actualización ISR segura
  // Actualización ISR segura
  taskENTER_CRITICAL(&gps_mux);
  memcpy((void*)&current_gps, &temp_data, sizeof(GpsMinimal));
  taskEXIT_CRITICAL(&gps_mux);

  return true;
}


// Parser para sentencia $GPGGA (Global Positioning System Fix Data)
// Formato: $GPGGA,hhmmss.ss,ddmm.mmmm,N,dddmm.mmmm,E,quality,satellites,hdop,altitude,M,...
bool parse_gpgga(const char* sentence, GpsMinimal& gps_data) {
  char buffer[NMEA_BUFFER_SIZE];
  strncpy(buffer, sentence, NMEA_BUFFER_SIZE - 1);
  buffer[NMEA_BUFFER_SIZE - 1] = '\0';
  
  char* token;
  int field = 0;
  
  char lat_str[16] = {0};
  char lat_hem = 'N';
  char lon_str[16] = {0};
  char lon_hem = 'E';
  int quality = 0;
  
  token = strtok(buffer, ",");
  
  while (token != NULL && field < 8) {
    switch(field) {
      case 0: // $GPGGA
        break;
      case 1: // Tiempo (ignorado)
        break;
      case 2: // Latitud
        strncpy(lat_str, token, sizeof(lat_str) - 1);
        break;
      case 3: // N/S
        lat_hem = token[0];
        break;
      case 4: // Longitud
        strncpy(lon_str, token, sizeof(lon_str) - 1);
        break;
      case 5: // E/W
        lon_hem = token[0];
        break;
      case 6: // Quality (0=invalid, 1=GPS fix, 2=DGPS)
        quality = atoi(token);
        break;
    }
    
    token = strtok(NULL, ",");
    field++;
  }
  
  // Validar datos
  if (quality > 0 && strlen(lat_str) > 0 && strlen(lon_str) > 0) {
    gps_data.lat = gps_convert_coord(lat_str); // <--- CORREGIDO
    if (lat_hem == 'S') {
        gps_data.lat = -gps_data.lat;
    }
    
 gps_data.lon = gps_convert_coord(lon_str); // <--- CORREGIDO
    if (lon_hem == 'W') {
        gps_data.lon = -gps_data.lon;
    }
    // GPGGA no tiene velocidad, mantener la anterior
    gps_data.valid = true;
    gps_data.timestamp = millis();
    return true;
  }
  
  return false;
}

// ============================================================================
// LECTURA GPS (Core 0)
// ============================================================================

// Reemplazo completo de la lógica de TinyGPSPlus
void gps_process_data() {
  // Lee del puerto serial GPS (asumimos Serial2 por el código original)
  while (Serial2.available()) {
    char c = Serial2.read();
    
    // Inicio de nueva sentencia
    if (c == '$') {
      nmea_idx = 0;
    }
    
    if (nmea_idx < NMEA_BUFFER_SIZE - 1) {
      nmea_buffer[nmea_idx++] = c;
      
      // Fin de sentencia (LF o CR)
      if (c == '\n' || c == '\r') {
        nmea_buffer[nmea_idx] = '\0';
        
        // Procesar solo GPRMC
        if (nmea_idx > 6 && strncmp(nmea_buffer, "$GPRMC", 6) == 0) {
          if (gps_parse_rmc(nmea_buffer)) {
            // El parser RMC actualiza current_gps si es válido.
            LOG_D("RMC recibido y parseado.");
          } else {
            LOG_D("RMC no válido (V) o sin fix.");
          }
        }
        nmea_idx = 0; // Reiniciar buffer para la próxima línea
      }
    } else {
      // Buffer overflow
      nmea_idx = 0;
      LOG_E(ERR_NMEA_OVERFLOW);
    }
  }
}

// ============================================================================
// LOOP - NO BLOQUEANTE
// ============================================================================

void loop() {
  if (system_error) {
    system_handle_error();
    return;
  }
  
  if (restart_requested) {
    system_handle_restart();
    return;
  }
  
  // LED de actividad
  if (millis() - last_led_toggle >= 500) {
    last_led_toggle = millis();
    led_state = !led_state;
    digitalWrite(LED_BUILTIN, led_state);
  }
  
  // Verificar BT
  if (millis() - last_bt_check >= 5000) {
    last_bt_check = millis();
    bt_check_connection();
  }
  
  // Reporte periódico - FORMATO CORREGIDO %lu para uint32_t
  if (millis() - last_status_print >= 30000) {
    last_status_print = millis();
    Serial.printf("Sistema: RAM libre=%lu bytes | BT=%s\n",
                  esp_get_free_heap_size(),
                  bt_is_connected() ? "OK" : "NO");
  }
  
  system_process_commands();
  
  vTaskDelay(pdMS_TO_TICKS(10));
}

// ============================================================================
// BLUETOOTH
// ============================================================================

void bt_init_slave() {
  LOG_I("Iniciando Bluetooth SLAVE...");
  
  bt_mutex = xSemaphoreCreateMutex();
  if (bt_mutex == NULL) {
    LOG_E(ERR_MUTEX);
    system_error = true;
    return;
  }
  
  if (!SerialBT.begin(FPSTR(BT_NAME), false)) {
    LOG_E(ERR_BT_INIT);
    system_error = true;
    return;
  }
  
  LOG_I("Bluetooth iniciado: %s", FPSTR(BT_NAME));
  LOG_I("Esperando conexion del Master...");
}

void bt_check_connection() {
  bool connected_now = SerialBT.hasClient();
  
  if (connected_now && !bt_connected) {
    bt_connected = true;
    LOG_I("Bluetooth reconectado");
  } else if (!connected_now && bt_connected) {
    bt_connected = false;
    LOG_I("Bluetooth desconectado");
  }
}

bool bt_is_connected() {
  return SerialBT.hasClient();
}

// ============================================================================
// PROCESAMIENTO GPS CON PARSER LIGERO
// ============================================================================

void gps_process_bluetooth_data() {
  if (!bt_is_connected()) return;
  
  // Lee del puerto serial Bluetooth
  while (SerialBT.available()) {
    char c = SerialBT.read();
    taskENTER_CRITICAL(&gps_mux);
    chars_received = chars_received + 1;
    taskEXIT_CRITICAL(&gps_mux);
    // Inicio de nueva sentencia
    if (c == '$') {
      nmea_idx = 0;
     }
    
    if (nmea_idx < NMEA_BUFFER_SIZE - 1) {
      nmea_buffer[nmea_idx++] = c;
       // Fin de sentencia (LF o CR)
      if (c == '\n' || c == '\r') {
        nmea_buffer[nmea_idx] = '\0';
         // Procesar solo GPRMC
        if (nmea_idx > 6 && strncmp(nmea_buffer, "$GPRMC", 6) == 0) {
          if (gps_parse_rmc(nmea_buffer)) {
            // El parser RMC actualiza current_gps si es válido.
           last_bt_data = millis();
            LOG_D("RMC recibido y parseado.");
          } else {
            LOG_D("RMC no válido (V) o sin fix.");
}
        }
        nmea_idx = 0;
        // Reiniciar buffer para la próxima línea
      }
    } else {
      // Buffer overflow
      nmea_idx = 0;
      LOG_E(ERR_NMEA_OVERFLOW);
    }
  }
}

bool gps_get_fix(GpsMinimal& fix) {
  if (test_mode) {
    static unsigned long last_sim = 0;
    if (millis() - last_sim >= 1000) {
      last_sim = millis();
      
      static float sim_lat = 41.3851;
      static float sim_lon = 2.1734;
      
      sim_lat += (random(-50, 50) / 100000.0);
      sim_lon += (random(-50, 50) / 100000.0);
      
      fix.lat = sim_lat;
      fix.lon = sim_lon;
      fix.speed_kmh = random(20, 80);
      fix.valid = true;
      fix.timestamp = millis();
      return true;
    }
    return false;
  }
  
  // Datos reales
  // Datos reales
  portENTER_CRITICAL(&gps_mux);
  memcpy(&fix, (void*)&current_gps, sizeof(GpsMinimal));
  portEXIT_CRITICAL(&gps_mux);
  
  return fix.valid && (millis() - fix.timestamp < 5000);
}

// ============================================================================
// SISTEMA DE ZONAS
// ============================================================================

void zone_check_position(const GpsMinimal& gps_data) {
  const double ZONA_CENTRO_LAT_MIN = 41.37;
  const double ZONA_CENTRO_LAT_MAX = 41.40;
  const double ZONA_CENTRO_LON_MIN = 2.16;
  const double ZONA_CENTRO_LON_MAX = 2.19;
  
  bool inside = (gps_data.lat >= ZONA_CENTRO_LAT_MIN && 
                 gps_data.lat <= ZONA_CENTRO_LAT_MAX &&
                 gps_data.lon >= ZONA_CENTRO_LON_MIN && 
                 gps_data.lon <= ZONA_CENTRO_LON_MAX);
  
  uint8_t limit = inside ? 30 : 50;
  
  ZoneMessage msg;
  msg.inside_zone = inside;
  msg.speed_limit = limit;
  msg.current_speed = gps_data.speed_kmh;
  strncpy(msg.zone_name, zone_get_name(inside), sizeof(msg.zone_name) - 1);
  msg.valid = true;
  msg.timestamp = millis();
  
  send_zone_message(msg);
  
  LOG_D("Zona: %s | Limite: %d | Vel: %.1f",
        msg.zone_name, limit, gps_data.speed_kmh);
}

const char* zone_get_name(bool inside) {
  return inside ? "CENTRO" : "PERIFERIA";
}

// ============================================================================
// SISTEMA
// ============================================================================

void system_init() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  
  zone_queue = xQueueCreate(QUEUE_LENGTH, sizeof(ZoneMessage));
  if (zone_queue == NULL) {
    LOG_E(ERR_QUEUE);
    system_error = true;
    return;
  }
}

void system_show_banner() {
  Serial.println(FPSTR(BANNER_LINE1));
  Serial.println(FPSTR(BANNER_LINE2));
  Serial.println(FPSTR(BANNER_LINE3));
  Serial.println(FPSTR(BANNER_LINE4));
  Serial.println(FPSTR(BANNER_LINE5));
  
  Serial.println(FPSTR(ARCH_LINE1));
  Serial.println(FPSTR(ARCH_LINE2));
  Serial.println(FPSTR(ARCH_LINE3));
  Serial.println(FPSTR(ARCH_LINE4));
  
  Serial.println(FPSTR(CMD_LINE1));
  Serial.println(FPSTR(CMD_LINE2));
  Serial.println(FPSTR(CMD_LINE3));
  Serial.println(FPSTR(CMD_LINE4));
  Serial.println(FPSTR(CMD_LINE5));
}

void system_handle_error() {
  if (millis() - last_error_blink >= 100) {
    last_error_blink = millis();
    error_led_state = !error_led_state;
    digitalWrite(LED_BUILTIN, error_led_state);
  }
  
  static unsigned long last_error_print = 0;
  if (millis() - last_error_print >= 5000) {
    last_error_print = millis();
    LOG_E("Sistema en estado de error critico");
  }
}

void system_handle_restart() {
  static unsigned long last_blink = 0;
  static int blink_count = 0;
  
  if (millis() - last_blink >= 200) {
    last_blink = millis();
    digitalWrite(LED_BUILTIN, blink_count % 2);
    blink_count++;
  }
  
  if (millis() - restart_request_time >= 2000) {
    LOG_I("Ejecutando reinicio...");
    ESP.restart();
  }
}

void system_process_commands() {
  while (Serial.available()) {
    char cmd = Serial.read();
    
    switch (cmd) {
      case 't':
        test_mode = !test_mode;
        LOG_I("Modo TEST: %s", test_mode ? "ON" : "OFF");
        break;
        
      case 's':
        Serial.printf("\nEstado: TEST=%s | BT=%s | RX=%lu | Queue=%d | RAM=%lu | Uptime=%lu s\n",
                     test_mode ? "ON" : "OFF",
                     bt_is_connected() ? "SI" : "NO",
                     chars_received,
                     uxQueueMessagesWaiting(zone_queue),
                     esp_get_free_heap_size(),  // %lu corregido
                     millis() / 1000);
        break;
        
      case 'g':
        Serial.printf("\nGPS: BT=%s | RX=%lu | Lat=%.6f | Lon=%.6f | Vel=%.1f km/h\n",
                     bt_is_connected() ? "OK" : "NO",
                     chars_received,
                     current_gps.lat,
                     current_gps.lon,
                     current_gps.speed_kmh);
        break;
        
      case 'r':
        LOG_I("Reinicio solicitado (2 seg)");
        restart_requested = true;
        restart_request_time = millis();
        break;
        
      case '\n':
      case '\r':
        break;
        
      default:
        LOG_E("Comando desconocido");
        break;
    }
  }
}

// ============================================================================
// FREERTOS
// ============================================================================

bool send_zone_message(const ZoneMessage& msg) {
  return xQueueSend(zone_queue, &msg, pdMS_TO_TICKS(100)) == pdTRUE;
}

void taskGPS(void *param) {
  LOG_I("[Core 0] Tarea GPS iniciada");
  
  unsigned long last_check = 0;
  while (!bt_is_connected()) {
    if (millis() - last_check >= 2000) {
      last_check = millis();
      if (SerialBT.hasClient()) {
        bt_connected = true;
        LOG_I("Master conectado");
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  
  LOG_I("[Core 0] GPS/BT listo");
  
  GpsMinimal gps_fix;
  unsigned long last_process = 0;
  
  while (true) {
    gps_process_bluetooth_data();
    
    if (timer_flag) {
      timer_flag = false;
      
      if (gps_get_fix(gps_fix)) {
        zone_check_position(gps_fix);
      } else if (bt_is_connected() && !test_mode) {
        if (millis() - last_process >= 5000) {
          last_process = millis();
          LOG_D("Esperando fix GPS...");
        }
      }
    }
    
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void taskControl(void *param) {
  LOG_I("[Core 1] Tarea Control iniciada");
  
  ZoneMessage msg;
  unsigned long last_report = 0;
  
  while (true) {
    if (xQueueReceive(zone_queue, &msg, pdMS_TO_TICKS(200)) == pdTRUE) {
      if (msg.valid) {
        if (msg.current_speed > msg.speed_limit) {
          // Reporte directo sin log_print para ahorro
          Serial.printf("[Control] ALERTA: %.1f > %d km/h en %s\n",
                       msg.current_speed, msg.speed_limit, msg.zone_name);
        }
        
        if (millis() - last_report >= 10000) {
          last_report = millis();
          // Reporte periódico directo
          Serial.printf("[Core 1] Zona=%s | Vel=%.1f/%d km/h\n",
                       msg.zone_name, msg.current_speed, msg.speed_limit);
        }
      }
    }
    
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}