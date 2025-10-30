/* ===========================================================================
   [PRESENTACIÓN] intercore_comm.cpp
   Implementación del módulo de comunicación entre núcleos.
   ============================================================================= */

#include "intercore_comm.h"
#include "config.h"

// [AGREGADO] Cola para comunicación entre núcleos
static QueueHandle_t intercore_queue = NULL;

// [AGREGADO] Inicializa la comunicación entre núcleos
bool intercore_init() {
  intercore_queue = xQueueCreate(INTERCORE_QUEUE_SIZE, sizeof(InterCoreMsg));
  if (intercore_queue == NULL) {
    DEBUG_SERIAL.println("Error al crear la cola entre núcleos");
    return false;
  }
  return true;
}

// [AGREGADO] Envía un mensaje a la cola entre núcleos
bool intercore_send_msg(const InterCoreMsg& msg) {
  if (intercore_queue == NULL) {
    return false;
  }
  return xQueueSend(intercore_queue, &msg, pdMS_TO_TICKS(10)) == pdPASS;
}

// [AGREGADO] Recibe un mensaje de la cola entre núcleos
bool intercore_receive_msg(InterCoreMsg& msg, TickType_t timeout) {
  if (intercore_queue == NULL) {
    return false;
  }
  return xQueueReceive(intercore_queue, &msg, timeout) == pdPASS;
}