/* ===========================================================================
   [PRESENTACIÓN] control_IO.cpp - VERSIÓN ACTUALIZADA
   Implementación del módulo de I/O y del acelerador (ADC → EMA → PWM via LEDC).
   
   NUEVAS SECCIONES IMPLEMENTADAS:
   - LEDs de límite de velocidad (4 umbrales)
   - Display de 7 segmentos triple con multiplexado
   - Conversión RPM a velocidad con factor de escala configurable
   ============================================================================= */

#include <Arduino.h>
#include "control_IO.h"
#include "config.h"
#include <driver/ledc.h>
#include <driver/adc.h>

// ========================= VARIABLES MÓDULO ACELERADOR (EXISTENTES) =========================
static float smoothed_adc_value = 0.0f;
static bool accelerator_initialized = false;
static float current_duty_percent = MIN_DUTY_PERCENT;
static float target_duty_percent = MIN_DUTY_PERCENT;
static uint32_t last_ramp_ms = 0;
static int last_raw_adc = 0;
static uint32_t last_duty_value = 0;

// ========================= VARIABLES SENSOR HALL (EXISTENTES) =========================
static volatile uint32_t hall_pulse_count = 0;
static uint32_t hall_last_time = 0;
static float hall_rpm = 0.0f;

// ========================= VARIABLES LÍMITE DE VELOCIDAD (EXISTENTES) =========================
static bool speed_limit_active = false;
static uint16_t current_speed_limit = 0;

// ========================= [NUEVO] VARIABLES DISPLAY 7 SEGMENTOS =========================
static uint16_t display_current_value = 0;      // Valor actual mostrado
static uint8_t display_current_digit = 0;       // Dígito actualmente multiplexado (0-2)
static uint32_t display_last_multiplex_us = 0;  // Timestamp del último cambio de dígito
static uint8_t display_digits[3] = {0, 0, 0};   // [centenas, decenas, unidades]

// ========================= [NUEVO] VARIABLES VELOCIDAD =========================
static float current_speed_kmh = 0.0f;          // Velocidad calculada actual
static uint32_t last_speed_calc_ms = 0;         // Timestamp del último cálculo

// ========================= FUNCIONES GENERALES (SIN CAMBIOS) =========================
void io_init() {
  DEBUG_SERIAL.begin(DEBUG_BAUD);
  //delay(200);
}

void io_print_startup_message() {
  DEBUG_SERIAL.println("\nESP32 Speed Limit Alert - Inicio (Dual-Core)");
}

void io_print_map_summary(size_t point_count, double lat0, double lon0) {
  DEBUG_SERIAL.printf("Mapa cargado: %u puntos\n", (unsigned)point_count);
  DEBUG_SERIAL.printf("Centro local: lat0=%.6f lon0=%.6f\n", lat0, lon0);
}

void io_print_error(const char* message) {
  DEBUG_SERIAL.println(message);
}

void io_print_message(const char* message) {
  DEBUG_SERIAL.println(message);
}

void io_print_no_match(const GpsFix& fix) {
  DEBUG_SERIAL.printf("SIN_MATCH: lat=%.6f lon=%.6f v=%.1f km/h hdop=%.1f\n",
                      fix.lat, fix.lon, fix.speedKph, fix.hdop);
}

void io_print_status(bool is_alert, float filtered_speed, const MatchResult& match) {
  if (is_alert) {
    float exceso = filtered_speed - (float)match.speed_kph;
    DEBUG_SERIAL.printf("ALERTA: speed %.1f km/h > limit %u km/h (exceso %.1f) [id %d, d=%.1f m]\n",
                        filtered_speed, match.speed_kph, exceso, match.id, match.distance_m);
  } else {
    DEBUG_SERIAL.printf("OK: speed %.1f km/h <= limit %u km/h [id %d, d=%.1f m]\n",
                        filtered_speed, match.speed_kph, match.id, match.distance_m);
  }
}

