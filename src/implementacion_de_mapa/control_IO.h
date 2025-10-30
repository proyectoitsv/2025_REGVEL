/* ===========================================================================
   [PRESENTACIÓN] control_IO.h - VERSIÓN ACTUALIZADA
   Interface pública para E/S generales y el módulo acelerador (ADC→PWM).
   
   NUEVAS FUNCIONES AGREGADAS:
   - LEDs de límite de velocidad (4 umbrales configurables)
   - Display de 7 segmentos triple con CD4511BE
   - Conversión RPM a velocidad con factor de escala
   
   ESTRUCTURA DEL MÓDULO:
   1. API General (Serial, prints, mensajes)
   2. API Acelerador (ADC, PWM, ramping)
   3. API Sensor Hall (RPM, conteo de pulsos)
   4. API LEDs Originales (límite/OK del sistema GPS)
   5. [NUEVO] API LEDs de Límite de Velocidad (4 umbrales)
   6. [NUEVO] API Display 7 Segmentos (multiplexado con CD4511BE)
   7. [NUEVO] API Conversión de Velocidad (RPM → km/h)
   8. [NUEVO] API Integración Visual (inicialización y actualización completa)
   9. API Límite de Velocidad (aplicación desde Core 0)
   
   INTEGRACIÓN CON EL SISTEMA:
   - Llamar desde Core 1 (motorTask)
   - No bloqueante (compatible con FreeRTOS)
   - Actualización periódica coordinada
   ============================================================================= */

#ifndef CONTROL_IO_H
#define CONTROL_IO_H

#include <Arduino.h>
#include "gestor_mapa.h"
#include "control_velocidad.h"
#include "config.h"
#include "intercore_comm.h"

// ========================= 1. API GENERAL (I/O) =========================
/**
 * @brief Inicializa comunicación Serial para debug
 * Debe ser la primera función llamada en setup()
 */
void io_init();

/**
 * @brief Imprime mensaje de inicio del sistema
 */
void io_print_startup_message();

/**
 * @brief Imprime resumen del mapa cargado
 * @param point_count Número de puntos en el mapa
 * @param lat0 Latitud del centro del mapa
 * @param lon0 Longitud del centro del mapa
 */
void io_print_map_summary(size_t point_count, double lat0, double lon0);

/**
 * @brief Imprime mensaje de error
 * @param message Texto del error
 */
void io_print_error(const char* message);

/**
 * @brief Imprime mensaje informativo general
 * @param message Texto del mensaje
 */
void io_print_message(const char* message);

/**
 * @brief Imprime estado cuando no hay coincidencia en el mapa
 * @param fix Datos del GPS actual
 */
void io_print_no_match(const GpsFix& fix);

/**
 * @brief Imprime estado de velocidad vs límite
 * @param is_alert true si hay exceso de velocidad
 * @param filtered_speed Velocidad filtrada actual
 * @param match Resultado del match con el mapa
 */
void io_print_status(bool is_alert, float filtered_speed, const MatchResult& match);

// ========================= 2. API ACELERADOR (ADC → PWM) =========================
/**
 * @brief Inicializa el módulo acelerador (ADC + LEDC/PWM)
 * Configura:
 * - LEDC timer y canal para PWM
 * - ADC pin con atenuación
 * - Estado inicial del sistema
 * 
 * Debe ser llamado en setup() antes de usar otras funciones del acelerador
 */
void io_accelerator_init();

/**
 * @brief Lee el pedal, filtra la señal y actualiza el ciclo de trabajo del motor
 * Esta función realiza:
 *  - Lectura ADC con protección contra valores inválidos
 *  - Suavizado por EMA (Exponential Moving Average)
 *  - Cálculo de duty objetivo (MIN_DUTY_PERCENT..MAX_DUTY_PERCENT)
 *  - Rampa segura hacia el objetivo
 *  - Actualización del LEDC/PWM
 *  - Aplicación de límite de velocidad si está activo
 *
 * Debe llamarse frecuentemente (cada 10-50 ms) desde loop() de Core 1
 */
void io_update_motor_from_pedal();

/**
 * @brief Wrapper corto para inicializar acelerador
 * Equivalente a io_accelerator_init()
 */
void accel_init();

