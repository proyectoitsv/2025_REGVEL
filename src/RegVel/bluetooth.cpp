//=== bluetooth.cpp ===
#include "bluetooth.h"
#include "config.h"
#include "motor_control.h" // Para leer ADC/PWM en el debug
#include <BluetoothSerial.h>

// Buffer fijo para evitar fragmentación de heap
// #define NMEA_BUFFER_SIZE 128
// #define BT_BUFFER_SIZE 64

// --- Variables de Sistema ---
static uint8_t system_mode = DEFAULT_MODE;
static uint32_t last_debug_ms = 0;

// --- Bluetooth ---
BluetoothSerial SerialBT;
static bool bt_connected = false;
static String bt_buffer = ""; // Buffer para comandos de texto

// --- Variables GPS ---
static String nmea_buffer = "";
static float last_lat = 0.0, last_lon = 0.0;
static bool gps_fix = false;

// Variables para gestión Master/Slave
static uint8_t  target_mac[6] = BT_TARGET_MAC; // Definido en config.h
static bool     is_bt_master_hw = false;       // Rastrea si el hardware está en modo Master
static uint32_t last_reconnect_ms = 0;         // Timer para reintentos no bloqueantes

// --- MAPA (PROGMEM) ---
struct ZonePoint {
    double lat, lon;
    uint8_t limit_idx;
};
// Puntos de ejemplo (Córdoba)
const ZonePoint ZONES[] PROGMEM = {
    { -31.36217, -64.27673, 2 }, // 60 km/h
    { -31.36302, -64.27560, 1 }, // 40 km/h
    { -31.36629, -64.27923, 2 }
};
#define ZONE_COUNT (sizeof(ZONES)/sizeof(ZonePoint))

// --- Inicialización ---
void bluetooth_init() {
    SerialBT.begin(BT_NAME); 
    Serial.println("[Comm] BT Iniciado");
    Serial.printf("[Comm] Modo Inicial: %d\n", system_mode);
}

// --- Helpers GPS ---
double parse_coord(String raw, String dir) {
    if (raw.length() < 5) return 0.0;
    int dot = raw.indexOf('.');
    String deg = raw.substring(0, dot-2);
    String min = raw.substring(dot-2);
    double val = deg.toDouble() + (min.toDouble()/60.0);
    if (dir == "S" || dir == "W") val = -val;
    return val;
}


void manage_bt_hardware() {
    // CASO A: Estamos en MODO FULL -> Necesitamos ser MASTER
    if (system_mode == MODE_FULL) {
        // 1. Si el hardware no es Master, reiniciarlo como Master
        if (!is_bt_master_hw) {
            Serial.println("[BT] Cambiando HW a MASTER...");
            SerialBT.end(); 
            delay(100); // Pequeña pausa de seguridad para el stack BT
            // Inicia como Master (true)
            if(SerialBT.begin(BT_NAME, true)) {
                SerialBT.setPin(BT_MASTER_PIN, 4); //SerialBT.setPin(BT_MASTER_PIN, 4);
                is_bt_master_hw = true;
                bt_connected = false;
                Serial.println("[BT] HW Listo (Master). Buscando HC-06...");
            } else {
                Serial.println("[BT] Error iniciando modo Master");
            }
            return; // Esperar al siguiente ciclo para conectar
        }

        // 2. Si ya es Master pero no está conectado, intentar conectar periódicamente
        if (!SerialBT.connected()) {
            bt_connected = false;
            uint32_t now = millis();
            // Reintentar cada BT_RECONNECT_MS (ej. 10s)
            if (now - last_reconnect_ms > BT_RECONNECT_MS) {
                Serial.printf("[BT] Intentando conectar a MAC...\n");
                // connect() es bloqueante (1-3s), pero al estar en Core 0
                // NO afecta al motor (Core 1).
                if (SerialBT.connect(target_mac)) {
                    Serial.println("[BT] CONECTADO EXITOSAMENTE AL HC-06");
                    bt_connected = true;
                } else {
                    Serial.println("[BT] Falló conexión. Reintento en 10s");
                }
                last_reconnect_ms = now;
            }
        } else {
            // Ya conectado
            bt_connected = true; 
        }
    }
    // CASO B: NO estamos en MODO FULL -> Hardware como SLAVE (Comportamiento original)
    else {
        // Si estábamos en modo Master, volver a Slave
        if (is_bt_master_hw) {
            Serial.println("[BT] Liberando Master. Volviendo a SLAVE...");
            SerialBT.disconnect();
            SerialBT.end();
            delay(100);
            SerialBT.begin(BT_NAME); // Inicia como Slave (default)
            is_bt_master_hw = false;
            bt_connected = false;
        }
        // La gestión de bt_connected como Slave la hace bluetooth_update original
    }
}