// ========================= INICIALIZACIÓN ACELERADOR (SIN CAMBIOS IMPORTANTES) =========================
void io_accelerator_init() {
  ledc_timer_config_t timer_config;
  timer_config.speed_mode = LEDC_LOW_SPEED_MODE;
#if PWM_RESOLUTION_BITS == 10
  timer_config.duty_resolution = LEDC_TIMER_10_BIT;
#elif PWM_RESOLUTION_BITS == 8
  timer_config.duty_resolution = LEDC_TIMER_8_BIT;
#elif PWM_RESOLUTION_BITS == 12
  timer_config.duty_resolution = LEDC_TIMER_12_BIT;
#else
  timer_config.duty_resolution = LEDC_TIMER_10_BIT;
#endif
  timer_config.timer_num = (ledc_timer_t)PWM_TIMER;
  timer_config.freq_hz = PWM_FREQ_HZ;
  timer_config.clk_cfg = LEDC_AUTO_CLK;
  ledc_timer_config(&timer_config);

  ledc_channel_config_t channel_config;
  channel_config.gpio_num = (gpio_num_t)MOTOR_PWM_PIN;
  channel_config.speed_mode = LEDC_LOW_SPEED_MODE;
  channel_config.channel = (ledc_channel_t)PWM_CHANNEL;
  channel_config.timer_sel = (ledc_timer_t)PWM_TIMER;
  channel_config.duty = 0;
  channel_config.hpoint = 0;
  ledc_channel_config(&channel_config);

#if defined(analogSetPinAttenuation)
  analogSetPinAttenuation(PEDAL_ADC_PIN, PEDAL_ADC_ATTENUATION);
#endif

  int init_raw = analogRead(PEDAL_ADC_PIN);
  if (init_raw < 0 || init_raw > ADC_MAX_READING) {
    smoothed_adc_value = 0.0f;
  } else {
    smoothed_adc_value = (float)init_raw;
  }

  current_duty_percent = MIN_DUTY_PERCENT;
  target_duty_percent = MIN_DUTY_PERCENT;
  last_ramp_ms = millis();
  accelerator_initialized = true;

  DEBUG_SERIAL.printf("ACCEL INIT OK: PWM pin=%d, ch=%d, freq=%dHz, res=%d bits, ADC pin=%d\n",
                      MOTOR_PWM_PIN, PWM_CHANNEL, PWM_FREQ_HZ, PWM_RESOLUTION_BITS, PEDAL_ADC_PIN);
}

// ========================= LECTURA + FILTRO + ACTUALIZACIÓN PWM (CON LÍMITE) =========================
void io_update_motor_from_pedal() {
  if (!accelerator_initialized) return;

  int raw_adc = analogRead(PEDAL_ADC_PIN);

  if (raw_adc < 0 || raw_adc > ADC_MAX_READING) {
    DEBUG_SERIAL.println("ADC inválido -> aplicando fail-safe MIN_DUTY.");
    target_duty_percent = MIN_DUTY_PERCENT;
  } else {
    smoothed_adc_value = (PEDAL_EMA_ALPHA * (float)raw_adc) + ((1.0f - PEDAL_EMA_ALPHA) * smoothed_adc_value);
    float norm = smoothed_adc_value / (float)ADC_MAX_READING;
    norm = constrain(norm, 0.0f, 1.0f);
    target_duty_percent = MIN_DUTY_PERCENT + norm * (MAX_DUTY_PERCENT - MIN_DUTY_PERCENT);

    if (speed_limit_active) {
      float max_allowed_duty = map(current_speed_limit, 0, 120, MIN_DUTY_PERCENT, MAX_DUTY_PERCENT);
      target_duty_percent = min(target_duty_percent, max_allowed_duty);
    }

    last_raw_adc = raw_adc;
  }

  uint32_t now = millis();
  uint32_t elapsed = (now >= last_ramp_ms) ? (now - last_ramp_ms) : 0;
  if (elapsed > 0) {
    float step_percent = (RAMP_RATE_PERCENT_PER_100MS * ((float)elapsed / (float)RAMP_INTERVAL_MS));
    if (step_percent < 0.01f) step_percent = 0.01f;
    if (fabs(target_duty_percent - current_duty_percent) <= step_percent) {
      current_duty_percent = target_duty_percent;
    } else if (target_duty_percent > current_duty_percent) {
      current_duty_percent += step_percent;
    } else {
      current_duty_percent -= step_percent;
    }
    last_ramp_ms = now;
  }

  uint32_t max_duty_value = ((1u << PWM_RESOLUTION_BITS) - 1u);
  uint32_t duty_value = (uint32_t)roundf((current_duty_percent / 100.0f) * (float)max_duty_value);

  ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)PWM_CHANNEL, duty_value);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)PWM_CHANNEL);

  last_duty_value = duty_value;

  static uint32_t lastDbg = 0;
  if (millis() - lastDbg >= 250) {
    float voltage = (smoothed_adc_value / (float)ADC_MAX_READING) * 3.3f;
    float norm = smoothed_adc_value / (float)ADC_MAX_READING;
    DEBUG_SERIAL.printf("ADC raw=%4d filt=%.1f V=%.2f norm=%.3f tgt=%.1f cur=%.1f duty=%lu/%lu\n",
                        last_raw_adc, smoothed_adc_value, voltage,
                        norm, target_duty_percent, current_duty_percent,
                        (unsigned long)last_duty_value, (unsigned long)max_duty_value);
    lastDbg = millis();
  }
}

