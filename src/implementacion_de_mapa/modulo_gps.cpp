/* ===========================================================================
   modulo_gps.cpp - ESP32 SLAVE + HC-05 MASTER (VERSIÓN SIMPLIFICADA)
   Configuración: ESP32 recibe datos NMEA del GPS vía HC-05 por Bluetooth

   CONFIGURACIÓN PREVIA REQUERIDA EN HC-05:
   1. AT+ROLE=1                    // Modo MASTER
   2. AT+CMODE=0                   // Conectar a dirección específica
   3. AT+BIND=XXXX,XX,XXXXXX      // MAC del ESP32 (ver abajo)
   4. AT+UART=9600,0,0            // Baudrate del GPS

   IMPORTANTE: Reemplaza la MAC en la constante HC05_TARGET_MAC
   ============================================================================= */

#include "modulo_gps.h"
#include "config.h"
#include <TinyGPSPlus.h>
#include <BluetoothSerial.h>

// ============================================================================
// CONFIGURACIÓN: Coloca aquí la MAC de tu HC-05 o ESP32
// ============================================================================
// Obtén la MAC ejecutando primero el código con esta línea descomentada en setup:
// Serial.println(String((uint32_t)(ESP.getEfuseMac() >> 16), HEX));

const char* BT_DEVICE_NAME = "ESP32_GPS_Slave";

// Instancias
TinyGPSPlus gps;
BluetoothSerial SerialBT;

// Variables de estado simples
static unsigned long last_data_time = 0;
static unsigned long chars_received = 0;

// --- (AGREGADO / CORRECCIÓN) -------------------------------------------------
// Se agregan las constantes y variables necesarias para la conexión por MAC
// (tomadas y adaptadas desde CODE_ESP_32_A_HC06.txt).
const char* HC06_MAC_STR = "00:22:09:01:2C:9E";
const char* HC06_PIN = "1234";
//int pin = 1234;
uint8_t HC06_MAC[6];
bool connected = false;
// -----------------------------------------------------------------------------

// Declaración de la función añadida
void bt_connect_hc06_by_mac();

void gps_init() {
  DEBUG_SERIAL.println("\n===========================================");
  DEBUG_SERIAL.println("  Iniciando GPS via HC-05 Bluetooth");
  DEBUG_SERIAL.println("===========================================");

  // Iniciar Bluetooth
  if (!SerialBT.begin(BT_DEVICE_NAME)) {
    DEBUG_SERIAL.println("ERROR: No se pudo iniciar Bluetooth");
    DEBUG_SERIAL.println("Reinicia el ESP32");
    while(1) delay(1000);
  }

  DEBUG_SERIAL.println("Bluetooth iniciado OK");
  DEBUG_SERIAL.print("Nombre BT: ");
  DEBUG_SERIAL.println(BT_DEVICE_NAME);

  // Obtener y mostrar MAC del ESP32 (para configurar HC-05)
  uint64_t mac = ESP.getEfuseMac();
  DEBUG_SERIAL.print("MAC ESP32: ");
  DEBUG_SERIAL.printf("%02X:%02X:%02X:%02X:%02X:%02X\n",
    (uint8_t)(mac >> 40), (uint8_t)(mac >> 32), (uint8_t)(mac >> 24),
    (uint8_t)(mac >> 16), (uint8_t)(mac >> 8), (uint8_t)mac);

  DEBUG_SERIAL.println("\nPara configurar HC-05 usa:");
  DEBUG_SERIAL.printf("AT+BIND=%02X%02X,%02X,%02X%02X%02X\n",
    (uint8_t)(mac >> 40), (uint8_t)(mac >> 32), (uint8_t)(mac >> 24),
    (uint8_t)(mac >> 16), (uint8_t)(mac >> 8), (uint8_t)mac);

  DEBUG_SERIAL.println("\nEsperando datos del HC-05...");
  DEBUG_SERIAL.println("(El HC-05 debe conectarse automaticamente)");
  DEBUG_SERIAL.println("===========================================\n");

  // Pequeña espera para dar tiempo a la conexión
  delay(3000);

  // ---------------------------------------------------------------------------
  // INVOCACIÓN: Intentar conectar activamente al HC-06 por MAC (nuevo comportamiento)
  // - El HC-06 solo puede ser slave; por eso el ESP32 debe buscar la MAC y conectarla.
  // - Llamada mínima aquí para no modificar el resto de la estructura.
  bt_connect_hc06_by_mac();
  // ---------------------------------------------------------------------------
}

void gps_feed_parser() {
  // Leer datos desde Bluetooth
  while (SerialBT.available()) {
    char c = SerialBT.read();

    // Alimentar el parser
    gps.encode(c);

    // Actualizar contadores
    chars_received++;
    last_data_time = millis();

    // Descomentar para debug (ver datos NMEA crudos):
    // DEBUG_SERIAL.write(c);
  }
}

bool gps_get_fix(GpsFix& fix) {
#if SIMULATE_INPUT
  // Modo simulación por Serial
  static String line;
  if (DEBUG_SERIAL.available()) {
    char c = (char)DEBUG_SERIAL.read();
    if (c == '\n') {
      line.trim();
      int c1 = line.indexOf(',');
      int c2 = line.indexOf(',', c1 + 1);
      int c3 = line.indexOf(',', c2 + 1);

      if (c1 > 0 && c2 > c1 && c3 > c2) {
        fix.lat = line.substring(0, c1).toDouble();
        fix.lon = line.substring(c1 + 1, c2).toDouble();
        fix.speedKph = line.substring(c2 + 1, c3).toFloat();
        fix.courseDeg = line.substring(c3 + 1, c3 + 1).toFloat();
        fix.hdop = 1.0f;
        line = "";
        return true;
      }
      line = "";
    } else {
      line += c;
    }
  }
  return false;
#else
  // Modo normal: usar datos del GPS
  if (gps.location.isValid() && gps.location.isUpdated()) {
    fix.lat = gps.location.lat();
    fix.lon = gps.location.lng();
    fix.speedKph = gps.speed.isValid() ? gps.speed.kmph() : NAN;
    fix.courseDeg = gps.course.isValid() ? gps.course.deg() : NAN;
    fix.hdop = gps.hdop.isValid() ? gps.hdop.hdop() : 99.0f;
    return true;
  }
  return false;
#endif
}

