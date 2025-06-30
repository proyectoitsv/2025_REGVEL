# 2025_REGVEL
## Sistema de Limitación de Velocidad Adaptativa (REGVEL) 🚗

![Imagen GitHub](https://github.com/user-attachments/assets/28d50548-7fde-4362-b549-a6ad07e272d7)



## Introducción 📍
REGVEL es un sistema inteligente de limitación de velocidad vehicular que adapta automáticamente los límites de velocidad según la zona de circulación. Mediante la integración de sensores de velocidad y tecnología GPS, el sistema:

Detecta en tiempo real la ubicación del vehículo

Identifica el tipo de vía (calle, avenida, ruta o auto-pista)

Compara la velocidad actual con el límite permitido

Actúa para prevenir excesos de velocidad

### Límites de velocidad implementados:

🏙️ Calles: 40 km/h

🛣️ Avenidas: 60 km/h

🚧 Rutas: 110 km/h

🛣️ Auto-pistas: 130 km/h

### Características Clave 🔑
#### ___ Módulo _______ | ______ Tecnología __________ | ____ Función Principal

Posicionamiento __ | _ GPS _______________________ | __ Identificación precisa de ubicación y tipo de vía

Monitoreo ________ | _ Sensores de velocidad ____ | __ Medición en tiempo real de velocidad vehicular

Control ___________ | _ Unidad de procesamiento _ | __ Comparación velocidad actual vs. límite permitido

Interfaz ___________ | _ Pantalla + alertas __________ | __ Visualización de datos y alertas al conductor

### Estructura del Repositorio 📂

2025_REGVEL/

├── documentación/            # Carpeta principal de documentos

│   ├── anteproyecto/         # Documento inicial del proyecto

│   ├── diagramas/            # Diagramas técnicos

│   │   ├── bloques.md        # Diagrama de bloques del sistema

│   │   ├── flujo.png         # Diagrama de flujo del proceso

│   │   └── gantt.pdf         # Cronograma de implementación

│   ├── encuestas/            # Resultados de investigación

│   │   └── resultados.xlsx   # Datos estadísticos de encuestas

│   └── presentación/         # Materiales de presentación

│       └── regvel_slides.pptx

├── src/                      # Código fuente (si aplica)

├── LICENSE

└── README.md                 # Este archivo

### Documentación Técnica 📄

- Diagramas Disponibles

1. Diagrama de Bloques: Arquitectura del sistema

2. Diagrama de Flujo: Lógica de control de velocidad

3. Diagrama de Gantt: Cronograma de implementación

4. Diagramas Eléctricos: Conexiones de sensores y actuadores

- Resultados de Encuesta

a. Análisis de necesidades de conductores

b. Datos sobre excesos de velocidad por tipo de vía

c. Preferencias de interfaz de usuario

### Hardware:

- Módulo GPS (ej: U-blox NEO-6M)

- Sensor de velocidad (ej: Hall Effect Sensor)

- Unidad de procesamiento (Raspberry Pi/Arduino)

- Módulo de control del acelerador

### Flujo De Operación:

    graph TD

    A[Inicio] --> B[Lectura GPS]
    B --> C{Identificar tipo de vía}
    C -->|Calle| D[Límite: 40 km/h]
    C -->|Avenida| E[Límite: 60 km/h]
    C -->|Ruta| F[Límite: 110 km/h]
    C -->|Auto-pista| G[Límite: 130 km/h]
    D --> H[Lectura sensor velocidad]
    E --> H
    F --> H
    G --> H
    H --> I{Comparar velocidades}
    I -->|Velocidad > Límite| J[Activar limitador]
    I -->|Velocidad <= Límite| K[Mantener operación normal]
    J --> L[Registrar evento]
    K --> L
    L --> M[Actualizar pantalla]
    M --> B


Equipo REGVEL · Instituto Tecnológico Salesiano Villada · 2025
