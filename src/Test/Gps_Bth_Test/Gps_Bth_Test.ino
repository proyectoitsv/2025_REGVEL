/* ===========================================================================
   SISTEMA_BLUETOOTH_INALAMBRICO.ino - ESP32 + HC-06 (Sin cables)
   Comunicación totalmente inalámbrica por Bluetooth
   =========================================================================== */

#include <BluetoothSerial.h>
#include <TinyGPSPlus.h>

// ============================================================================
// CONFIGURACIÓN BLUETOOTH INALÁMBRICO
// ============================================================================

// Configuración Bluetooth - TOTALMENTE INALÁMBRICO
const char* BT_DEVICE_NAME = "ESP32_GPS_Mapa";
const char* HC06_MAC_STR = "00:22:09:01:2C:9E";  // REEMPLAZA con MAC real del HC-06
const char* HC06_PIN = "1234";
uint8_t HC06_MAC[6];

// Configuración GPS
#define HDOP_MAX 5.0

// Configuración FreeRTOS
#define GPS_TASK_STACK_SIZE 8192
#define MOTOR_TASK_STACK_SIZE 4096
#define TASK_PRIORITY 1

// Pines
#define LED_BUILTIN 2

// ============================================================================
// ESTRUCTURAS
// ============================================================================

struct GpsFix {
  double lat;
  double lon;
  float speedKph;
  float courseDeg;
  float hdop;
};

struct InterCoreMsg {
  bool inside_zone;
  uint8_t speed_limit;
  bool valid;
};

// ============================================================================
// VARIABLES GLOBALES
// ============================================================================

// Instancias
TinyGPSPlus gps;
BluetoothSerial SerialBT;

// Estado del sistema
static unsigned long last_data_time = 0;
static unsigned long chars_received = 0;
bool bt_connected = false;
bool test_mode = true;
float current_speed_kmh = 0.0;

// Tareas FreeRTOS
TaskHandle_t gpsTaskHandle = NULL;
TaskHandle_t motorTaskHandle = NULL;

// Comunicación entre núcleos
QueueHandle_t intercore_queue;

// ============================================================================
// PROTOTIPOS BLUETOOTH INALÁMBRICO
// ============================================================================

void bt_inalambrico_init();
void bt_esperar_conexion();
bool bt_conectar_hc06();
void bt_verificar_conexion();

// GPS
void gps_procesar_datos();
bool gps_obtener_posicion(GpsFix& fix);
void gps_mostrar_estado();

// Mapa
void mapa_procesar_posicion(const GpsFix& fix);
void mapa_enviar_control(bool dentro_zona, uint8_t limite_velocidad);

// Sistema
void sistema_init();
void sistema_mostrar_inicio();
void sistema_procesar_comandos();

// FreeRTOS
bool intercore_init();
bool intercore_enviar_msg(const InterCoreMsg& msg);
bool intercore_recibir_msg(InterCoreMsg& msg, TickType_t timeout);

// Tareas
void tareaGPS(void *pvParameters);
void tareaMotor(void *pvParameters);

// ============================================================================
// SETUP PRINCIPAL - BLUETOOTH INALÁMBRICO
// ============================================================================

void setup() {
  Serial.begin(115200);
  sistema_init();
  
  // Crear tareas FreeRTOS
  xTaskCreatePinnedToCore(
    tareaGPS, "TareaGPS", GPS_TASK_STACK_SIZE, NULL, TASK_PRIORITY, &gpsTaskHandle, 0
  );
  
  xTaskCreatePinnedToCore(
    tareaMotor, "TareaMotor", MOTOR_TASK_STACK_SIZE, NULL, TASK_PRIORITY, &motorTaskHandle, 1
  );
  
  Serial.println("\n✅ Sistema Bluetooth Inalámbrico Iniciado");
  Serial.println("📡 Esperando conexión HC-06...");
}

void loop() {
  // Todo el procesamiento se hace en las tareas FreeRTOS
  delay(1000);
}

// ============================================================================
// BLUETOOTH INALÁMBRICO - SIN CABLEADO
// ============================================================================

