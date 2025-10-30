/* ===========================================================================
   [PRESENTACIÓN] control_velocidad.h
   Encabezado del módulo que gestiona la lógica de velocidad y alertas.
   Contiene prototipos para:
     - process_speed_and_course(...)
     - evaluate_and_report_status(...)
     - reset_alert_logic()
   Integración con acelerador/PWM:
     - Si deseas cortar o limitar el PWM por alerta, aquí es el lugar lógico para llamar
       a una función que fuerce target_duty = MIN_DUTY_PERCENT o una futura accel_disable().
   Prevención:
     - Evitar accesos directos a variables internas de control_IO; usar funciones públicas/wrappers.
   ============================================================================= */

#ifndef CONTROL_VELOCIDAD_H
#define CONTROL_VELOCIDAD_H

#include <Arduino.h>
#include "modulo_gps.h" // Para GpsFix
#include "intercore_comm.h" // [AGREGADO] Para comunicación entre núcleos

// Estructura para el resultado de la búsqueda en el mapa
struct MatchResult {
  bool found;
  int16_t id;
  uint16_t speed_kph;
  float distance_m;
};

void process_speed_and_course(GpsFix& fix);
MatchResult find_match(float curX, float curY, float courseDeg);
void evaluate_and_report_status(const GpsFix& fix, const MatchResult& match);
void reset_alert_logic();

// [AGREGADO] Función para enviar mensaje entre núcleos con la decisión de límite
void send_speed_limit_decision(const GpsFix& fix, const MatchResult& match);

#endif // CONTROL_VELOCIDAD_H