// ========================= WRAPPERS ACELERADOR =========================
void accel_init() { io_accelerator_init(); }
float accel_read_norm() {
  if (!accelerator_initialized) return 0.0f;
  return smoothed_adc_value / (float)ADC_MAX_READING;
}
void accel_update_pwm() { io_update_motor_from_pedal(); }

void io_accelerator_print_debug() {
  uint32_t max_duty_value = ((1u << PWM_RESOLUTION_BITS) - 1u);
  float voltage = (smoothed_adc_value / (float)ADC_MAX_READING) * 3.3f;
  float norm = smoothed_adc_value / (float)ADC_MAX_READING;
  DEBUG_SERIAL.printf("[ACCEL] t=%lu raw=%d filt=%.1f V=%.2f norm=%.3f tgt=%.1f cur=%.1f duty=%lu/%lu\n",
                      (unsigned long)millis(), last_raw_adc, smoothed_adc_value, voltage,
                      norm, target_duty_percent, current_duty_percent,
                      (unsigned long)last_duty_value, (unsigned long)max_duty_value);
}

// ========================= SENSOR HALL (SIN CAMBIOS) =========================
void hall_init() {
  pinMode(HALL_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(HALL_PIN), hall_isr, FALLING);
  hall_last_time = millis();
  hall_pulse_count = 0;
}

void IRAM_ATTR hall_isr() {
  hall_pulse_count++;
}

uint32_t hall_get_pulse_count() {
  return hall_pulse_count;
}

float hall_get_rpm() {
  uint32_t current_time = millis();
  uint32_t time_diff = current_time - hall_last_time;
  
  if (time_diff >= 1000) {
    uint32_t pulses = hall_pulse_count;
    hall_pulse_count = 0;
    hall_last_time = current_time;
    
    hall_rpm = (float)pulses * 60.0f / ((float)time_diff / 1000.0f) / (float)HALL_PULSES_PER_REV;
  }
  
  return hall_rpm;
}

void hall_reset_pulse_count() {
  hall_pulse_count = 0;
  hall_last_time = millis();
}

// ========================= LEDS ORIGINALES (SIN CAMBIOS) =========================
void leds_init() {
  pinMode(LED_LIMIT_PIN, OUTPUT);
  pinMode(LED_OK_PIN, OUTPUT);
  digitalWrite(LED_LIMIT_PIN, LOW);
  digitalWrite(LED_OK_PIN, HIGH);
}

void led_limit_set(bool state) {
  digitalWrite(LED_LIMIT_PIN, state ? HIGH : LOW);
}

void led_ok_set(bool state) {
  digitalWrite(LED_OK_PIN, state ? HIGH : LOW);
}

void apply_speed_limit(const InterCoreMsg& msg) {
  if (msg.valid) {
    speed_limit_active = msg.inside_zone;
    current_speed_limit = msg.speed_limit;
    
    if (msg.inside_zone) {
      led_limit_set(true);
      led_ok_set(false);
    } else {
      led_limit_set(false);
      led_ok_set(true);
    }
  }
}