// --- Lógica Principal (Llamada por TaskCore0) ---
bool bluetooth_update(uint8_t *detected_limit) {
    bool new_limit = false;

    // 0. GESTIÓN DE HARDWARE (NUEVO)
    // Revisa si hay que cambiar entre Master/Slave o reconectar
    manage_bt_hardware();

    // 1. Actualizar estado de conexión
    if (!is_bt_master_hw) {
        // MODO SLAVE (Parcial/NoBT): Usamos la detección estándar de cliente
        if (SerialBT.hasClient()) bt_connected = true;
        else bt_connected = false;
    } 
    // MODO MASTER (Full): bt_connected ya fue actualizado por manage_bt_hardware()

    // 2. Leer Serial USB (Cambio de Modo o Comandos)
    if (Serial.available()) {
        char c = Serial.read();
        // Selector de Modo
        if (c == CMD_SYS_NO_BT) { system_mode = MODE_NO_BT; Serial.println("Modo: NO BT"); }
        else if (c == CMD_SYS_PARTIAL) { system_mode = MODE_PARTIAL; Serial.println("Modo: PARCIAL"); }
        else if (c == CMD_SYS_FULL) { system_mode = MODE_FULL; Serial.println("Modo: FULL"); }
        // Comandos Directos (Solo si modo NO_BT)
        else if (system_mode == MODE_NO_BT && c >= '0' && c <= '4') {
            *detected_limit = c - '0';
            new_limit = true;
        }
    }

    // 3. Leer Bluetooth
    if (bt_connected && SerialBT.available()) {
        if (system_mode == MODE_FULL) {
            // --- MODO FULL (GPS NMEA) ---
            char c = SerialBT.read();
            if (c == '\n') {
                nmea_buffer.trim();
                
                // a) Chequear comando de cambio de modo por texto
                if (nmea_buffer == "MODE_PARTIAL") { system_mode = MODE_PARTIAL; Serial.println("BT: Modo PARCIAL"); }
                else if (nmea_buffer == "MODE_FULL") { system_mode = MODE_FULL; Serial.println("BT: Modo FULL"); }
                
                // b) Parsear GPRMC
                else if (nmea_buffer.startsWith("$GPRMC")) {
                    // Parser simple por comas
                    int idx = 0, comas = 0;
                    String parts[12]; // Necesitamos hasta la longitud (idx 6)
                    
                    for (int i=0; i<nmea_buffer.length() && comas < 12; i++) {
                        if (nmea_buffer[i] == ',') { comas++; }
                        else if (comas >= 1) { parts[comas-1] += nmea_buffer[i]; } 
                        // parts[0]=time, [1]=status, [2]=lat, [3]=NS, [4]=lon, [5]=EW
                    }
                    
                    if (parts[1] == "A") { // Status Active
                        gps_fix = true;
                        double lat = parse_coord(parts[2], parts[3]);
                        double lon = parse_coord(parts[4], parts[5]);
                        last_lat = lat; last_lon = lon;

                        // Comparar con Mapa
                        for (int i=0; i<ZONE_COUNT; i++) {
                            ZonePoint pt;
                            memcpy_P(&pt, &ZONES[i], sizeof(ZonePoint));
                            if (abs(lat - pt.lat) < 0.0002 && abs(lon - pt.lon) < 0.0002) {
                                *detected_limit = pt.limit_idx;
                                new_limit = true;
                            }
                        }
                    } else {
                        gps_fix = false;
                    }
                }
                nmea_buffer = "";
            } else {
                nmea_buffer += c;
            }
        } 
        else if (system_mode == MODE_PARTIAL) {
            // --- MODO PARCIAL (Comandos o Strings) ---
            // Leemos línea completa para soportar "MODE_FULL"
            String line = SerialBT.readStringUntil('\n');
            line.trim();
            if (line == "MODE_FULL") { system_mode = MODE_FULL; Serial.println("BT: Modo FULL"); }
            else if (line.length() == 1 && line[0] >= '0' && line[0] <= '4') {
                *detected_limit = line[0] - '0';
                new_limit = true;
            }
        }
    }

    // 4. Reporte de Debug (Cada 1s)
    if (millis() - last_debug_ms > 1000) {
        last_debug_ms = millis();
        
        // Info ADC/PWM (Siempre)
        Serial.printf("[DEBUG] ADC: %d | PWM: %d |  Vel: %5.1f km/h | Modo: %d\n", 
                      motor_get_adc(), motor_get_pwm(), motor_get_speed(), system_mode);
        
        // Info GPS (Solo modo FULL)
        if (system_mode == MODE_FULL) {
            Serial.printf("[GPS] Fix: %s | Lat: %.5f | Lon: %.5f\n |", 
                          gps_fix ? "SI" : "NO", last_lat, last_lon);
            
            // Enviar también por BT si conectado
            if (bt_connected) {
                SerialBT.printf("ADC:%d PWM:%d Fix:%d\n", motor_get_adc(), motor_get_pwm(),  gps_fix);
            }
        }
    }

    return new_limit;
}