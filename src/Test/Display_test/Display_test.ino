/*
  Modulo1_Display_LEDs.ino
  Módulo de prueba (Core 1) - Display 7 segmentos + LEDs de umbral
  - Mantiene nombres de funciones del proyecto: display_7seg_init(), display_7seg_show_number(),
    display_7seg_refresh(), speed_leds_init(), speed_leds_update(), visual_indicators_init(),
    visual_indicators_update(), apply_speed_limit(...)
  - Autocontenible: no depende del resto del proyecto (incluye stubs mínimos para InterCoreMsg)
  - Simula mensajes desde Core 0 por Serial para pruebas (ej: "MSG INSIDE 60" o "MSG OUT 0")
  - Pins y thresholds por defecto coinciden con los del diseño.
  Referencias en repositorio: funciones y API tomadas de control_IO.h / control_IO.cpp. :contentReference[oaicite:3]{index=3} :contentReference[oaicite:4]{index=4}
*/

#include <Arduino.h>

// --------------------------- Configuración de pines (valores del diseño) ---------------------------
#define DISP_BCD_BIT1_PIN 4   // D0 (LSB)
#define DISP_BCD_BIT2_PIN 19  // D1
#define DISP_BCD_BIT3_PIN 17  // D2
#define DISP_BCD_BIT4_PIN 16  // D3 (MSB)

#define DISP_DIGIT1_PIN 21    // Centenas
#define DISP_DIGIT2_PIN 22    // Decenas
#define DISP_DIGIT3_PIN 23    // Unidades

#define DISP_COMMON_ANODE false   // false = común cátodo (ajusta según hardware)
#define DISP_MULTIPLEX_DELAY_US 2000  // microsegundos por dígito (~166Hz total)

#define LED_LIM1_PIN 26   // umbral 1 (40 km/h)
#define LED_LIM2_PIN 25   // umbral 2 (60 km/h)
#define LED_LIM3_PIN 33   // umbral 3 (110 km/h)
#define LED_LIM4_PIN 32   // umbral 4 (130 km/h)

#define LED_LIMIT_PIN 2   // LED que indica "límite aplicado (desde Core0)"
#define LED_OK_PIN 15     // LED OK sistema

// Umbrales (coinciden con config.h esperado)
#define LED_LIM1_THRESHOLD 40
#define LED_LIM2_THRESHOLD 60
#define LED_LIM3_THRESHOLD 110
#define LED_LIM4_THRESHOLD 130

// --------------------------- Tipos y stubs de comunicación inter-core (simulación) --------------
struct InterCoreMsg {
  bool inside_zone;
  uint16_t speed_limit;
  bool valid;
};

// En el módulo real esto vendría de intercore_comm; aquí hay un stub de test.
static InterCoreMsg g_simulated_msg = { false, 0, false };

// API simulada (para pruebas desde Serial)
// Llamar set_test_intercore_msg_from_serial() al parsear comando "MSG ..."
void set_test_intercore_msg_from_serial(bool inside, uint16_t limit) {
  g_simulated_msg.inside_zone = inside;
  g_simulated_msg.speed_limit = limit;
  g_simulated_msg.valid = true;
}

// Función que emula recepción no bloqueante de mensaje inter-core
bool intercore_receive_msg_stub(InterCoreMsg &out) {
  if (g_simulated_msg.valid) {
    out = g_simulated_msg;
    // Consumir el mensaje (simulación)
    g_simulated_msg.valid = false;
    return true;
  }
  return false;
}

// --------------------------- Variables internas display / leds ---------------------------
static uint16_t display_current_value = 0;
static uint8_t display_digits[3] = {0,0,0};
static uint8_t display_current_digit = 0;
static uint32_t display_last_multiplex_us = 0;

static bool speed_limit_active = false;
static uint16_t current_speed_limit = 0;

// --------------------------- Implementación display (mismas funciones públicas) -------------
static inline void write_bcd_digit(uint8_t digit) {
  if (digit > 9) digit = 9;
  digitalWrite(DISP_BCD_BIT1_PIN, (digit & 0x01) ? HIGH : LOW);
  digitalWrite(DISP_BCD_BIT2_PIN, (digit & 0x02) ? HIGH : LOW);
  digitalWrite(DISP_BCD_BIT3_PIN, (digit & 0x04) ? HIGH : LOW);
  digitalWrite(DISP_BCD_BIT4_PIN, (digit & 0x08) ? HIGH : LOW);
}

static inline void set_digit_active(uint8_t digit_index, bool state) {
  bool pin_state = DISP_COMMON_ANODE ? !state : state;
  switch(digit_index) {
    case 0: digitalWrite(DISP_DIGIT1_PIN, pin_state); break;
    case 1: digitalWrite(DISP_DIGIT2_PIN, pin_state); break;
    case 2: digitalWrite(DISP_DIGIT3_PIN, pin_state); break;
  }
}

