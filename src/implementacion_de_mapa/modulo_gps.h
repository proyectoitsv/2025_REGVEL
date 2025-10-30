/* ===========================================================================
   [PRESENTACIÓN] modulo_gps.h
   Interfaz para inicialización y lectura del módulo GPS vía Bluetooth.
   Contiene:
     - gps_init(): Inicializa Bluetooth Serial y UART
     - gps_feed_parser(): Lee datos NMEA y alimenta el parser
     - gps_get_fix(): Obtiene fix válido del GPS
     - gps_is_bluetooth_connected(): Estado de conexión BT
     - gps_print_bluetooth_status(): Imprime estado para debug
   Configuración:
     - GPS_SERIAL, GPS_RX_PIN/GPS_TX_PIN y GPS_BAUD en config.h
   ============================================================================= */

#ifndef MODULO_GPS_H
#define MODULO_GPS_H

#include <Arduino.h>

// Estructura para devolver una lectura completa y válida del GPS
struct GpsFix {
  double lat;
  double lon;
  float speedKph;
  float courseDeg;
  float hdop;
};

// Funciones principales
void gps_init();
void gps_feed_parser();
bool gps_get_fix(GpsFix& fix);

// Funciones de utilidad/debug
bool gps_is_bluetooth_connected();
void gps_print_bluetooth_status();
void gps_enable_raw_debug(bool enable);

#endif // MODULO_GPS_H