/**
 * @brief Devuelve lectura filtrada del pedal normalizada (0.0 .. 1.0)
 * @return Valor normalizado del ADC (0.0 = pedal suelto, 1.0 = pedal a fondo)
 */
float accel_read_norm();

/**
 * @brief Actualiza el PWM del acelerador (wrapper)
 * Equivalente a io_update_motor_from_pedal()
 */
void accel_update_pwm();

/**
 * @brief Imprime estado completo del acelerador para debug
 * Muestra:
 * - Timestamp
 * - ADC raw y filtrado
 * - Voltaje calculado
 * - Normalizado (0-1)
 * - Target duty y current duty
 * - Duty value actual del LEDC
 */
void io_accelerator_print_debug();

// ========================= 3. API SENSOR HALL (RPM) =========================
/**
 * @brief Inicializa el sensor Hall
 * Configura:
 * - Pin como INPUT_PULLUP
 * - Interrupción en flanco de bajada (FALLING)
 * - Variables de conteo y timing
 */
void hall_init();

/**
 * @brief ISR (Interrupt Service Routine) del sensor Hall
 * Incrementa el contador de pulsos
 * NOTA: Esta función es llamada automáticamente por la interrupción
 */
void hall_isr();

/**
 * @brief Obtiene el número total de pulsos contados
 * @return Conteo acumulado de pulsos
 */
uint32_t hall_get_pulse_count();

/**
 * @brief Calcula y devuelve las RPM actuales del motor
 * @return RPM calculadas desde los pulsos del sensor
 * 
 * Actualiza el cálculo cada 1 segundo basado en:
 * RPM = (pulsos × 60) / (tiempo_en_segundos × pulsos_por_revolución)
 */
float hall_get_rpm();

/**
 * @brief Reinicia el contador de pulsos del Hall
 * Útil para sincronizar mediciones o después de detección de error
 */
void hall_reset_pulse_count();

// ========================= 4. API LEDS ORIGINALES (GPS/Sistema) =========================
/**
 * @brief Inicializa los LEDs indicadores originales del sistema GPS
 * Configura:
 * - LED_LIMIT_PIN (indica límite activo desde GPS)
 * - LED_OK_PIN (indica estado OK del sistema)
 */
void leds_init();

/**
 * @brief Controla el LED de límite del sistema GPS
 * @param state true = encendido, false = apagado
 */
void led_limit_set(bool state);

/**
 * @brief Controla el LED de estado OK
 * @param state true = encendido, false = apagado
 */
void led_ok_set(bool state);

// ========================= 5. [NUEVO] API LEDS DE LÍMITE DE VELOCIDAD =========================
/**
 * @brief Inicializa los 4 LEDs indicadores de límite de velocidad
 * Configura los pines como salidas digitales y los apaga
 * 
 * LEDs configurados:
 * - LED_LIM1_PIN (GPIO 26) → Umbral LED_LIM1_THRESHOLD (40 km/h por defecto)
 * - LED_LIM2_PIN (GPIO 25) → Umbral LED_LIM2_THRESHOLD (60 km/h por defecto)
 * - LED_LIM3_PIN (GPIO 33) → Umbral LED_LIM3_THRESHOLD (110 km/h por defecto)
 * - LED_LIM4_PIN (GPIO 32) → Umbral LED_LIM4_THRESHOLD (130 km/h por defecto)
 * 
 * Los umbrales son configurables en config.h
 */
void speed_leds_init();

/**
 * @brief Actualiza el estado de los LEDs según la velocidad actual
 * @param speed_kmh Velocidad actual en km/h
 * 
 * Lógica de encendido:
 * - Los LEDs se encienden progresivamente al superar cada umbral
 * - LED1: speed >= LED_LIM1_THRESHOLD
 * - LED2: speed >= LED_LIM2_THRESHOLD
 * - LED3: speed >= LED_LIM3_THRESHOLD
 * - LED4: speed >= LED_LIM4_THRESHOLD
 * 
 * Ejemplo: A 75 km/h se encienden LED1 (40) y LED2 (60)
 * 
 * Debe llamarse periódicamente (cada 100ms recomendado)
 */
void speed_leds_update(float speed_kmh);

/**
 * @brief Apaga todos los LEDs de límite
 * Útil para:
 * - Reset del sistema
 * - Modo de prueba
 * - Condición de error o seguridad
 */
void speed_leds_off();

