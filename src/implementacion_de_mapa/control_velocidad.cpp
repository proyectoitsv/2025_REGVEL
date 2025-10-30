/* ===========================================================================
   [PRESENTACIÓN] control_velocidad.cpp
   Implementa la lógica que procesa velocidad (del GPS) y decide alertas.
   Funciones clave:
     - Filtrado de velocidad, debouncing de alertas, comparación con límites del mapa.
   Nota de integración:
     - Si se detecta una condición de seguridad (ej. exceso), se envía un mensaje
       a Core 1 para que limite el PWM.
   Prevención:
     - Evitar modificar directamente variables de hardware desde aquí; usar API de control_IO.
   ============================================================================= */

#include "control_velocidad.h"
#include "config.h"
#include "gestor_mapa.h"
#include "control_IO.h"
#include "intercore_comm.h" // [AGREGADO] Para comunicación entre núcleos

// Variables de estado para esta unidad
float g_speedBuffer[SPEED_SMA_N] = {0};
int   g_speedIndex = 0;
int   g_speedCount = 0;
float g_lastSpeedKph = 0;
bool  g_lastAlert = false;
int   g_alertStreak = 0;
float g_last_known_course = 0.0f;

// [AGREGADO] Variables para el estado de la zona
static bool last_inside_zone = false;
static uint16_t last_speed_limit = 0;

// Prototipos de funciones internas
float smoothSpeed(float v_kph);

void process_speed_and_course(GpsFix& fix) {
  if (!isfinite(fix.speedKph)) {
    fix.speedKph = 0.0f;
  }
  fix.speedKph = smoothSpeed(fix.speedKph);

  if (isfinite(fix.courseDeg)) {
    g_last_known_course = fix.courseDeg;
  } else {
    fix.courseDeg = g_last_known_course;
  }
}

MatchResult find_match(float curX, float curY, float courseDeg) {
  // [NOTA: Aquí está la lógica de búsqueda por radio y cono de avance]
  // [Puedes modificarla o simplificarla si solo necesitas búsqueda por radio]
  MatchResult best = { false, -1, 0, 1e9f };
  float bestAheadProj = 1e9f;
  bool hasWithinRadius = false;

  float h = courseDeg * DEG_TO_RAD;
  float ux = sinf(h), uy = cosf(h);

  for (size_t i = 0; i < g_pointCount; i++) {
    float dx = g_points[i].x - curX;
    float dy = g_points[i].y - curY;
    float d = hypotf(dx, dy);
    if (d <= MATCH_RADIUS_M) {
      if (!hasWithinRadius || d < best.distance_m) {
        best = { true, g_points[i].id, g_points[i].speed_kph, d };
        hasWithinRadius = true;
      }
    }
  }
  if (hasWithinRadius) return best;

  for (size_t i = 0; i < g_pointCount; i++) {
    float dx = g_points[i].x - curX;
    float dy = g_points[i].y - curY;
    float d = hypotf(dx, dy);
    if (d < 0.1f) d = 0.1f;
    float bearing = atan2f(dx, dy) * 180.0f / PI;
    if (bearing < 0) bearing += 360.0f;
    float adiff = fabsf(courseDeg - bearing);
    if (adiff > 180.0f) adiff = 360.0f - adiff;

    if (adiff <= CONE_ANGLE_DEG) {
      float proj = dx * ux + dy * uy;
      if (proj >= 0.0f && proj <= LOOKAHEAD_M) {
        if (!best.found || proj < bestAheadProj) {
          best = { true, g_points[i].id, g_points[i].speed_kph, d };
          bestAheadProj = proj;
        }
      }
    }
  }
  return best;
}

void evaluate_and_report_status(const GpsFix& fix, const MatchResult& match) {
  bool is_currently_exceeding = (fix.speedKph > (float)match.speed_kph + SPEED_TOL_KPH);

  if (is_currently_exceeding) {
    g_alertStreak++;
  } else {
    g_alertStreak = 0;
  }

  bool should_emit_alert = is_currently_exceeding && (g_alertStreak >= ALERT_DEBOUNCE_N);
  
  io_print_status(should_emit_alert, fix.speedKph, match);
  
  // [AGREGADO] Enviar decisión de límite a Core 1
  send_speed_limit_decision(fix, match);
}

void reset_alert_logic() {
  g_alertStreak = 0;
}

float smoothSpeed(float v_kph) {
  if (g_speedCount > 0) {
    if (fabsf(v_kph - g_lastSpeedKph) > SPEED_JUMP_REJ) {
      v_kph = g_lastSpeedKph; // Rechaza el salto
    }
  }
  g_speedBuffer[g_speedIndex] = v_kph;
  g_speedIndex = (g_speedIndex + 1) % SPEED_SMA_N;
  if (g_speedCount < SPEED_SMA_N) g_speedCount++;

  float sum = 0;
  for (int i = 0; i < g_speedCount; i++) sum += g_speedBuffer[i];
  
  float filtered = sum / g_speedCount;
  g_lastSpeedKph = filtered;
  return filtered;
}

// [AGREGADO] Función para enviar mensaje entre núcleos con la decisión de límite
void send_speed_limit_decision(const GpsFix& fix, const MatchResult& match) {
  bool inside_zone = match.found;
  uint16_t speed_limit = match.found ? match.speed_kph : 0;
  
  // Solo enviar si hay cambios
  if (inside_zone != last_inside_zone || speed_limit != last_speed_limit) {
    InterCoreMsg msg;
    msg.inside_zone = inside_zone;
    msg.speed_limit = speed_limit;
    msg.valid = true;
    
    if (intercore_send_msg(msg)) {
      DEBUG_SERIAL.printf("Enviado mensaje a Core 1: zona=%s, límite=%u km/h\n", 
                          inside_zone ? "DENTRO" : "FUERA", speed_limit);
      
      last_inside_zone = inside_zone;
      last_speed_limit = speed_limit;
    } else {
      DEBUG_SERIAL.println("Error al enviar mensaje a Core 1");
    }
  }
}