static void decompose_number(uint16_t value) {
  if (value > 999) value = 999;
  display_digits[0] = (value / 100) % 10;
  display_digits[1] = (value / 10) % 10;
  display_digits[2] = value % 10;
  display_current_value = value;
}

/**
 * @brief Inicializa el display (Configura pines)
 * Igual firma que en el módulo original.
 */
void display_7seg_init() {
  pinMode(DISP_BCD_BIT1_PIN, OUTPUT);
  pinMode(DISP_BCD_BIT2_PIN, OUTPUT);
  pinMode(DISP_BCD_BIT3_PIN, OUTPUT);
  pinMode(DISP_BCD_BIT4_PIN, OUTPUT);
  pinMode(DISP_DIGIT1_PIN, OUTPUT);
  pinMode(DISP_DIGIT2_PIN, OUTPUT);
  pinMode(DISP_DIGIT3_PIN, OUTPUT);

  // desactivar todos los dígitos
  set_digit_active(0, false);
  set_digit_active(1, false);
  set_digit_active(2, false);

  decompose_number(0);
  display_last_multiplex_us = micros();

  Serial.println("Display 7seg inicializado (Module1).");
}

/**
 * @brief Actualiza el valor a mostrar (0..999)
 * Igual firma que en el módulo original.
 */
void display_7seg_show_number(uint16_t value) {
  decompose_number(value);
}

/**
 * @brief Refresh multiplexado, debe llamarse muy frecuentemente (~cada 10-20ms).
 * Igual firma que en el módulo original.
 */
void display_7seg_refresh() {
  uint32_t now = micros();
  if ((now - display_last_multiplex_us) < (uint32_t)DISP_MULTIPLEX_DELAY_US) return;
  display_last_multiplex_us = now;

  // apagar todos los dígitos (evita ghosting)
  set_digit_active(0, false);
  set_digit_active(1, false);
  set_digit_active(2, false);

  // escribir BCD del dígito actual
  write_bcd_digit(display_digits[display_current_digit]);
  // activar solo el dígito actual
  set_digit_active(display_current_digit, true);

  display_current_digit = (display_current_digit + 1) % 3;
}

// --------------------------- Implementación LEDs umbral ----------------------------------
/**
 * @brief Inicializa pines de LEDs de límite y LEDs del sistema
 */
void speed_leds_init() {
  pinMode(LED_LIM1_PIN, OUTPUT);
  pinMode(LED_LIM2_PIN, OUTPUT);
  pinMode(LED_LIM3_PIN, OUTPUT);
  pinMode(LED_LIM4_PIN, OUTPUT);
  pinMode(LED_LIMIT_PIN, OUTPUT);
  pinMode(LED_OK_PIN, OUTPUT);

  speed_leds_off();
  digitalWrite(LED_OK_PIN, HIGH); // sistema OK al inicio
  Serial.println("LEDs de umbral inicializados.");
}

/**
 * @brief Actualiza LEDs según velocidad en km/h
 * Igual firma / comportamiento que en control_IO.
 */
void speed_leds_update(float speed_kmh) {
  digitalWrite(LED_LIM1_PIN, (speed_kmh >= LED_LIM1_THRESHOLD) ? HIGH : LOW);
  digitalWrite(LED_LIM2_PIN, (speed_kmh >= LED_LIM2_THRESHOLD) ? HIGH : LOW);
  digitalWrite(LED_LIM3_PIN, (speed_kmh >= LED_LIM3_THRESHOLD) ? HIGH : LOW);
  digitalWrite(LED_LIM4_PIN, (speed_kmh >= LED_LIM4_THRESHOLD) ? HIGH : LOW);
}

/**
 * @brief Apaga todos los LEDs de límite
 */
void speed_leds_off() {
  digitalWrite(LED_LIM1_PIN, LOW);
  digitalWrite(LED_LIM2_PIN, LOW);
  digitalWrite(LED_LIM3_PIN, LOW);
  digitalWrite(LED_LIM4_PIN, LOW);
}

// --------------------------- Integración visual / API pública ------------------------------
/**
 * @brief Inicializa todos los elementos visuales (wrapper)
 * Firma y orden recomendada desde motorTask en proyecto original:
 * 1. io_accelerator_init()
 * 2. hall_init()
 * 3. visual_indicators_init()
 */
void visual_indicators_init() {
  speed_leds_init();
  display_7seg_init();
}

/**
 * @brief Actualiza visuales de forma coordinada (display + leds)
 * - speed_kmh: velocidad calculada (proporcionada por sensor Hall o simulación)
 * - Esta función no llama a display_7seg_refresh(); esa debe llamarse en cada ciclo rápido.
 */
void visual_indicators_update(float speed_kmh) {
  // actualizar leds de umbral
  speed_leds_update(speed_kmh);
  // actualizar display (valor entero)
  uint16_t showv = (uint16_t) roundf(speed_kmh);
  display_7seg_show_number(showv);
}