// ========================= 6. [NUEVO] API DISPLAY 7 SEGMENTOS =========================
/**
 * @brief Inicializa el display de 7 segmentos triple con CD4511BE
 * Configura:
 * - Pines BCD (4 bits) como salidas → CD4511BE
 * - Pines de control de dígitos (multiplexado) como salidas
 * - Estado inicial: todos los dígitos desactivados
 * - Display limpio (muestra 000)
 * 
 * Pines BCD (LSB a MSB):
 * - DISP_BCD_BIT1_PIN (GPIO 4)  → D0 del CD4511BE
 * - DISP_BCD_BIT2_PIN (GPIO 19) → D1 del CD4511BE
 * - DISP_BCD_BIT3_PIN (GPIO 17) → D2 del CD4511BE
 * - DISP_BCD_BIT4_PIN (GPIO 16) → D3 del CD4511BE
 * 
 * Pines de dígitos (multiplexado):
 * - DISP_DIGIT1_PIN (GPIO 21) → Display 1 (Centenas)
 * - DISP_DIGIT2_PIN (GPIO 22) → Display 2 (Decenas)
 * - DISP_DIGIT3_PIN (GPIO 23) → Display 3 (Unidades)
 */
void display_7seg_init();

/**
 * @brief Establece el número a mostrar en el display (0-999)
 * @param value Valor a mostrar (será limitado a 999 si es mayor)
 * 
 * Esta función solo actualiza el valor interno. Para que el número
 * sea visible, display_7seg_refresh() debe llamarse periódicamente.
 * 
 * Internamente descompone el número en:
 * - Centenas (dígito 1)
 * - Decenas (dígito 2)
 * - Unidades (dígito 3)
 * 
 * Ejemplo: show_number(123)
 * - Dígito 1 (centenas) = 1
 * - Dígito 2 (decenas)  = 2
 * - Dígito 3 (unidades) = 3
 */
void display_7seg_show_number(uint16_t value);

/**
 * @brief Actualiza el display durante un ciclo de multiplexado
 * 
 * IMPORTANTE: Esta función debe llamarse con ALTA FRECUENCIA (cada 10-20ms)
 * desde el loop principal de Core 1 para mantener el display visible
 * sin parpadeo.
 * 
 * Funcionamiento del multiplexado:
 * 1. Desactiva todos los dígitos (evita ghosting)
 * 2. Escribe el valor BCD del dígito actual al CD4511BE
 * 3. Activa solo el dígito actual
 * 4. Espera DISP_MULTIPLEX_DELAY_US microsegundos
 * 5. Pasa al siguiente dígito
 * 
 * El ciclo completo (3 dígitos) toma:
 * 3 × DISP_MULTIPLEX_DELAY_US = 6ms (166Hz refresh rate)
 * 
 * La persistencia visual del ojo humano hace que veamos todos
 * los dígitos como si estuvieran encendidos simultáneamente.
 * 
 * NOTA: Es una función no bloqueante (no usa delay())
 */
void display_7seg_refresh();

/**
 * @brief Limpia el display (muestra 000)
 * Equivalente a display_7seg_show_number(0)
 */
void display_7seg_clear();

// ========================= 7. [NUEVO] API CONVERSIÓN DE VELOCIDAD =========================
/**
 * @brief Convierte RPM del motor a velocidad en km/h
 * @param rpm Revoluciones por minuto del motor
 * @return Velocidad calculada en km/h (ya escalada con SPEED_MULTIPLIER_FACTOR)
 * 
 * Fórmula completa aplicada:
 * 
 * 1. Velocidad lineal (m/s):
 *    v = (RPM × π × WHEEL_DIAMETER_M × GEAR_RATIO) / 60
 * 
 * 2. Conversión a km/h:
 *    v_kmh = v × 3.6
 * 
 * 3. Factor de escala (para simulación):
 *    v_display = v_kmh × SPEED_MULTIPLIER_FACTOR
 * 
 * Parámetros configurables en config.h:
 * - WHEEL_DIAMETER_M: Diámetro de la rueda en metros
 * - GEAR_RATIO: Relación de transmisión (1.0 si es directa)
 * - SPEED_MULTIPLIER_FACTOR: Factor de escala para simulación
 * - MIN_DISPLAY_SPEED_KMH: Umbral mínimo (devuelve 0 si está por debajo)
 * 
 * Ejemplo de cálculo:
 * RPM = 300, WHEEL_DIAMETER = 0.66m, GEAR_RATIO = 1.0, FACTOR = 3.5
 * v = (300 × π × 0.66 × 1.0) / 60 = 10.4 m/s = 37.4 km/h
 * v_display = 37.4 × 3.5 = 130.9 km/h
 */