void bt_inalambrico_init() {
  Serial.println("\n");
  Serial.println("╔══════════════════════════════════════════╗");
  Serial.println("║         BLUETOOTH INALÁMBRICO           ║");
  Serial.println("║           ESP32 ↔ HC-06                 ║");
  Serial.println("║           SIN CONEXIONES POR CABLE      ║");
  Serial.println("╚══════════════════════════════════════════╝");
  
  // Inicializar Bluetooth Serial en modo MAESTRO
  if (!SerialBT.begin(BT_DEVICE_NAME, true)) {
    Serial.println("❌ Error crítico: No se pudo iniciar Bluetooth");
    return;
  }
  
  Serial.println("✅ Bluetooth Serial iniciado como MAESTRO");
  Serial.print("📛 Dispositivo: ");
  Serial.println(BT_DEVICE_NAME);
  
  // Mostrar MAC del ESP32 para referencia
  uint64_t mac = ESP.getEfuseMac();
  Serial.print("📡 MAC ESP32: ");
  Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X\n",
    (uint8_t)(mac >> 40), (uint8_t)(mac >> 32), (uint8_t)(mac >> 24),
    (uint8_t)(mac >> 16), (uint8_t)(mac >> 8), (uint8_t)mac);
}

void bt_esperar_conexion() {
  Serial.println("\n🔵 BUSCANDO HC-06 INALÁMBRICAMENTE...");
  Serial.println("==========================================");
  
  unsigned long inicio_espera = millis();
  const unsigned long TIMEOUT_CONEXION = 45000; // 45 segundos
  bool conexion_exitosa = false;
  
  // Convertir MAC string a array
  if (sscanf(HC06_MAC_STR, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
             &HC06_MAC[0], &HC06_MAC[1], &HC06_MAC[2],
             &HC06_MAC[3], &HC06_MAC[4], &HC06_MAC[5]) != 6) {
    Serial.println("❌ Error: Formato de MAC incorrecto");
    Serial.println("   Usa formato: 00:11:22:33:44:55");
    return;
  }
  
  Serial.print("🎯 Buscando HC-06 MAC: ");
  Serial.println(HC06_MAC_STR);
  
  while (!conexion_exitosa && (millis() - inicio_espera < TIMEOUT_CONEXION)) {
    
    // Intentar conexión cada 5 segundos
    static unsigned long ultimo_intento = 0;
    if (millis() - ultimo_intento > 5000) {
      ultimo_intento = millis();
      
      Serial.println("🔄 Intentando conexión inalámbrica...");
      
      // Configurar PIN del HC-06
      SerialBT.setPin(HC06_PIN);
      
      // Intentar conexión inalámbrica
      if (SerialBT.connect(HC06_MAC)) {
        conexion_exitosa = true;
        bt_connected = true;
        Serial.println("✅ ¡CONEXIÓN INALÁMBRICA ESTABLECIDA!");
        Serial.println("📡 HC-06 conectado via Bluetooth");
        break;
      } else {
        unsigned segundos_espera = (millis() - inicio_espera) / 1000;
        Serial.printf("⏳ No encontrado... (%u segundos)\n", segundos_espera);
        Serial.println("   Verifica:");
        Serial.println("   1. HC-06 encendido y en modo esclavo");
        Serial.println("   2. MAC correcta del HC-06");
        Serial.println("   3. HC-06 en rango Bluetooth");
      }
    }
    
    // Pequeña pausa para no saturar
    delay(500);
  }
  
  if (conexion_exitosa) {
    Serial.println("==========================================");
    Serial.println("🎉 ¡SISTEMA INALÁMBRICO CONECTADO!");
    Serial.println("📊 Iniciando recepción de datos GPS...");
    Serial.println("==========================================");
  } else {
    Serial.println("==========================================");
    Serial.println("❌ Timeout: HC-06 no encontrado");
    Serial.println("🔍 Verifica la configuración del HC-06");
    Serial.println("📡 Continuando en modo espera...");
    Serial.println("==========================================");
  }
}

void bt_verificar_conexion() {
  // Verificar estado de conexión periódicamente
  static unsigned long ultima_verificacion = 0;
  if (millis() - ultima_verificacion > 10000) { // Cada 10 segundos
    ultima_verificacion = millis();
    
    if (SerialBT.connected() && !bt_connected) {
      bt_connected = true;
      Serial.println("🔵 Reconexión Bluetooth detectada");
    } else if (!SerialBT.connected() && bt_connected) {
      bt_connected = false;
      Serial.println("🔴 Conexión Bluetooth perdida");
      Serial.println("🔄 Intentando reconexión automática...");
    }
  }
}

