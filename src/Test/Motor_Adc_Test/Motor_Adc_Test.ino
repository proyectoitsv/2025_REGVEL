/*
  Modulo2_ADC_PWM_Hall.ino
  Módulo de prueba (Core 1) - ADC (pedal) + PWM (motor) + Sensor Hall (velocidad) + reacción a límites

  Requisitos cubiertos:
  - Mantiene funciones y firmas que el proyecto principal espera (hall_init(), hall_get_speed_kmh(),
    adc_pedal_init(), adc_pedal_read(), pwm_motor_init(), pwm_motor_set_duty(), control_accelerator_task(), ...).
  - Autocontenible: compila y corre solo en ESP32 DevKit v1.
  - Incluye stubs para comunicación inter-core (simulación por Serial).
  - Incluye un stub "débil" para integrar con Módulo 1: visual_indicators_update(float) que puede ser reemplazado
    por el .o del módulo 1 cuando se linkeen juntos.
  - Ejemplo de interacción: el loop simula velocidad por sensor Hall real (puede usar pulsos manuales por GPIO),
    lectura de pedal y limitación de PWM cuando llega un mensaje de límite desde Core 0.
*/

#include <Arduino.h>

// --------------------------- Configuración pines ---------------------------
#define HALL_PIN 34          // Pin de entrada del sensor Hall (interrupt-capable)
#define PEDAL_ADC_PIN 35     // Pin ADC para lectura de pedal (0..4095)
#define MOTOR_PWM_PIN 14     // Pin de salida PWM que maneja el driver del motor (ej: EN de driver)
#define MOTOR_PWM_CHANNEL 0
#define MOTOR_PWM_FREQ 20000 // 20 kHz
#define MOTOR_PWM_RES 8      // 8-bit resolution (0-255)

// LED indicador (opcional)
#define LED_ACTIVITY_PIN 2

// --------------------------- Parámetros de vehículo / conversión ------------
#define WHEEL_CIRCUMFERENCE_M 2.05f // metros (ej: rueda + transmisión equivalente); ajustar según diseño
#define HALL_PULSES_PER_REV 1       // pulsos por revolución del sensor hall (ajustar si usa imán único)
#define MS_PER_CALC 250             // ventana de cálculo de velocidad (ms)

// Lógica de limitación
#define DEFAULT_MAX_DUTY 255
#define MIN_SAFE_DUTY 30            // mínimo duty que permitimos por seguridad (evitar apagado total)


// --------------------------- Tipo mensaje inter-core (simulación) -----------
struct InterCoreMsg {
  bool inside_zone;
  uint16_t speed_limit; // km/h
  bool valid;
};

// Simulación de mensaje inter-core (se escribe por Serial con "MSG ..." como en Módulo 1)
static InterCoreMsg g_simulated_msg = { false, 0, false };
void set_test_intercore_msg_from_serial(bool inside, uint16_t limit) {
  g_simulated_msg.inside_zone = inside;
  g_simulated_msg.speed_limit = limit;
  g_simulated_msg.valid = true;
}
bool intercore_receive_msg_stub(InterCoreMsg &out) {
  if (g_simulated_msg.valid) {
    out = g_simulated_msg;
    g_simulated_msg.valid = false;
    return true;
  }
  return false;
}

// Simulación de envío de estado a Core 0 (por ejemplo velocidad actual)
// En producción, reemplazar por intercore_send()
void intercore_send_status_stub(float speed_kmh, uint8_t duty) {
  Serial.printf(">> intercore_send_status_stub: speed=%.2f km/h duty=%u\n", speed_kmh, duty);
}

// --------------------------- Variables para Hall (conteo de pulsos) ----------
volatile uint32_t hall_pulse_count = 0;
volatile uint32_t hall_last_pulse_millis = 0;

// Ventana de cálculo
static uint32_t last_calc_time = 0;
static uint32_t last_calc_count = 0;
static float last_speed_kmh = 0.0f;

// --------------------------- Funciones del Hall ------------------------------
/**
 * @brief Handler de interrupción del sensor Hall: incrementa contador de pulsos.
 * Firma y comportamiento compatibles con la versión del proyecto.
 */
void IRAM_ATTR hall_isr() {
  hall_pulse_count=+1; //++
  hall_last_pulse_millis = millis();
}

/**
 * @brief Inicializa sensor Hall (configura interrupción).
 * Debe llamarse en init del core1. (Firma hall_init())
 */
