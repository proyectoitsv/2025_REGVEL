/* ===========================================================================
   [PRESENTACIÓN] gestor_mapa.h
   Interfaz del gestor de mapa: carga, búsqueda de coincidencias y utilidades de coordenadas.
   Contiene:
     - map_init(), find_match(), map_deg_to_meters(), etc.
   Notas importantes:
     - Ajustar MAP_MAX_POINTS en config.h si el mapa no cabe en RAM.
     - Si usas SD/PROGMEM asegúrate de que MAP_FILENAME y USE_SD_CARD estén correctos.
   Prevención:
     - No llamar a find_match() sin haber completado map_init().
   ============================================================================= */

#ifndef GESTOR_MAPA_H
#define GESTOR_MAPA_H

#include <Arduino.h>

// Estructura de datos para un punto del mapa en coordenadas geográficas
struct MapPointLLA {
  int16_t id;
  double lat;
  double lon;
  uint16_t speed_kph;
};

// Estructura de datos para un punto del mapa en coordenadas locales (metros)
struct MapPointLocal {
  int16_t id;
  float x;     // metros Este
  float y;     // metros Norte
  uint16_t speed_kph;
};

// Acceso externo a los puntos del mapa ya procesados
extern MapPointLocal* g_points;
extern size_t g_pointCount;

bool map_init();
void map_deg_to_meters(double lat, double lon, float& x, float& y);

#endif // GESTOR_MAPA_H