// ============================================================================
// PROCESAMIENTO GPS POR BLUETOOTH INALÁMBRICO
// ============================================================================

void gps_procesar_datos() {
  // Leer datos GPS via Bluetooth inalámbrico
  while (SerialBT.available()) {
    char c = SerialBT.read();
    
    // Alimentar parser GPS
    gps.encode(c);
    
    // Actualizar contadores
    chars_received++;
    last_data_time = millis();
    
    // Debug: mostrar datos NMEA (descomentar si necesario)
    // Serial.write(c);
  }
}

bool gps_obtener_posicion(GpsFix& fix) {
  // Modo simulación para pruebas sin HC-06
  if (test_mode) {
    static unsigned long ultima_simulacion = 0;
    if (millis() - ultima_simulacion > 2000) { // Nuevo dato cada 2 segundos
      // Simular posición en Barcelona
      fix.lat = 41.3851 + (random(-100, 100) / 100000.0);
      fix.lon = 2.1734 + (random(-100, 100) / 100000.0);
      fix.speedKph = random(0, 100);
      fix.courseDeg = random(0, 360);
      fix.hdop = 1.5;
      ultima_simulacion = millis();
      return true;
    }
    return false;
  }
  
  // Modo real: datos del GPS via Bluetooth inalámbrico
  if (gps.location.isValid() && gps.location.isUpdated()) {
    fix.lat = gps.location.lat();
    fix.lon = gps.location.lng();
    fix.speedKph = gps.speed.isValid() ? gps.speed.kmph() : 0.0;
    fix.courseDeg = gps.course.isValid() ? gps.course.deg() : 0.0;
    fix.hdop = gps.hdop.isValid() ? gps.hdop.hdop() : 99.0;
    return true;
  }
  
  return false;
}

void gps_mostrar_estado() {
  Serial.println("\n========== ESTADO INALÁMBRICO ==========");
  
  // Estado Bluetooth
  bool recibiendo_datos = (millis() - last_data_time < 5000) && (chars_received > 0);
  Serial.print("📶 Bluetooth: ");
  Serial.println(SerialBT.connected() ? "CONECTADO" : "DESCONECTADO");
  Serial.print("📨 Recepción: ");
  Serial.println(recibiendo_datos ? "ACTIVA" : "INACTIVA");
  Serial.print("📊 Datos recibidos: ");
  Serial.println(chars_received);
  
  if (SerialBT.connected() && chars_received > 0) {
    unsigned long segundos_sin_datos = (millis() - last_data_time) / 1000;
    Serial.print("⏱️  Último dato: ");
    Serial.print(segundos_sin_datos);
    Serial.println(" segundos");
  }
  
  // Estado GPS
  if (gps.location.isValid()) {
    Serial.println("\n📍 POSICIÓN GPS:");
    Serial.printf("   Latitud:  %.6f\n", gps.location.lat());
    Serial.printf("   Longitud: %.6f\n", gps.location.lng());
    
    if (gps.speed.isValid()) {
      Serial.printf("   Velocidad: %.1f km/h\n", gps.speed.kmph());
    }
    
    if (gps.satellites.isValid()) {
      Serial.printf("   Satélites: %d\n", gps.satellites.value());
    }
    
    if (gps.hdop.isValid()) {
      Serial.printf("   Precisión: %.2f\n", gps.hdop.hdop());
    }
  } else {
    Serial.println("\n🌎 GPS: Esperando señal...");
  }
  
  Serial.println("======================================");
}

// ============================================================================
// SISTEMA DE MAPA
// ============================================================================