void hall_init(uint8_t pin = HALL_PIN) {
  pinMode(pin, INPUT);
  // Usar RISING o FALLING según montaje
  attachInterrupt(digitalPinToInterrupt(pin), hall_isr, RISING);
  hall_pulse_count = 0;
  last_calc_time = millis();
  last_calc_count = 0;
  Serial.println("hall_init(): sensor Hall inicializado.");
}

/**
 * @brief Calcula velocidad en km/h basada en pulsos en la ventana de tiempo.
 * - Compatible con la función hall_get_speed_kmh() que usa el proyecto.
 * - Lógica: mide pulsos en ventana MS_PER_CALC (o desde último cálculo).
 */
float hall_get_speed_kmh() {
  uint32_t now = millis();
  uint32_t elapsed = now - last_calc_time;
  if (elapsed < 50) {
    // si llaman muy seguido, devolver último valor
    return last_speed_kmh;
  }

  // Leer contador atómicamente
  noInterrupts();
  uint32_t count = hall_pulse_count;
  interrupts();

  uint32_t delta_count = count - last_calc_count;
  if (delta_count == 0) {
    // si no hay movimiento, decrementar lentamente a 0
    if (elapsed > MS_PER_CALC) {
      last_speed_kmh *= 0.9f;
      if (last_speed_kmh < 0.1f) last_speed_kmh = 0.0f;
      last_calc_time = now;
      last_calc_count = count;
      return last_speed_kmh;
    }
    return last_speed_kmh;
  }

  // revoluciones en ventana = delta_count / pulses_per_rev
  float revs = (float)delta_count / (float)HALL_PULSES_PER_REV;
  float minutes = (float)elapsed / 60000.0f; // minutos
  float rpm = revs / minutes;
  // velocidad = rpm * circunferencia / 60 (m/s) -> km/h
  float speed_m_per_s = (rpm / 60.0f) * WHEEL_CIRCUMFERENCE_M;
  float speed_kmh = speed_m_per_s * 3.6f;

  // actualizar histórico
  last_speed_kmh = speed_kmh;
  last_calc_time = now;
  last_calc_count = count;

  return speed_kmh;
}

// --------------------------- Funciones ADC (pedal) ---------------------------
/**
 * @brief Inicializa ADC para lectura del pedal.
 * Firma: adc_pedal_init()
 */
void adc_pedal_init(uint8_t pin = PEDAL_ADC_PIN) {
  // en ESP32 no se necesita tanto; configurar atenuación si se requiere
  analogReadResolution(12); // 0..4095
  analogSetPinAttenuation(pin, ADC_11db); // rango mayor
  Serial.println("adc_pedal_init(): ADC inicializado.");
}

/**
 * @brief Lee pedal y devuelve valor normalizado 0.0..1.0
 * Firma: adc_pedal_read()
 */
float adc_pedal_read(uint8_t pin = PEDAL_ADC_PIN) {
  int raw = analogRead(pin);
  // map raw (0..4095) a 0..1
  float v = (float)raw / 4095.0f;
  if (v < 0.0f) v = 0.0f;
  if (v > 1.0f) v = 1.0f;
  return v;
}


// --------------------------- Funciones PWM motor ------------------------------
/**
 * @brief Inicializa PWM para motor (ledc).
 * Firma: pwm_motor_init()
 */
void pwm_motor_init(uint8_t pin, int channel, int freq, int res) {
  ledcSetup(channel, freq, res); 
  ledcAttachPin(pin, channel);
  ledcWrite(channel, 0);
  pinMode(LED_ACTIVITY_PIN, OUTPUT);
  digitalWrite(LED_ACTIVITY_PIN, LOW);
  Serial.println("pwm_motor_init(): PWM motor inicializado.");
}
/**
 * @brief Ajusta duty cycle (0..255)
 * Firma: pwm_motor_set_duty(uint8_t duty)
 */
void pwm_motor_set_duty(uint8_t duty, int channel = MOTOR_PWM_CHANNEL) {
  if (duty > 254) duty = 255;
  ledcWrite(channel, duty);
  digitalWrite(LED_ACTIVITY_PIN, duty > 0 ? HIGH : LOW);
}

// --------------------------- Lógica de control: reacción a límites --------------
static bool speed_limit_active = false;
static uint16_t current_speed_limit = 0;