// ========================= [NUEVO] IMPLEMENTACIÓN LEDS DE LÍMITE =========================
/**
 * Inicializa los 4 LEDs indicadores de velocidad
 * Configura los pines GPIO como salidas digitales y los pone en estado bajo
 */
void speed_leds_init() {
  pinMode(LED_LIM1_PIN, OUTPUT);
  pinMode(LED_LIM2_PIN, OUTPUT);
  pinMode(LED_LIM3_PIN, OUTPUT);
  pinMode(LED_LIM4_PIN, OUTPUT);
  
  // Apagar todos los LEDs al inicio
  speed_leds_off();
  
  DEBUG_SERIAL.println("LEDs de límite inicializados:");
  DEBUG_SERIAL.printf("  LED1 (pin %d): %d km/h\n", LED_LIM1_PIN, LED_LIM1_THRESHOLD);
  DEBUG_SERIAL.printf("  LED2 (pin %d): %d km/h\n", LED_LIM2_PIN, LED_LIM2_THRESHOLD);
  DEBUG_SERIAL.printf("  LED3 (pin %d): %d km/h\n", LED_LIM3_PIN, LED_LIM3_THRESHOLD);
  DEBUG_SERIAL.printf("  LED4 (pin %d): %d km/h\n", LED_LIM4_PIN, LED_LIM4_THRESHOLD);
}

/**
 * Actualiza el estado de los LEDs según la velocidad actual
 * Los LEDs se encienden progresivamente según se superan los umbrales
 * 
 * @param speed_kmh Velocidad actual en km/h
 */
void speed_leds_update(float speed_kmh) {
  // LED 1: 40 km/h
  digitalWrite(LED_LIM1_PIN, (speed_kmh >= LED_LIM1_THRESHOLD) ? HIGH : LOW);
  
  // LED 2: 60 km/h
  digitalWrite(LED_LIM2_PIN, (speed_kmh >= LED_LIM2_THRESHOLD) ? HIGH : LOW);
  
  // LED 3: 110 km/h
  digitalWrite(LED_LIM3_PIN, (speed_kmh >= LED_LIM3_THRESHOLD) ? HIGH : LOW);
  
  // LED 4: 130 km/h
  digitalWrite(LED_LIM4_PIN, (speed_kmh >= LED_LIM4_THRESHOLD) ? HIGH : LOW);
}

/**
 * Apaga todos los LEDs de límite
 * Útil para reset del sistema o modo de prueba
 */
void speed_leds_off() {
  digitalWrite(LED_LIM1_PIN, LOW);
  digitalWrite(LED_LIM2_PIN, LOW);
  digitalWrite(LED_LIM3_PIN, LOW);
  digitalWrite(LED_LIM4_PIN, LOW);
}

// ========================= [NUEVO] IMPLEMENTACIÓN DISPLAY 7 SEGMENTOS =========================
/**
 * Función auxiliar: escribe un dígito BCD (0-9) en los pines del CD4511BE
 * 
 * @param digit Dígito a mostrar (0-9)
 * 
 * El CD4511BE convierte BCD (4 bits) a señales para 7 segmentos
 * Bits: D3 D2 D1 D0 (MSB a LSB)
 */
static inline void write_bcd_digit(uint8_t digit) {
  if (digit > 9) digit = 9; // Limitar a 0-9
  
  // Escribir cada bit del dígito BCD
  digitalWrite(DISP_BCD_BIT1_PIN, (digit & 0x01) ? HIGH : LOW); // LSB (bit 0)
  digitalWrite(DISP_BCD_BIT2_PIN, (digit & 0x02) ? HIGH : LOW); // bit 1
  digitalWrite(DISP_BCD_BIT3_PIN, (digit & 0x04) ? HIGH : LOW); // bit 2
  digitalWrite(DISP_BCD_BIT4_PIN, (digit & 0x08) ? HIGH : LOW); // MSB (bit 3)
}