void mapa_procesar_posicion(const GpsFix& fix) {
  // Simulación de zonas en Barcelona
  const double ZONA_CENTRO_LAT_MIN = 41.37;
  const double ZONA_CENTRO_LAT_MAX = 41.40;
  const double ZONA_CENTRO_LON_MIN = 2.16;
  const double ZONA_CENTRO_LON_MAX = 2.19;
  
  bool dentro_zona = (fix.lat >= ZONA_CENTRO_LAT_MIN && fix.lat <= ZONA_CENTRO_LAT_MAX &&
                     fix.lon >= ZONA_CENTRO_LON_MIN && fix.lon <= ZONA_CENTRO_LON_MAX);
  
  uint8_t limite_velocidad = dentro_zona ? 30 : 50; // 30 en centro, 50 fuera
  
  // Enviar información al control del motor
  mapa_enviar_control(dentro_zona, limite_velocidad);
  
  // Mostrar información
  Serial.printf("🗺️  %s | Límite: %d km/h | Vel: %.1f km/h\n",
                dentro_zona ? "ZONA CENTRO" : "ZONA PERIFERIA",
                limite_velocidad, fix.speedKph);
}

void mapa_enviar_control(bool dentro_zona, uint8_t limite_velocidad) {
  InterCoreMsg mensaje;
  mensaje.inside_zone = dentro_zona;
  mensaje.speed_limit = limite_velocidad;
  mensaje.valid = true;
  
  if (intercore_enviar_msg(mensaje)) {
    current_speed_kmh = gps.speed.isValid() ? gps.speed.kmph() : 0.0;
  }
}

// ============================================================================
// SISTEMA GENERAL
// ============================================================================

void sistema_init() {
  // Configurar LED integrado
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  
  // Inicializar comunicación entre núcleos
  if (!intercore_init()) {
    Serial.println("❌ Error crítico: Comunicación inter-núcleos falló");
    while(true) {
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      delay(200);
    }
  }
  
  sistema_mostrar_inicio();
}

void sistema_mostrar_inicio() {
  Serial.println("\n");
  Serial.println("╔══════════════════════════════════════════╗");
  Serial.println("║        SISTEMA INALÁMBRICO GPS         ║");
  Serial.println("║           ESP32 + HC-06                 ║");
  Serial.println("║        CONEXIÓN TOTAL POR BLUETOOTH     ║");
  Serial.println("╚══════════════════════════════════════════╝");
  
  Serial.println("\n🎯 OBJETIVO: Conexión 100% inalámbrica entre ESP32 y HC-06");
  Serial.println("📡 Sin cables entre módulos Bluetooth");
  
  Serial.println("\n📋 Comandos disponibles:");
  Serial.println("   t - Alternar modo test (sin HC-06)");
  Serial.println("   s - Estado del sistema");
  Serial.println("   g - Estado GPS/Bluetooth");
  Serial.println("   c - Forzar conexión HC-06");
  Serial.println("   d - Desconectar Bluetooth");
  Serial.println("   m - Simular movimiento GPS");
}

void sistema_procesar_comandos() {
  while (Serial.available()) {
    char comando = Serial.read();
    
    switch (comando) {
      case 't':
        test_mode = !test_mode;
        Serial.printf("🔧 Modo test: %s\n", test_mode ? "ACTIVADO" : "DESACTIVADO");
        if (test_mode) {
          Serial.println("   📍 Simulando datos GPS sin HC-06");
        } else {
          Serial.println("   📡 Usando datos reales del HC-06");
        }
        break;
        
      case 's':
        Serial.println("\n=== ESTADO DEL SISTEMA INALÁMBRICO ===");
        Serial.printf("🔧 Modo test: %s\n", test_mode ? "ON" : "OFF");
        Serial.printf("📶 BT Conectado: %s\n", SerialBT.connected() ? "SI" : "NO");
        Serial.printf("📨 Datos recibidos: %lu\n", chars_received);
        Serial.printf("🎯 Velocidad: %.1f km/h\n", current_speed_kmh);
        Serial.printf("🔋 Memoria libre: %d bytes\n", esp_get_free_heap_size());
        break;
        
      case 'g':
        gps_mostrar_estado();
        break;
        
      case 'c':
        Serial.println("🔄 Forzando conexión HC-06...");
        if (SerialBT.connected()) {
          SerialBT.disconnect();
          delay(1000);
        }
        bt_connected = false;
        bt_esperar_conexion();
        break;
        
      case 'd':
        Serial.println("🔌 Desconectando Bluetooth...");
        if (SerialBT.connected()) {
          SerialBT.disconnect();
          bt_connected = false;
          Serial.println("✅ Bluetooth desconectado");
        } else {
          Serial.println("❌ Bluetooth ya estaba desconectado");
        }
        break;
        
      case 'm':
        Serial.println("🎮 Activando simulación de movimiento...");
        test_mode = true;
        break;
        
      case '\n':
      case '\r':
        break;
        
      default:
        Serial.println("❌ Comando desconocido");
        Serial.println("   Usa: t, s, g, c, d, m");
        break;
    }
  }
}

