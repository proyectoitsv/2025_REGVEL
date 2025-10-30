/* ===========================================================================
   [PRESENTACIÓN] intercore_comm.h
   Módulo de comunicación entre núcleos para el sistema de límite de velocidad.
   Implementa:
     - Estructura de mensajes entre núcleos
     - Funciones para inicializar y usar la cola de comunicación
   Uso:
     - Core 0: envía decisiones de límite de velocidad a Core 1
     - Core 1: recibe y aplica las decisiones de límite de velocidad
   ============================================================================= */

#ifndef INTERCORE_COMM_H
#define INTERCORE_COMM_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

// [AGREGADO] Estructura para mensajes entre núcleos
typedef struct {
  bool inside_zone;        // Verdadero si está dentro de una zona con límite
  uint16_t speed_limit;    // Límite de velocidad actual (km/h)
  bool valid;              // Indica si el mensaje es válido
} InterCoreMsg;

// [AGREGADO] Funciones de comunicación entre núcleos
bool intercore_init();
bool intercore_send_msg(const InterCoreMsg& msg);
bool intercore_receive_msg(InterCoreMsg& msg, TickType_t timeout = pdMS_TO_TICKS(100));

#endif // INTERCORE_COMM_H