//=== RegVel.ino ===
/*
 * RegVel Simplificado - Orquestador Final
 * - Core 0: Comunicaciones (Dinámico: GPS/BT/Serial)
 * - Core 1: Motor y Display (Tiempo Real)
 * - Depuración en tiempo real integrada
 */
#include "config.h"
#include "motor_control.h"
#include "display.h"
#include "bluetooth.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

// Cola de mensajes Inter-Core
QueueHandle_t q_ipc;

// =============================================================================
// TAREA NÚCLEO 0: COMUNICACIONES
// =============================================================================
void TaskCore0(void *pvParameters) {
    Serial.println("[Core0] Tarea Iniciada");
    
    IpcMsg msg;
    msg.limit_idx = 0; 
    msg.valid = false;

    for (;;) {
        uint8_t detected_limit = 0;

        // comm_update maneja TODO: Bluetooth, Serial, GPS y cambio de modo.
        // Retorna true si se detectó un cambio de límite válido.
        if (bluetooth_update(&detected_limit)) {
            
            // Preparar mensaje
            msg.limit_idx = detected_limit;
            msg.valid = true;
            
            // Enviar al Core 1 (Sobrescribir si hay uno pendiente)
            xQueueOverwrite(q_ipc, &msg);
            
            // (El debug de texto ya lo maneja comm_update internamente)
        }

        // Ceder tiempo al OS (Evita Watchdog Reset)
        vTaskDelay(pdMS_TO_TICKS(20)); 
    }
}

// =============================================================================
// SETUP (CORE 1)
// =============================================================================
void setup() {
    // Inicialización Serial para Debug
    Serial.begin(115200);
    delay(500); // Espera breve para estabilizar
    Serial.println("\n=== RegVel vFinal - Dynamic Mode ===");

    // Crear cola de comunicación
    q_ipc = xQueueCreate(1, sizeof(IpcMsg));
    if (q_ipc == NULL) {
        Serial.println("Error crítico: No se pudo crear la cola IPC");
        while(1); // Halt
    }

    // Inicializar Módulos de Hardware
    motor_control_init();
    disp_init();
    bluetooth_init(); // Inicia BT con nombre por defecto

    // Lanzar Tarea en Core 0
    xTaskCreatePinnedToCore(
        TaskCore0,    // Función
        "CommTask",   // Nombre
        8192,         // Stack (8KB para manejo seguro de Strings BT/GPS)
        NULL,         // Parámetros
        1,            // Prioridad
        NULL,         // Handle
        0             // Core ID
    );
    
    Serial.println("[System] Setup Completo. Iniciando Loop.");
}

// =============================================================================
// LOOP PRINCIPAL (CORE 1 - TIEMPO REAL)
// =============================================================================
void loop() {
    // 1. Actualizar Motor (Crítico: ADC, PWM, Override)
    motor_update();

    // 2. Refrescar Display (Crítico: Multiplexado)
    disp_mux();

    // 3. Verificar si el Core 0 mandó un nuevo límite
        // 3. Verificar si el Core 0 mandó un nuevo límite
    IpcMsg received;
    if (xQueueReceive(q_ipc, &received, 0) == pdTRUE) {
    motor_set_limit(received.limit_idx); // Esto inicia la animación LED automáticamente
    Serial.printf("[Core1] Nuevo Límite: %d\n", received.limit_idx);
}


    // Pequeño delay para estabilidad del WDT del Core 1
    delay(1); 
}