float rpm_to_speed_kmh(float rpm);

/**
 * @brief Obtiene la velocidad actual calculada desde el sensor Hall
 * @return Velocidad en km/h (ya escalada con SPEED_MULTIPLIER_FACTOR)
 * 
 * Esta función es un wrapper conveniente que:
 * 1. Lee las RPM del sensor Hall (hall_get_rpm())
 * 2. Convierte a velocidad (rpm_to_speed_kmh())
 * 3. Aplica caché interno (solo recalcula cada SPEED_CALC_INTERVAL_MS)
 * 
 * Ventajas del caché:
 * - Evita cálculos innecesarios
 * - Reduce carga del CPU
 * - Proporciona valor estable entre actualizaciones
 * 
 * Intervalo de actualización: SPEED_CALC_INTERVAL_MS (200ms por defecto)
 */
float get_current_speed_kmh();

// ========================= 8. [NUEVO] API INTEGRACIÓN VISUAL =========================
/**
 * @brief Inicializa todos los elementos visuales en un solo llamado
 * 
 * Llama internamente a:
 * - speed_leds_init()
 * - display_7seg_init()
 * 
 * Debe ser llamada desde setup() de Core 1 (motorTask)
 * después de inicializar el acelerador y el sensor Hall.
 * 
 * Orden de inicialización recomendado en motorTask:
 * 1. io_accelerator_init()
 * 2. hall_init()
 * 3. visual_indicators_init()  ← Esta función
 */
void visual_indicators_init();

/**
 * @brief Actualiza todos los elementos visuales de forma coordinada
 * @param speed_kmh Velocidad actual en km/h
 * 
 * Esta función coordina la actualización completa de:
 * - LEDs de límite (según umbrales configurados)
 * - Display de 7 segmentos (muestra la velocidad)
 * 
 * Timing interno:
 * - LEDs: Se actualizan cada LED_UPDATE_INTERVAL_MS (100ms por defecto)
 * - Display: El valor se actualiza inmediatamente, pero el multiplexado
 *   continúa con display_7seg_refresh() que debe llamarse por separado
 * 
 * Debe llamarse periódicamente desde el loop de Core 1:
 * - Frecuencia recomendada: 50-100ms
 * - Más frecuente no mejora, solo consume CPU
 * 
 * Ejemplo de uso en motorTask:
 * ```cpp
 * float speed = get_current_speed_kmh();
 * visual_indicators_update(speed);  // Cada 100ms
 * display_7seg_refresh();            // Cada ciclo (~10ms)
 * ```
 * 
 * NOTA: Esta función NO incluye el refresh del display.
 * display_7seg_refresh() debe llamarse por separado en cada iteración
 * del loop para mantener el multiplexado activo.
 */
void visual_indicators_update(float speed_kmh);

// ========================= 9. API LÍMITE DE VELOCIDAD (desde Core 0) =========================
/**
 * @brief Aplica decisión de límite de velocidad desde Core 0
 * @param msg Mensaje con información de zona y límite
 * 
 * Esta función recibe mensajes desde Core 0 (gpsTask) vía la cola
 * inter-core y aplica las restricciones correspondientes:
 * 
 * - Si inside_zone = true:
 *   - Limita el duty máximo del PWM según speed_limit
 *   - Enciende LED_LIMIT_PIN
 *   - Apaga LED_OK_PIN
 * 
 * - Si inside_zone = false:
 *   - Remueve la limitación del PWM
 *   - Apaga LED_LIMIT_PIN
 *   - Enciende LED_OK_PIN
 * 
 * NOTA: Esta función modifica el comportamiento del acelerador
 * (io_update_motor_from_pedal) pero NO afecta los indicadores visuales
 * nuevos (LEDs de límite y display). Esos se controlan independientemente
 * con visual_indicators_update().
 * 
 * Llamada automática desde motorTask al recibir mensaje de Core 0.
 */
void apply_speed_limit(const InterCoreMsg& msg);

#endif // CONTROL_IO_H