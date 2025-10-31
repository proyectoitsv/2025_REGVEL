/* ===========================================================================
   [PRESENTACIÓN] Implementacion_de_mapa_v2.ino - VERSIÓN ACTUALIZADA
   Archivo principal con indicadores visuales integrados (LEDs + Display 7seg)
   
   CAMBIOS PRINCIPALES:
   - Inicialización de LEDs de límite y display de 7 segmentos en Core 1
   - Actualización periódica de indicadores visuales basados en velocidad del motor
   - Cálculo de velocidad desde RPM del sensor Hall con factor de escala
   - Comandos de prueba extendidos para verificar displays y LEDs
   
   DISTRIBUCIÓN DE TAREAS:
   - Core 0: GPS, Bluetooth, mapa, decisiones de límite (sin cambios)
   - Core 1: Motor, ADC, sensor Hall, DISPLAY y LEDS (actualizado)
   ============================================================================= */

#include "config.h"
#include "control_IO.h"
#include "gestor_mapa.h"
#include "modulo_gps.h"
#include "control_velocidad.h"
#include "intercore_comm.h"

// Manejadores de tareas
TaskHandle_t gpsTaskHandle = NULL;
TaskHandle_t motorTaskHandle = NULL;

// Modo de test local
static bool test_mode = false;

// Prototipos de funciones de tareas
void gpsTask(void *pvParameters);
void motorTask(void *pvParameters);

void setup() {
  io_init();
  io_print_startup_message();

  // Inicializar comunicación entre núcleos
  if (!intercore_init()) {
    io_print_error("FATAL: No se pudo inicializar la comunicación entre núcleos. Deteniendo.");
    while(true);
  }

  // Inicializar LEDs indicadores originales
  leds_init();
  
  // Inicializar sensor Hall
  hall_init();

  // Crear tareas para cada núcleo
  xTaskCreatePinnedToCore(
    gpsTask,
    "GPSTask",
    GPS_TASK_STACK_SIZE,
    NULL,
    TASK_PRIORITY,
    &gpsTaskHandle,
    0  // Core 0
  );
  
  xTaskCreatePinnedToCore(
    motorTask,
    "MotorTask",
    MOTOR_TASK_STACK_SIZE,
    NULL,
    TASK_PRIORITY,
    &motorTaskHandle,
    1  // Core 1
  );
  
  DEBUG_SERIAL.println("\n=== Sistema RegVel Iniciado ===");
  DEBUG_SERIAL.println("Comandos disponibles:");
  DEBUG_SERIAL.println("  t - Alternar modo test");
  DEBUG_SERIAL.println("  p - Imprimir estado del acelerador");
  DEBUG_SERIAL.println("  v - Imprimir velocidad actual");
  DEBUG_SERIAL.println("  l - Test de LEDs de límite");
  DEBUG_SERIAL.println("  d - Test del display 7 segmentos");
  DEBUG_SERIAL.println("  s - Estado completo del sistema");
}

void loop() {
  // El loop principal está vacío ya que todo se maneja en las tareas
  // delay(1000);
}

