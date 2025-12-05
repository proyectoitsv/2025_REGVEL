//=== config.h ===
#ifndef CONFIG_H
#define CONFIG_H
#include <Arduino.h>

// =============================================================================
// 1. MODOS DE OPERACIÓN
// =============================================================================
#define MODE_FULL      1  // GPS + Bluetooth + Mapa
#define MODE_PARTIAL   2  // Solo Comandos Bluetooth (0-4)
#define MODE_NO_BT     3  // Solo Comandos Serial USB (0-4)

// Modo de arranque por defecto
#define DEFAULT_MODE   MODE_NO_BT 

// Comandos de Sistema (Serial / Bluetooth)
#define CMD_SYS_NO_BT   '7'
#define CMD_SYS_PARTIAL '8'
#define CMD_SYS_FULL    '9'

// =============================================================================
// 2. PINES Y HARDWARE
// =============================================================================
#define PIN_DISP_A      4
#define PIN_DISP_B      19
#define PIN_DISP_C      17
#define PIN_DISP_D      16
#define PIN_DIG_1       21
#define PIN_DIG_2       22
#define PIN_DIG_3       23
#define DISP_MUX_US     2000

#define PIN_HALL        15
#define PIN_PEDAL       35
#define PIN_PWM         18
#define PIN_LED_LIMIT   2

#define PIN_LED_L1      26
#define PIN_LED_L2      25
#define PIN_LED_L3      33
#define PIN_LED_L4      32

#define BT_NAME "RegVel_Admin"

// =============================================================================
// 3. PARÁMETROS DE CONTROL
// =============================================================================
#define PWM_FREQ        25000    //20000
#define PWM_CHANNEL     0
#define PWM_RES         8
#define PWM_MAX_DUTY    255
/*
#define PWM_FREQ        5000    
#define ADC_DBL_TAP_THR 3800
*/
#define ADC_MIN         0
#define ADC_MAX         2800
#define ADC_DBL_TAP_THR 2790  

#define TAP_MIN_MS      300   
#define TAP_MAX_MS      800   
#define TIME_OVR_L1     15
#define TIME_OVR_L2     10
#define TIME_OVR_L3     7
#define TIME_OVR_L4     5

#define SPEED_L1        40
#define SPEED_L2        60
#define SPEED_L3        110
#define SPEED_L4        130

// Límites de PWM (0-255) en Lógica Directa
// Ajustar experimentalmente:
#define PWM_LIM_1       120     // Muy Lento
#define PWM_LIM_2       100     // Lento
#define PWM_LIM_3       80     // Rápido
#define PWM_LIM_4       50      // Muy Rápido

// Frecuencias LED
#define BLINK_ENTRY_MS  250     // 2Hz (250ms on / 250ms off)
#define BLINK_OVR_MS    50      // 10Hz (50ms on / 50ms off)

// =============================================================================
// CONFIGURACIÓN BLUETOOTH MASTER (HC-05/HC-06)
// =============================================================================
// Reemplazar con la MAC de tu módulo HC-06
#define BT_TARGET_MAC     {0x00, 0x22, 0x09, 0x01, 0x2C, 0x9E} 
#define BT_MASTER_PIN     "1234"      // PIN por defecto del HC-06
#define BT_RECONNECT_MS   10000       // Intentar reconexión cada 10 seg (No bloqueante)
// =============================================================================

// =============================================================================
// 4. GEOMETRÍA
// =============================================================================
#define HALL_PULSES_REV 1
#define WHEEL_RADIUS_MM 7
#define WHEEL_CIRC_M    (3.14159 * 0.7 / 1000.0)

struct IpcMsg {
    uint8_t limit_idx;
    bool    valid;
};

// =============================================================================
// 5. PARÁMETROS DE DISPLAY
// =============================================================================
#define SPEED_DISPLAY_MULT  1.0  // Multiplicador para simular velocidades altas
#define SPEED_CALC_WINDOW_MS 100 // Ventana de cálculo de velocidad (ms)


#endif // CONFIG_H