/**
 * @brief Aplica la política de limitación al duty calculado por el pedal.
 * - duty_from_pedal: 0..255
 * - measured_speed_kmh: velocidad real del vehículo
 * - Devuelve duty final a aplicar al motor
 *
 * Este comportamiento está diseñado para respetar la estructura del proyecto principal:
 * - El módulo del acelerador calcula duty_base a partir del ADC del pedal
 * - Si hay límite activo y la velocidad medida > límite, se debe reducir duty para no superar límite
 * - La reducción puede ser proporcional (PID/simple) o cortar. Aquí implementamos un escalado simple.
 */
uint8_t apply_speed_limit_to_duty(uint8_t duty_from_pedal, float measured_speed_kmh) {
  if (!speed_limit_active || current_speed_limit == 0) {
    // sin límite, aplicar directamente (pero respetar MIN_SAFE_DUTY)
    return max((int)duty_from_pedal, MIN_SAFE_DUTY);
  }

  // Si estamos por debajo del límite, permitimos duty completo
  if (measured_speed_kmh <= (float)current_speed_limit - 0.5f) {
    return max((int)duty_from_pedal, MIN_SAFE_DUTY);
  }

  // Si superamos el límite: reducir duty proporcionalmente
  // factor = max(0.0, 1 - (speed - limit) / (speed)) -> simple
  float excess = measured_speed_kmh - (float)current_speed_limit;
  float factor = 1.0f - (excess / max(1.0f, measured_speed_kmh)); // evita div por 0
  if (factor < 0.0f) factor = 0.0f;
  // Aplicar factor al duty_from_pedal
  uint8_t limited_duty = (uint8_t) round((float)duty_from_pedal * factor);
  // No apagar completamente: respetar mínimo
  if (limited_duty < MIN_SAFE_DUTY) limited_duty = MIN_SAFE_DUTY;
  return limited_duty;
}

/**
 * @brief API que replica la función que en el proyecto principal recibe la decisión desde Core0.
 * Nombre compatible: apply_speed_limit(const InterCoreMsg& msg)
 */
void apply_speed_limit(const InterCoreMsg &msg) {
  if (!msg.valid) return;
  speed_limit_active = msg.inside_zone;
  current_speed_limit = msg.inside_zone ? msg.speed_limit : 0;
  Serial.printf("apply_speed_limit (Module2): inside=%s limit=%u\n",
                speed_limit_active ? "DENTRO" : "FUERA", current_speed_limit);
}

// --------------------------- Stubs para integración con Módulo 1 (Display+LEDs) -----------
/*
  El proyecto principal espera llamar a visual_indicators_update(speed_kmh) en Core1 para actualizar display/leds.
  Aquí definimos una versión "débil" (weak) que será reemplazada si al linkear se incluye Módulo1.
  Si probás este sketch solo, la función imprime por Serial en lugar de usar el hardware del display.
*/
void __attribute__((weak)) visual_indicators_update(float speed_kmh) {
  Serial.printf("[stub visual_indicators_update] speed=%.2f km/h\n", speed_kmh);
}

// --------------------------- Manejo Serial (comandos de prueba) ---------------------------
/*
 Comandos Serial disponibles para pruebas:
  - MSG INSIDE <limit>  -> simula mensaje inter-core indicando zona con límite
  - MSG OUT 0           -> simula mensaje indicando fuera de zona
  - PEDAL <0..1.0>      -> fija lectura de pedal (valor float) para pruebas (opcional)
  - PULSE               -> simula pulso Hall (incrementa contador) (útil para probar sin sensor físico)
  - INFO                -> imprime estado actual
*/
static float forced_pedal = -1.0f; // si >=0 se usa en lugar del ADC