// ========================= TAREA CORE 0: GPS Y MAPA =========================
void gpsTask(void *pvParameters) {
  DEBUG_SERIAL.println("[Core 0] Iniciando tarea GPS...");
  
  // Inicializar GPS y Bluetooth
  gps_init();
  
  // Inicializar mapa
  if (!map_init()) {
    io_print_error("FATAL: No se pudo cargar el mapa. Deteniendo.");
    vTaskDelete(NULL);
    return;
  }
  
  DEBUG_SERIAL.println("[Core 0] GPS y mapa inicializados correctamente");
  
  static uint32_t lastReportMs = 0;
  
  while(1) {
    // Actualizar parser GPS
    gps_feed_parser();
    
    // Manejo de comandos por Serial
    if (DEBUG_SERIAL.available()) {
      char c = (char)DEBUG_SERIAL.read();
      
      switch(c) {
        case 't':
          test_mode = !test_mode;
          DEBUG_SERIAL.printf("Test mode %s\n", test_mode ? "ON" : "OFF");
          break;
          
        case 'p':
          io_accelerator_print_debug();
          break;
          
        case 'v':
          DEBUG_SERIAL.printf("Velocidad actual: %.1f km/h (RPM: %.1f)\n", 
                            get_current_speed_kmh(), hall_get_rpm());
          break;
          
        case 's':
          DEBUG_SERIAL.println("\n=== ESTADO DEL SISTEMA ===");
          DEBUG_SERIAL.printf("Modo test: %s\n", test_mode ? "ON" : "OFF");
          DEBUG_SERIAL.printf("Velocidad: %.1f km/h\n", get_current_speed_kmh());
          DEBUG_SERIAL.printf("RPM: %.1f\n", hall_get_rpm());
          io_accelerator_print_debug();
          gps_print_bluetooth_status();
          break;
      }
    }
    
    // Si estamos en modo test, esperar y continuar
    if (test_mode) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    
    // Procesar la lógica principal aproximadamente una vez por segundo
    uint32_t now = millis();
    if (now - lastReportMs < 1000) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    lastReportMs = now;
    
    // Obtener datos del GPS
    GpsFix fix;
    if (!gps_get_fix(fix)) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    
    // Validar calidad de la señal
    if (fix.hdop > HDOP_MAX || !isfinite(fix.lat) || !isfinite(fix.lon)) {
      io_print_message("Fix pobre/inválido -> esperando mejora...");
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    
    // Procesar velocidad y rumbo
    process_speed_and_course(fix);
    
    // Convertir LLA a coordenadas locales
    float x, y;
    map_deg_to_meters(fix.lat, fix.lon, x, y);
    
    // Buscar el punto correspondiente en el mapa
    MatchResult match = find_match(x, y, fix.courseDeg);
    
    // Evaluar y reportar el estado
    if (!match.found) {
      io_print_no_match(fix);
      reset_alert_logic();
      
      // Enviar mensaje a Core 1 indicando que estamos fuera de zona
      InterCoreMsg msg;
      msg.inside_zone = false;
      msg.speed_limit = 0;
      msg.valid = true;
      intercore_send_msg(msg);
    } else {
      evaluate_and_report_status(fix, match);
    }
  }
}

// ========================= TAREA CORE 1: MOTOR Y VISUALIZACIÓN =========================
void motorTask(void *pvParameters) {
  DEBUG_SERIAL.println("[Core 1] Iniciando tarea de motor y visualización...");
  
  // Inicializar acelerador (ADC + PWM)
  io_accelerator_init();
  
  // [NUEVO] Inicializar indicadores visuales (LEDs + Display)
  visual_indicators_init();
  
  DEBUG_SERIAL.println("[Core 1] Motor y visualización inicializados correctamente");
  
  // Variables para el bucle principal
  InterCoreMsg lastMsg = { false, 0, false };
  uint32_t last_visual_update = 0;
  
  // Variables para el test de LEDs y display
  static bool led_test_mode = false;
  static bool display_test_mode = false;
  static uint16_t test_speed = 0;
  
  while(1) {
    // Actualizar PWM del acelerador con alta frecuencia
    accel_update_pwm();
    
    // [NUEVO] Refrescar display continuamente para evitar parpadeos
    // Esta función es muy rápida (solo cambia un dígito por llamada)
    display_7seg_refresh();
    
    // Comprobar mensajes de Core 0
    InterCoreMsg msg;
    if (intercore_receive_msg(msg, pdMS_TO_TICKS(10))) {
      if (msg.valid) {
        apply_speed_limit(msg);
        lastMsg = msg;
      }
    }
    
    // [NUEVO] Obtener velocidad actual calculada desde el sensor Hall
    float current_speed = get_current_speed_kmh();
    
    // [NUEVO] Actualizar indicadores visuales periódicamente
    uint32_t now = millis();
    if ((now - last_visual_update) >= 100) {  // Cada 100ms
      if (!led_test_mode && !display_test_mode) {
        // Modo normal: actualizar con velocidad real
        visual_indicators_update(current_speed);
      }
      last_visual_update = now;
    }
    
    // Manejo de comandos de prueba desde Serial
    if (DEBUG_SERIAL.available()) {
      char c = (char)DEBUG_SERIAL.read();
      
      switch(c) {
        case 'l':  // Test de LEDs
          led_test_mode = !led_test_mode;
          if (led_test_mode) {
            DEBUG_SERIAL.println("=== TEST DE LEDS INICIADO ===");
            DEBUG_SERIAL.println("Los LEDs se encenderán secuencialmente...");
            // Secuencia de prueba
            for(int i = 0; i <= 150; i += 10) {
              speed_leds_update((float)i);
              DEBUG_SERIAL.printf("Velocidad simulada: %d km/h\n", i);
              vTaskDelay(pdMS_TO_TICKS(1000));
            }
            speed_leds_off();
            DEBUG_SERIAL.println("=== TEST DE LEDS FINALIZADO ===");
            led_test_mode = false;
          }
          break;
          
        case 'd':  // Test del display
          display_test_mode = !display_test_mode;
          if (display_test_mode) {
            DEBUG_SERIAL.println("=== TEST DE DISPLAY INICIADO ===");
            DEBUG_SERIAL.println("Contando de 0 a 999...");
            for(uint16_t i = 0; i <= 999; i += 10) {
              i = 0;
              display_7seg_show_number(i);
              DEBUG_SERIAL.printf("Display: %03d\n", i);
              // Mantener el multiplexado activo durante la espera
              for(int j = 0; j < 20; j++) {
                display_7seg_refresh();
                vTaskDelay(pdMS_TO_TICKS(5));
              }
            }
            display_7seg_clear();
            DEBUG_SERIAL.println("=== TEST DE DISPLAY FINALIZADO ===");
            display_test_mode = false;
          }
          break;
          case 'e':
                  
          // Depuración periódica (cada 500 ms)
          static uint32_t lastDbg = 0;
          if ((now - lastDbg) >= 500) {
            DEBUG_SERIAL.printf("[MOTOR] RPM=%.1f, Velocidad=%.1f km/h, Zona=%s, Límite=%u km/h\n", 
                                hall_get_rpm(),
                                current_speed,
                                lastMsg.inside_zone ? "DENTRO" : "FUERA", 
                                lastMsg.speed_limit);
            lastDbg = now;
          }
          default:
          break;
      }
    }
        
    // Pequeña pausa para no sobrecargar el CPU
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