/**
 * Función auxiliar: activa/desactiva un dígito específico del display
 * 
 * @param digit_index Índice del dígito (0=centenas, 1=decenas, 2=unidades)
 * @param state true para activar, false para desactivar
 */
static inline void set_digit_active(uint8_t digit_index, bool state) {
  // Para común cátodo: HIGH activa, LOW desactiva
  // Para común ánodo: LOW activa, HIGH desactiva
  bool pin_state = HIGH; //DISP_COMMON_ANODE ? !state : state
  
  switch(digit_index) {
    case 0: digitalWrite(DISP_DIGIT1_PIN, pin_state); break; // Centenas
    case 1: digitalWrite(DISP_DIGIT2_PIN, pin_state); break; // Decenas
    case 2: digitalWrite(DISP_DIGIT3_PIN, pin_state); break; // Unidades
  }
}

/**
 * Inicializa el display de 7 segmentos triple
 * Configura todos los pines como salidas y limpia el display
 */
void display_7seg_init() {
  // Configurar pines BCD como salidas
  pinMode(DISP_BCD_BIT1_PIN, OUTPUT);
  pinMode(DISP_BCD_BIT2_PIN, OUTPUT);
  pinMode(DISP_BCD_BIT3_PIN, OUTPUT);
  pinMode(DISP_BCD_BIT4_PIN, OUTPUT);
  
  // Configurar pines de control de dígitos como salidas
  pinMode(DISP_DIGIT1_PIN, OUTPUT);
  pinMode(DISP_DIGIT2_PIN, OUTPUT);
  pinMode(DISP_DIGIT3_PIN, OUTPUT);
  
  // Desactivar todos los dígitos
  set_digit_active(0, false);
  set_digit_active(1, false);
  set_digit_active(2, false);
  
  // Limpiar el display
  display_7seg_clear();
  
  DEBUG_SERIAL.println("Display 7 segmentos inicializado:");
  DEBUG_SERIAL.printf("  Tipo: %s\n", DISP_COMMON_ANODE ? "Común Ánodo" : "Común Cátodo");
  DEBUG_SERIAL.printf("  Delay multiplexado: %d us\n", DISP_MULTIPLEX_DELAY_US);
}

/**
 * Descompone un número en dígitos individuales
 * 
 * @param value Número a descomponer (0-999)
 */
static void decompose_number(uint16_t value) {
  if (value > 999) value = 999; // Limitar a 999
  
  display_digits[0] = (value / 100) % 10;  // Centenas
  display_digits[1] = (value / 10) % 10;   // Decenas
  display_digits[2] = value % 10;          // Unidades
  
  display_current_value = value;
}

/**
 * Muestra un número de 3 dígitos en el display
 * Esta función solo actualiza el valor interno; display_7seg_refresh() debe
 * ser llamada periódicamente para mantener el display visible
 * 
 * @param value Valor a mostrar (0-999)
 */
void display_7seg_show_number(uint16_t value) {
  decompose_number(value);
}

/**
 * Refresca el display con multiplexado temporal
 * Esta función debe llamarse con alta frecuencia (cada 10-20ms) desde el loop
 * principal de Core 1 para mantener el display visible sin parpadeo
 * 
 * El multiplexado funciona así:
 * 1. Desactiva todos los dígitos
 * 2. Escribe el valor BCD del dígito actual al CD4511BE
 * 3. Activa solo el dígito actual
 * 4. Espera DISP_MULTIPLEX_DELAY_US microsegundos
 * 5. Pasa al siguiente dígito
 */
void display_7seg_refresh() {
  uint32_t current_us = micros();
  
  // Verificar si es tiempo de cambiar de dígito
  if ((current_us - display_last_multiplex_us) >= DISP_MULTIPLEX_DELAY_US) {
    // Desactivar todos los dígitos para evitar "ghosting"
    set_digit_active(0, false);
    set_digit_active(1, false);
    set_digit_active(2, false);
    
    // Pequeña pausa para evitar ghosting (opcional, depende del hardware)
    delayMicroseconds(10);
    
    // Escribir el dígito actual al CD4511BE
    write_bcd_digit(display_digits[display_current_digit]);
    
    // Activar solo el dígito actual
    set_digit_active(display_current_digit, true);
    
    // Avanzar al siguiente dígito (0 -> 1 -> 2 -> 0 ...)
    display_current_digit = (display_current_digit + 1) % 3;
    
    // Actualizar timestamp
    display_last_multiplex_us = current_us;
  }
}