// --------------------------- Lógica de aplicar límite desde Core 0 (stub) ------------------
/**
 * @brief Aplica decisión de límite recibida desde Core 0.
 * Igual firma que en control_IO.h: receive InterCoreMsg y modifica comportamiento.
 * Para este módulo de prueba:
 *  - Activa/desactiva speed_limit_active
 *  - Enciende LED_LIMIT_PIN cuando inside_zone == true
 *  - Enciende/apaga LED_OK_PIN en consecuencia
 */
void apply_speed_limit(const InterCoreMsg &msg) {
  if (!msg.valid) return;
  speed_limit_active = msg.inside_zone;
  current_speed_limit = msg.inside_zone ? msg.speed_limit : 0;

  digitalWrite(LED_LIMIT_PIN, speed_limit_active ? HIGH : LOW);
  digitalWrite(LED_OK_PIN, speed_limit_active ? LOW : HIGH);

  Serial.printf("apply_speed_limit(): inside=%s limit=%u\n",
                msg.inside_zone ? "DENTRO" : "FUERA", msg.speed_limit);
}

// --------------------------- Ejemplo de loop / simulador de velocidad ----------------------
/*
  Opciones de simulación:
   - Por Serial: enviar "MSG INSIDE 60" o "MSG OUT 0" para simular mensajes desde Core0.
   - Para cambiar velocidad simular: enviar "SPD 45" (muestra 45 km/h)
   - Si no se envía nada el loop simula una rampa de velocidad.
*/

// estado de prueba (velocidad simulada)
static float simulated_speed_kmh = 0.0f;
static uint32_t last_speed_step_ms = 0;

void handle_serial_commands() {
  while (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    // Comando: MSG INSIDE <limit>
    if (line.startsWith("MSG")) {
      // formato: MSG INSIDE 60  ó  MSG OUT 0
      if (line.indexOf("INSIDE") >= 0) {
        int sp = line.lastIndexOf(' ');
        if (sp > 0) {
          uint16_t lim = (uint16_t) line.substring(sp+1).toInt();
          set_test_intercore_msg_from_serial(true, lim);
          Serial.printf("Serial -> simulando mensaje: INSIDE %u\n", lim);
        }
      } else if (line.indexOf("OUT") >= 0) {
        set_test_intercore_msg_from_serial(false, 0);
        Serial.println("Serial -> simulando mensaje: OUT");
      }
      continue;
    }

    // Comando: SPD <value>
    if (line.startsWith("SPD")) {
      int sp = line.lastIndexOf(' ');
      if (sp > 0) {
        simulated_speed_kmh = line.substring(sp+1).toFloat();
        Serial.printf("Serial -> velocidad simulada set a %.1f km/h\n", simulated_speed_kmh);
      }
      continue;
    }

    Serial.printf("Comando no reconocido: %s\n", line.c_str());
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("=== Module1 Display+LEDs (Core1) - prueba ===");
  visual_indicators_init();

  // Inicial values
  simulated_speed_kmh = 0.0f;
  last_speed_step_ms = millis();
}

void loop() {
  // 1) manejar comandos de prueba por Serial (simula mensajes desde Core0 o cambiar velocidad)
  handle_serial_commands();

  // 2) comprobar si hay mensaje inter-core (simulado) y aplicar cambios
  InterCoreMsg m;
  if (intercore_receive_msg_stub(m)) {
    apply_speed_limit(m);
  }

  // 3) actualizar velocidad simulada (si no la controla el usuario)
  uint32_t now = millis();
  if (now - last_speed_step_ms >= 500) {
    last_speed_step_ms = now;
    // si no fue fijada por SPD, hacemos una rampa simple
    // comment: el usuario puede fijar velocidad con "SPD 75"
    if (Serial.available() == 0) {
      simulated_speed_kmh += 2.5f;
      if (simulated_speed_kmh > 140.0f) simulated_speed_kmh = 0.0f;
    }
  }

  // 4) actualizar visuales periódicamente (50-100ms recomendado)
  static uint32_t last_visual_ms = 0;
  if (now - last_visual_ms >= 100) {
    last_visual_ms = now;
    // Si hay limit activo, podemos mostrar min(simulated_speed, limit) o la actual según prefieras.
    float display_speed = simulated_speed_kmh;
    if (speed_limit_active && current_speed_limit > 0) {
      // visual: mostramos velocidad real pero indicamos límite con LED_LIMIT_PIN.
      // (La lógica que limita el PWM está en el módulo del acelerador en Core1; aquí solo indicamos)
    }
    visual_indicators_update(display_speed);
  }

  // 5) refresh del multiplexado (debe llamarse muy frecuentemente)
  display_7seg_refresh();

  // pequeña espera no bloqueante
  delay(2);
}
