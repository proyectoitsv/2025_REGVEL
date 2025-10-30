/* ===========================================================================
   [PRESENTACIÓN] gestor_mapa.cpp
   Implementación de carga y búsqueda en el mapa (coordenadas -> límites de velocidad).
   Aspectos a tener en cuenta:
     - Lectura de archivo CSV puede tardar; verificar manejo de errores al abrir archivo.
     - El algoritmo de búsqueda usa MATCH_RADIUS_M y otros parámetros en config.h.
   Prevención:
     - Validar que el archivo del mapa está en el sistema de archivos correcto (SPIFFS/LittleFS).
     - Probar con un mapa pequeño antes de cargar todo para evitar OOM.
   ============================================================================= */

#include "gestor_mapa.h"
#include "config.h"
#include "control_IO.h"
#include <FS.h>
#include <LittleFS.h>  // [MODIFICADO] Usar LittleFS en lugar de SPIFFS/SD

// Variables globales para el mapa, definidas aquí
MapPointLocal* g_points = nullptr;
size_t g_pointCount = 0;
double g_lat0 = NAN, g_lon0 = NAN;
double g_cosLat0 = 1.0;

// Prototipos de funciones internas
bool loadMap();
static inline void degToMeters(double lat, double lon, float &x, float &y);

// [MODIFICADO] Solo mantenemos las funciones para leer desde archivo CSV
bool parseCsvLine(const String &line, MapPointLLA &out) {
  int c1 = line.indexOf(','); if (c1 < 0) return false;
  int c2 = line.indexOf(',', c1 + 1); if (c2 < 0) return false;
  int c3 = line.indexOf(',', c2 + 1); if (c3 < 0) return false;
  String s_id  = line.substring(0, c1);
  String s_lat = line.substring(c1+1, c2);
  String s_lon = line.substring(c2+1, c3);
  String s_spd = line.substring(c3+1);
  out.id = (int16_t)s_id.toInt();
  out.lat = s_lat.toDouble();
  out.lon = s_lon.toDouble();
  out.speed_kph = (uint16_t)s_spd.toInt();
  return true;
}

bool computeCenterFromFile(fs::FS &fs, const char* path, size_t &count) {
  File f = fs.open(path, "r");
  if (!f) return false;
  String line;
  double sumLat=0, sumLon=0;
  size_t n=0;
  if (f.available()) {
    line = f.readStringUntil('\n');
    if (line.indexOf("lat") < 0) {
      MapPointLLA tmp;
      if (parseCsvLine(line, tmp)) { sumLat += tmp.lat; sumLon += tmp.lon; n++; }
    }
  }
  while (f.available()) {
    line = f.readStringUntil('\n');
    MapPointLLA tmp;
    if (parseCsvLine(line, tmp)) { sumLat += tmp.lat; sumLon += tmp.lon; n++; }
  }
  f.close();
  if (n == 0) return false;
  g_lat0 = sumLat / n;
  g_lon0 = sumLon / n;
  g_cosLat0 = cos(g_lat0 * DEG_TO_RAD);
  count = n;
  return true;
}

bool loadFileToLocal(fs::FS &fs, const char* path) {
  size_t n=0;
  if (!computeCenterFromFile(fs, path, n)) return false;
  if (n > MAP_MAX_POINTS) { io_print_error("Mapa demasiado grande para RAM"); return false; }
  g_points = (MapPointLocal*) heap_caps_malloc(n * sizeof(MapPointLocal), MALLOC_CAP_8BIT);
  if (!g_points) return false;
  File f = fs.open(path, "r");
  if (!f) return false;
  String line;
  size_t idx = 0;
  bool header_decided = false;
  while (f.available() && idx < n) {
    line = f.readStringUntil('\n');
    if (!header_decided) {
      header_decided = true;
      if (line.indexOf("lat") >= 0 && line.indexOf("lon") >= 0) continue;
    }
    MapPointLLA tmp;
    if (parseCsvLine(line, tmp)) {
      float x, y;
      degToMeters(tmp.lat, tmp.lon, x, y);
      g_points[idx++] = { tmp.id, x, y, tmp.speed_kph };
    }
  }
  f.close();
  g_pointCount = idx;
  return (g_pointCount > 0);
}

// --- Implementación de funciones públicas ---
void map_deg_to_meters(double lat, double lon, float& x, float& y) {
    degToMeters(lat, lon, x, y);
}

bool initFS() {
  // [MODIFICADO] Solo inicializamos LittleFS
  if (!LittleFS.begin(true)) { 
    io_print_error("Fallo LittleFS.begin()"); 
    return false; 
  }
  return true;
}

bool map_init() {
  if (!initFS()) {
    io_print_error("Error iniciando LittleFS. No se puede continuar.");
    return false;
  }
  
  if (!loadMap()) {
      return false;
  }
  io_print_map_summary(g_pointCount, g_lat0, g_lon0);
  return true;
}

// --- Implementación de funciones internas ---
static inline void degToMeters(double lat, double lon, float &x, float &y) {
  const double m_per_deg_lat = 111320.0;
  const double m_per_deg_lon = 111320.0 * g_cosLat0;
  y = (float)((lat - g_lat0) * m_per_deg_lat);
  x = (float)((lon - g_lon0) * m_per_deg_lon);
}

bool loadMap() {
  // [MODIFICADO] Solo cargamos desde LittleFS
  return loadFileToLocal(LittleFS, MAP_FILENAME);
}