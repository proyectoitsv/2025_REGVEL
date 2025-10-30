/* ===========================================================================
   [PRESENTACIÓN] config.h - VERSIÓN ACTUALIZADA CON DISPLAYS Y LEDS
   Archivo de configuración central para todo el proyecto RegVel.
   
   NUEVAS SECCIONES AGREGADAS:
   - Configuración de LEDs indicadores de límite (4 LEDs)
   - Configuración de Display de 7 segmentos triple con CD4511BE
   - Parámetros de conversión RPM a velocidad
   ============================================================================= */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

// ========================= Configuración General =========================

// [Bluetooth - Configuración del ESP32 como ESCLAVO]
#define BT_DEVICE_NAME   ESP32_GPS_Slave

// [E/S Serial para Depuración]
#define DEBUG_SERIAL     Serial
#define DEBUG_BAUD       115200

// [Selección de fuente de mapa]
#define USE_PROGMEM_MAP  1
#define USE_LITTLEFS     0

// [Archivo mapa si no es PROGMEM]
#define MAP_FILENAME     "/clea_mapa_cole.csv"

// [Límites del algoritmo]
#define MATCH_RADIUS_M   5.5f
#define LOOKAHEAD_M      12.0f
#define CONE_ANGLE_DEG   30.0f
#define SPEED_TOL_KPH    2.0f
#define SPEED_SMA_N      3
#define SPEED_JUMP_REJ   25.0f
#define HDOP_MAX         3.0f
#define ALERT_DEBOUNCE_N 2

// [Tamaño máximo (si se carga a RAM)]
#define MAP_MAX_POINTS   12000

// [Simulación opcional por Serial]
#define SIMULATE_INPUT   0

// ========================= CONFIGURACIÓN DEL ACELERADOR (ADC → PWM) =========================
#define PEDAL_ADC_PIN              35
#define MOTOR_PWM_PIN              18

// [Sensor Hall]
#define HALL_PIN                   34
#define HALL_PULSES_PER_REV        20

// [LEDs indicadores originales]
#define LED_LIMIT_PIN              2
#define LED_OK_PIN                 4

// ParÃ¡metros ADC (ESP32)
#define PEDAL_ADC_ATTENUATION      ADC_ATTEN_DB_11
#define ADC_MAX_READING            4095

// ParÃ¡metros PWM / LEDC
#define PWM_CHANNEL                0
#define PWM_TIMER                  0
#define PWM_FREQ_HZ                20000
#define PWM_RESOLUTION_BITS        10

// Duty limits (porcentaje)
#define MIN_DUTY_PERCENT           5.0f
#define MAX_DUTY_PERCENT           100.0f

// Ramping / slew rate
#define RAMP_RATE_PERCENT_PER_100MS 5.0f
#define RAMP_INTERVAL_MS            100

// Filtros y seguridad
#define PEDAL_EMA_ALPHA            0.10f
#define PEDAL_INVALID_READ_RETRY_MS 200

// [Configuración de tareas y comunicación entre núcleos]
#define GPS_TASK_STACK_SIZE        4096
#define MOTOR_TASK_STACK_SIZE      4096
#define INTERCORE_QUEUE_SIZE       8
#define TASK_PRIORITY              5

// ========================= [NUEVO] CONFIGURACIÓN DE LEDS DE LÍMITE =========================
/* 
 * LEDs indicadores de umbrales de velocidad
 * Cada LED se enciende cuando la velocidad supera su umbral
 * Los LEDs permanecen encendidos mientras se mantenga por encima del umbral
 */
#define LED_LIM1_PIN               26      // LED límite 40 km/h
#define LED_LIM2_PIN               25      // LED límite 60 km/h
#define LED_LIM3_PIN               33      // LED límite 110 km/h
#define LED_LIM4_PIN               32      // LED límite 130 km/h

// Umbrales de velocidad para cada LED (km/h)
// Puedes ajustar estos valores según tus necesidades
#define LED_LIM1_THRESHOLD         40      // Umbral LED 1
#define LED_LIM2_THRESHOLD         60      // Umbral LED 2
#define LED_LIM3_THRESHOLD         110     // Umbral LED 3
#define LED_LIM4_THRESHOLD         130     // Umbral LED 4

// ========================= [NUEVO] CONFIGURACIÓN DEL DISPLAY 7 SEGMENTOS =========================
/*
 * Display de 7 segmentos triple con CD4511BE
 * El CD4511BE es un decodificador BCD a 7 segmentos
 * Entradas: 4 bits BCD (0-9)
 * Salidas: 7 segmentos (a-g)
 * El multiplexado se realiza por software activando cada dígito secuencialmente
 */

// Pines de salida BCD al CD4511BE (4 bits)
#define DISP_BCD_BIT1_PIN          4       // Bit menos significativo (LSB)
#define DISP_BCD_BIT2_PIN          19
#define DISP_BCD_BIT3_PIN          17
#define DISP_BCD_BIT4_PIN          16      // Bit más significativo (MSB)

// Pines de control de dígitos (multiplexado)
// Cada pin activa un dígito del display (común cátodo o común ánodo según tu hardware)
#define DISP_DIGIT1_PIN            21      // Dígito de las centenas
#define DISP_DIGIT2_PIN            22      // Dígito de las decenas
#define DISP_DIGIT3_PIN            23      // Dígito de las unidades

// Configuración del multiplexado
#define DISP_MULTIPLEX_DELAY_US    2000    // Microsegundos por dígito (2ms = ~166Hz refresh)
#define DISP_COMMON_ANODE          false   // true si es común ánodo, false si es común cátodo

// ========================= [NUEVO] CONVERSIÓN RPM A VELOCIDAD =========================
/*
 * Parámetros para convertir RPM del motor a velocidad en km/h
 * 
 * FÓRMULA BASE:
 * velocidad_real_kmh = (RPM * PI * DIAMETRO_RUEDA_M * 60) / 1000
 * 
 * Para simular velocidades más altas en el display:
 * velocidad_display_kmh = velocidad_real_kmh * SPEED_MULTIPLIER_FACTOR
 */

// Diámetro de la rueda en metros (ajustar según tu vehículo real o simulado)
// Ejemplo: rueda de 26" = 0.66m, rueda de 20" = 0.508m
#define WHEEL_DIAMETER_M           0.66f   

// Relación de transmisión (si hay reductora entre motor y rueda)
// Si el motor está directamente conectado a la rueda, usar 1.0
#define GEAR_RATIO                 1.0f    

// Factor multiplicador para simular velocidades más altas
// Ejemplo: con factor 10, un motor girando a 60 RPM mostraría ~24 km/h
// Ajusta este valor para que el rango de tu motor (ej: 0-300 RPM) 
// se mapee al rango deseado (0-130 km/h)
#define SPEED_MULTIPLIER_FACTOR    5.0f    

// Velocidad mínima para considerar el vehículo en movimiento (km/h)
// Por debajo de este valor, el display mostrará 0
#define MIN_DISPLAY_SPEED_KMH      1.0f    

// ========================= [NUEVO] CONFIGURACIÓN DE ACTUALIZACIÓN DE DISPLAYS =========================
/*
 * Frecuencia de actualización de los elementos visuales
 */
#define LED_UPDATE_INTERVAL_MS     100     // Actualizar LEDs cada 100ms
#define SPEED_CALC_INTERVAL_MS     200     // Calcular velocidad cada 200ms

#endif // CONFIG_H