bool gps_is_bluetooth_connected() {
  // Consideramos conectado si hemos recibido datos recientemente
  // (en los últimos 5 segundos)
  return (millis() - last_data_time < 5000) && (chars_received > 0);
}

void gps_print_bluetooth_status() {
  DEBUG_SERIAL.println("\n========== ESTADO GPS/BLUETOOTH ==========");

  // Estado de recepción
  bool receiving = gps_is_bluetooth_connected();
  DEBUG_SERIAL.print("Estado: ");
  DEBUG_SERIAL.println(receiving ? "RECIBIENDO DATOS" : "SIN DATOS");

  DEBUG_SERIAL.print("Caracteres recibidos: ");
  DEBUG_SERIAL.println(chars_received);

  if (chars_received > 0) {
    unsigned long elapsed = (millis() - last_data_time) / 1000;
    DEBUG_SERIAL.print("Ultimo dato hace: ");
    DEBUG_SERIAL.print(elapsed);
    DEBUG_SERIAL.println(" seg");
  }

  DEBUG_SERIAL.println("-------------------------------------------");

  // Estado GPS
  if (gps.location.isValid()) {
    DEBUG_SERIAL.print("Latitud:  ");
    DEBUG_SERIAL.println(gps.location.lat(), 6);

    DEBUG_SERIAL.print("Longitud: ");
    DEBUG_SERIAL.println(gps.location.lng(), 6);

    if (gps.speed.isValid()) {
      DEBUG_SERIAL.print("Velocidad: ");
      DEBUG_SERIAL.print(gps.speed.kmph(), 1);
      DEBUG_SERIAL.println(" km/h");
    }

    if (gps.course.isValid()) {
      DEBUG_SERIAL.print("Rumbo: ");
      DEBUG_SERIAL.print(gps.course.deg(), 1);
      DEBUG_SERIAL.println(" grados");
    }
  } else {
    DEBUG_SERIAL.println("GPS: Sin Fix Valido");
  }

  // Satélites y calidad
  if (gps.satellites.isValid()) {
    DEBUG_SERIAL.print("Satelites: ");
    DEBUG_SERIAL.println(gps.satellites.value());
  }

  if (gps.hdop.isValid()) {
    DEBUG_SERIAL.print("HDOP: ");
    DEBUG_SERIAL.println(gps.hdop.hdop(), 2);
  }

  // Estadísticas del parser
  DEBUG_SERIAL.print("Chars procesados: ");
  DEBUG_SERIAL.println(gps.charsProcessed());

  DEBUG_SERIAL.print("Checksum fallidos: ");
  DEBUG_SERIAL.println(gps.failedChecksum());

  DEBUG_SERIAL.println("==========================================\n");
}

// Función auxiliar para ver datos crudos NMEA
void gps_enable_raw_debug(bool enable) {
  // Esta función se puede implementar con una variable global si se necesita
  DEBUG_SERIAL.print("Debug NMEA: ");
  DEBUG_SERIAL.println(enable ? "ACTIVADO" : "DESACTIVADO");
  DEBUG_SERIAL.println("Descomentar DEBUG_SERIAL.write(c) en gps_feed_parser()");
}

/* ============================================================================
   NUEVA FUNCIÓN AÑADIDA: Conectar al HC-06 usando su MAC
   - Se adapta la implementación que venía en CODE_ESP_32_A_HC06.txt
   - El ESP32 actúa activamente como cliente y conecta a la MAC del HC-06.
   - Mantiene mensajes de depuración con DEBUG_SERIAL.
   ============================================================================ */
void bt_connect_hc06_by_mac() {
  // Convertir MAC string a array uint8_t (formato XX:XX:XX:XX:XX:XX)
  sscanf(HC06_MAC_STR, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
         &HC06_MAC[0], &HC06_MAC[1], &HC06_MAC[2],
         &HC06_MAC[3], &HC06_MAC[4], &HC06_MAC[5]);

  DEBUG_SERIAL.print("Intentando conectar a HC-06 (MAC): ");
  DEBUG_SERIAL.println(HC06_MAC_STR);

  // Asegurar que el PIN esté configurado (algunos firmwares lo requieren)
  // setPin acepta (const char*, uint8_t) en algunos builds; usamos la longitud del PIN:
  SerialBT.setPin(HC06_PIN);//si esta linea da error probar con:  SerialBT.setPin(HC06_PIN, 4); ó   SerialBT.setPin(HC06_PIN);

  // Intento de conexión (bloqueante corto — retorna true si conectado)
  DEBUG_SERIAL.println("Conectando...");
  connected = SerialBT.connect(HC06_MAC);

  if (connected) {
    DEBUG_SERIAL.println("✅ Conectado al HC-06");
  } else {
    DEBUG_SERIAL.println("❌ Error en la conexión al HC-06");
  }

  // Nota: si se desea, se puede añadir lógica de reconexión periódica similar
  // al ejemplo original; aquí solo agregamos el intento inicial para no alterar
  // la estructura actual del proyecto.
}
