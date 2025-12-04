//=== bluetooth.h ===
/*
 * Módulo de Comunicaciones (Core 0)
 * Maneja Bluetooth, GPS, Mapa y Cambio de Modo Dinámico
 */
#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include <Arduino.h>

// Inicializa Bluetooth
void bluetooth_init();

// Función principal del Core 0:
// - Gestiona conexión BT
// - Lee cambios de modo (BT y Serial)
// - Procesa GPS o Comandos según modo
// - Imprime debug cada 1s
bool bluetooth_update(uint8_t *detected_limit);

#endif