/**
 * Limpia el display (muestra 000)
 */
void display_7seg_clear() {
  display_7seg_show_number(0);
}

// ========================= [NUEVO] CONVERSIÓN RPM A VELOCIDAD =========================
/**
 * Convierte RPM del motor a velocidad en km/h
 * 
 * Fórmula:
 * velocidad_real = (RPM × π × DIAMETRO × 60 × GEAR_RATIO) / 1000
 * velocidad_display = velocidad_real × SPEED_MULTIPLIER_FACTOR
 * 
 * @param rpm Revoluciones por minuto
 * @return Velocidad en km/h (escalada con SPEED_MULTIPLIER_FACTOR)
 */
float rpm_to_speed_kmh(float rpm) {
  if (rpm < 0.1f) return 0.0f; // Motor detenido o RPM demasiado bajo
  
  // Calcular velocidad real basada en física del motor/rueda
  // Fórmula: v (m/s) = RPM × (2π × radio) / 60
  //          v (km/h) = v (m/s) × 3.6
  // Simplificado: v (km/h) = RPM × π × DIAMETRO × 60 / 1000
  
  float circumference_m = PI * WHEEL_DIAMETER_M;  // Circunferencia de la rueda
  float speed_real_mps = (rpm * circumference_m * GEAR_RATIO) / 60.0f; // m/s
  float speed_real_kmh = speed_real_mps * 3.6f;  // Convertir a km/h
  
  // Aplicar factor multiplicador para simulación
  float speed_display_kmh = speed_real_kmh * SPEED_MULTIPLIER_FACTOR;
  
  // Aplicar umbral mínimo
  if (speed_display_kmh < MIN_DISPLAY_SPEED_KMH) {
    return 0.0f;
  }
  
  return speed_display_kmh;
}

/**
 * Obtiene la velocidad actual calculada desde el sensor Hall
 * Esta función actualiza el cálculo periódicamente para evitar cálculos innecesarios
 * 
 * @return Velocidad en km/h (ya escalada)
 */
float get_current_speed_kmh() {
  uint32_t now = millis();
  
  // Solo recalcular cada SPEED_CALC_INTERVAL_MS ms
  if ((now - last_speed_calc_ms) >= SPEED_CALC_INTERVAL_MS) {
    float rpm = hall_get_rpm();
    current_speed_kmh = rpm_to_speed_kmh(rpm);
    last_speed_calc_ms = now;
  }
  
  return current_speed_kmh;
}

// ========================= [NUEVO] INTEGRACIÓN VISUAL =========================
/**
 * Inicializa todos los elementos visuales del sistema
 * Debe ser llamada desde setup() en Core 1
 */
void visual_indicators_init() {
  speed_leds_init();
  display_7seg_init();
  
  DEBUG_SERIAL.println("Indicadores visuales inicializados correctamente");
}

/**
 * Actualiza todos los elementos visuales en coordinación
 * Debe llamarse periódicamente desde el loop de Core 1 (cada 50-100ms)
 * 
 * @param speed_kmh Velocidad actual en km/h
 */
void visual_indicators_update(float speed_kmh) {
  static uint32_t last_led_update = 0;
  uint32_t now = millis();
  
  // Actualizar LEDs según el intervalo configurado
  if ((now - last_led_update) >= LED_UPDATE_INTERVAL_MS) {
    speed_leds_update(speed_kmh);
    last_led_update = now;
  }
  
  // Actualizar el valor a mostrar en el display (limitar a 999)
  uint16_t display_speed = (uint16_t)constrain(speed_kmh, 0, 999);
  display_7seg_show_number(display_speed);
  
  // El refresh del display se hace en cada iteración para mantener
  // el multiplexado activo y evitar parpadeos
  display_7seg_refresh();
}