// ============================================================================
// COMUNICACIÓN FREERTOS
// ============================================================================

bool intercore_init() {
  intercore_queue = xQueueCreate(5, sizeof(InterCoreMsg));
  return (intercore_queue != NULL);
}

bool intercore_enviar_msg(const InterCoreMsg& msg) {
  return xQueueSend(intercore_queue, &msg, pdMS_TO_TICKS(50)) == pdTRUE;
}

bool intercore_recibir_msg(InterCoreMsg& msg, TickType_t timeout) {
  return xQueueReceive(intercore_queue, &msg, timeout) == pdTRUE;
}

// ============================================================================
// TAREAS FREERTOS - SISTEMA INALÁMBRICO
// ============================================================================

void tareaGPS(void *pvParameters) {
  Serial.println("[Core 0] 🛰️ Iniciando sistema GPS inalámbrico...");
  
  // Inicializar Bluetooth inalámbrico
  bt_inalambrico_init();
  
  // Esperar conexión con HC-06
  bt_esperar_conexion();
  
  Serial.println("[Core 0] ✅ Sistema inalámbrico listo");
  
  unsigned long ultimo_procesamiento = 0;
  unsigned long ultimo_parpadeo = 0;
  bool estado_led = true;
  
  while (true) {
    // Parpadear LED indicando actividad
    if (millis() - ultimo_parpadeo > 1000) {
      estado_led = !estado_led;
      digitalWrite(LED_BUILTIN, estado_led);
      ultimo_parpadeo = millis();
    }
    
    // Procesar comandos del usuario
    sistema_procesar_comandos();
    
    // Verificar estado de conexión Bluetooth
    bt_verificar_conexion();
    
    // Procesar datos GPS via Bluetooth inalámbrico
    gps_procesar_datos();
    
    // Procesamiento principal cada 3 segundos
    if (millis() - ultimo_procesamiento >= 3000) {
      ultimo_procesamiento = millis();
      
      // Obtener posición GPS
      GpsFix posicion_actual;
      if (gps_obtener_posicion(posicion_actual)) {
        // Validar calidad de señal
        if (posicion_actual.hdop <= HDOP_MAX) {
          // Procesar en el sistema de mapa
          mapa_procesar_posicion(posicion_actual);
        } else if (!test_mode) {
          Serial.println("📡 Señal GPS débil - esperando mejor precisión");
        }
      } else if (SerialBT.connected() && !test_mode) {
        Serial.println("⏳ Esperando datos GPS del HC-06...");
      }
    }
    
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void tareaMotor(void *pvParameters) {
  Serial.println("[Core 1] 🚗 Iniciando sistema de control...");
  
  InterCoreMsg ultimo_mensaje = {false, 0, false};
  unsigned long ultimo_estado = 0;
  
  while (true) {
    // Recibir mensajes del núcleo GPS
    InterCoreMsg nuevo_mensaje;
    if (intercore_recibir_msg(nuevo_mensaje, pdMS_TO_TICKS(100))) {
      if (nuevo_mensaje.valid) {
        ultimo_mensaje = nuevo_mensaje;
        
        // Control basado en límites de velocidad
        if (ultimo_mensaje.inside_zone && current_speed_kmh > ultimo_mensaje.speed_limit) {
          Serial.printf("⚠️  ALERTA VELOCIDAD: %.1f km/h > Límite %d km/h\n", 
                       current_speed_kmh, ultimo_mensaje.speed_limit);
        }
      }
    }
    
    // Reporte de estado cada 15 segundos
    if (millis() - ultimo_estado > 15000) {
      ultimo_estado = millis();
      Serial.printf("[Control] Vel: %.1fkm/h | Zona: %s | Límite: %dkm/h\n",
                   current_speed_kmh,
                   ultimo_mensaje.inside_zone ? "CENTRO" : "PERIFERIA",
                   ultimo_mensaje.speed_limit);
    }
    
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