void handle_serial_commands_module2() {
  while (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    if (line.startsWith("MSG")) {
      if (line.indexOf("INSIDE") >= 0) {
        int sp = line.lastIndexOf(' ');
        if (sp > 0) {
          uint16_t lim = (uint16_t) line.substring(sp+1).toInt();
          set_test_intercore_msg_from_serial(true, lim);
          Serial.printf("Serial -> simulando InterCore MSG INSIDE %u\n", lim);
        }
      } else if (line.indexOf("OUT") >= 0) {
        set_test_intercore_msg_from_serial(false, 0);
        Serial.println("Serial -> simulando InterCore MSG OUT");
      }
      continue;
    }

    if (line.startsWith("PEDAL")) {
      int sp = line.lastIndexOf(' ');
      if (sp > 0) {
        forced_pedal = line.substring(sp+1).toFloat();
        Serial.printf("Serial -> pedal forzado a %.3f (usar valor <0 para volver a ADC)\n", forced_pedal);
      }
      continue;
    }

    if (line.equalsIgnoreCase("PULSE")) {
      // Simula pulso
      noInterrupts();
      hall_pulse_count += 1;
      interrupts();
      Serial.println("Serial -> pulso HALL simulado");
      continue;
    }

    if (line.equalsIgnoreCase("INFO")) {
      Serial.printf("INFO: speed=%.2f km/h limitActive=%s limit=%u pulses=%u\n",
                    last_speed_kmh, speed_limit_active ? "YES":"NO", current_speed_limit, (unsigned)hall_pulse_count);
      continue;
    }

    Serial.printf("Comando desconocido (Module2): %s\n", line.c_str());
  }
}

// --------------------------- Tarea principal de control (loop) ---------------------------
/*
  control_accelerator_task() es la funcionalidad que en el proyecto principal estaría dentro de motorTask/core1.
  - Lee ADC pedal -> duty_base
  - Lee velocidad via Hall -> speed_kmh
  - Aplica limitación si corresponde -> duty_final
  - Escribe PWM al motor
  - Envía estado a Core0 (intercore_send_status_stub)
  - Actualiza visuales llamando a visual_indicators_update(speed_kmh) (stub débil)
*/
void control_accelerator_task() {
  // 1) Leer pedal
  float pedal = (forced_pedal >= 0.0f) ? forced_pedal : adc_pedal_read(PEDAL_ADC_PIN);
  // map pedal 0..1 a duty 0..255
  uint8_t duty_base = (uint8_t) roundf(pedal * (float)DEFAULT_MAX_DUTY);

  // 2) Calcular velocidad (cada MS_PER_CALC ms aprox)
  float speed_kmh = hall_get_speed_kmh();

  // 3) Aplicar limitación si llegó mensaje inter-core
  // Leer mensajes inter-core simulados
  InterCoreMsg m;
  if (intercore_receive_msg_stub(m)) {
    apply_speed_limit(m);
    // Además, para que el resto del sistema vea el cambio, podríamos notificar al Módulo 1:
    // El Módulo 1 real implementará apply_speed_limit(...) suyo y lámparas; aquí solo actualizamos flags.
  }

  uint8_t duty_final = apply_speed_limit_to_duty(duty_base, speed_kmh);

  // 4) Aplicar PWM
  pwm_motor_set_duty(duty_final);

  // 5) Enviar estado a Core0 (simulado)
  intercore_send_status_stub(speed_kmh, duty_final);

  // 6) Actualizar visuales (display/leds) a través del stub débil (o Módulo1 real si linkeado)
  visual_indicators_update(speed_kmh);

  // 7) Log opcional
  static uint32_t last_log_ms = 0;
  uint32_t now = millis();
  if (now - last_log_ms >= 1000) {
    last_log_ms = now;
    Serial.printf("[control] pedal=%.3f duty=%u speed=%.2f limitActive=%s limit=%u\n",
                  pedal, duty_final, speed_kmh, speed_limit_active ? "YES":"NO", current_speed_limit);
  }
}

// --------------------------- setup / loop de prueba ---------------------------
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("=== Module2 ADC+PWM+Hall (Core1) - prueba ===");

  // Inicializaciones
  hall_init(HALL_PIN);
  adc_pedal_init(PEDAL_ADC_PIN);
  pwm_motor_init(MOTOR_PWM_PIN, MOTOR_PWM_CHANNEL, MOTOR_PWM_FREQ, MOTOR_PWM_RES);

  // Estado inicial
  speed_limit_active = false;
  current_speed_limit = 0;
  forced_pedal = -1.0f;
  last_calc_time = millis();
  last_calc_count = 0;
  last_speed_kmh = 0.0f;

  Serial.println("Comandos disponibles: MSG INSIDE <n>, MSG OUT 0, PEDAL <0..1>, PULSE, INFO");
}

void loop() {
  // Manejar comandos de prueba por Serial
  handle_serial_commands_module2();

  // Ejecutar la tarea de control (no bloqueante)
  control_accelerator_task();

  // Frecuencia de tarea: 50..200 Hz depende de necesidades; aquí ~50 Hz
  delay(20);
}
