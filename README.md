# RegVel — Regulador de Velocidad Automático 🚗💨

**Resumen breve**\
RegVel es un sistema inteligente diseñado para limitar automáticamente la velocidad de un vehículo según la zona de circulación (calle, avenida, ruta, autopista) con el objetivo de reducir accidentes de tránsito en Argentina.

---

## Tabla de contenidos

- [Visión general](#visión-general)
- [Problema y estadísticas](#problema-y-estadísticas)
- [Solución propuesta](#solución-propuesta)
- [Estructura del repositorio](#estructura-del-repositorio)
- [Requisitos de hardware y diseño del sistema](#requisitos-de-hardware-y-diseño-del-sistema)
- [Flujo de operación (resumen)](#flujo-de-operación-resumen)
- [Fases de implementación](#fases-de-implementación)
- [Desafíos y consideraciones](#desafíos-y-consideraciones)
- [Marco legal](#marco-legal)
- [Resultados de la encuesta](#resultados-de-la-encuesta)
- [Equipo](#equipo)
- [Documentación disponible](#documentación-disponible)
- [Contribuir](#contribuir)
- [Licencia](#licencia)

---

## Visión general

**Objetivo:** reducir accidentes por exceso de velocidad mediante un limitador automático que se adapta al tipo de vía detectado por GPS y otros sensores.

---

## Problema y estadísticas

- **Muertes (Argentina, 2023):** 4.486.
- **Porcentaje de siniestros por exceso de velocidad:** 31% (2023).
- **Regla de riesgo:** cada aumento de 1 km/h incrementa los accidentes en \~3%.

---

## Solución propuesta

RegVel utiliza posicionamiento por GPS junto con sensores de velocidad y un actuador para limitar la aceleración cuando la velocidad del vehículo supera el límite definido según el tipo de vía. Los límites considerados en el diseño son:

- Calle: **40 km/h**.
- Avenida: **60 km/h**.
- Ruta: **110 km/h**.
- Autopista: **130 km/h**.

---

## Estructura del repositorio

```plaintext
RegVel/
├── docs/
│   ├── PROYECTO_REG_VEL.pdf
│   ├── REGVEL_Requisitos.pdf
│   └── Encuesta.pdf
├── diagrams/
│   ├── Diagrama_de_Bloques.pdf
│   ├── sequence_diagram.jpg
│   └── Gantt.xlsx
├── src/                # Código / VHDL (si disponible)
└── presentation/
    └── REGVEL.pptx
```

---

## Requisitos de hardware y diseño del sistema

**Componentes principales identificados:**

| Componente      | Tecnología / Ejemplo        | Funcín principal                                  |
| --------------- | --------------------------- | ------------------------------------------------- |
| Posicionamiento | GPS (ej. U-blox NEO-6M)     | Detectar ubicación y tipo de vía en tiempo real.  |
| Monitoreo       | Sensor de velocidad (Hall)  | Medir la velocidad actual del vehículo.           |
| Control         | Raspberry Pi / Arduino Mega | Comparar velocidad vs. límite, decidir acción.    |
| Actuador        | Controlador del acelerador  | Reducir la entrada de aceleración si corresponde. |
| Interfaz        | Pantalla y alertas sonoras  | Notificar al conductor; mostrar datos.            |

**Diagrama de bloques**\
En la documentación se incluye un diagrama de bloques y flujo que muestra: GPS → Comparador de Velocidad → Actuador.

---

## Flujo de operación (resumen)

1. Lectura periódica del GPS para determinar ubicación y tipo de vía.
2. Lectura del sensor de velocidad del vehículo.
3. Comparación: si `velocidad_actual > límite_zona` → activar limitador (actuador).
4. Registro del evento y notificación al conductor.

---

## Fases de implementación

1. **Piloto en Córdoba** — validar el sistema en condiciones reales y recoger datos de aceptación.
2. **Cumplimiento regulatorio** — adaptar el sistema a la Ley 24.449 y normativas locales.
3. **Escalabilidad** — asegurar compatibilidad con distintos modelos de vehículos, incluyendo vehículos antiguos.

---

## Desafíos y consideraciones

- **Precisión del GPS** en zonas urbanas con interferencias/sombrado urbano.
- **Ciberseguridad**: proteger la integración con la ECU y evitar manipulaciones maliciosas.
- **Casos especiales**: adelantamientos, control de crucero, situaciones de emergencia (mecanismo de inhibición disponible al usuario según diseño).

---

## Marco legal

- Ley de tránsito de referencia: **Ley 24.449** (se menciona como base para el diseño y cumplimiento normativo).
- Límites considerados: urbano 40 km/h, rural 110 km/h, autopista 130 km/h.

---

## Resultados de la encuesta

- **50%** de los conductores reportados exceden los límites de velocidad.
- **70%** de las víctimas fatales son hombres entre 15–34 años (según la muestra).

---

## Equipo

| Rol        | Nombre            |
| ---------- | ----------------- |
| Estudiante | Julian Homola     |
| Estudiante | Fabrizio Marrone  |
| Estudiante | Pedro Zamora      |
| Asesor     | Marcos Remedi     |
| Asesor     | Federico Ferraro  |
| Asesor     | Matias Schulthess |

---

## Documentación disponible

Carpeta `docs/` con los archivos principales:

- `PROYECTO_REG_VEL.pdf` — Informe completo (problema, estadísticas, marco legal).
- `REGVEL_Requisitos.pdf` — Requisitos y especificaciones del sistema.
- `Encuesta.pdf` — Datos y resultados de la encuesta.

Diagrama de bloques y flujo: ver `diagrams/Diagrama_de_Bloques.pdf`.

---

## Contribuir

Si deseas colaborar:

1. Abre un *issue* describiendo la mejora o fallo.
2. Crea un *fork* y envía un *pull request* con tu cambio (explica el objetivo y pruebas realizadas).

Nota: antes de implementar cambios en `src/` consulta los documentos de requisitos y el diagrama de bloques para mantener consistencia con el diseño propuesto.

---

## Licencia

No se especificó una licencia en la documentación entregada. Añade un archivo `LICENSE` al repositorio con la licencia preferida (por ejemplo MIT, Apache-2.0, etc.) o indica la política de uso/cesión de derechos.

---

## Notas finales

Este README resume y organiza la documentación base provista para el proyecto **RegVel**. Los documentos fuente (informes, diagramas y la presentación) están en las carpetas listadas arriba; revisa `docs/` y `diagrams/` para los detalles técnicos y anexos.

