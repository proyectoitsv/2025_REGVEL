//=== display.cpp ===
#include "display.h"
#include "config.h"
#include "motor_control.h"

// --- Variables Multiplexado ---
static uint32_t last_mux_us = 0;
static uint8_t  digit_idx = 0;
static uint8_t  digits[3] = {0, 0, 0}; 

// --- Helpers BCD 4511 ---
void write_bcd(uint8_t val) {
    digitalWrite(PIN_DISP_A, (val & 1) ? HIGH : LOW);
    digitalWrite(PIN_DISP_B, (val & 2) ? HIGH : LOW);
    digitalWrite(PIN_DISP_C, (val & 4) ? HIGH : LOW);
    digitalWrite(PIN_DISP_D, (val & 8) ? HIGH : LOW);
}

void disp_init() {
    // Pines BCD
    pinMode(PIN_DISP_A, OUTPUT); pinMode(PIN_DISP_B, OUTPUT);
    pinMode(PIN_DISP_C, OUTPUT); pinMode(PIN_DISP_D, OUTPUT);
    
    // Pines Dígitos (Control NPN)
    pinMode(PIN_DIG_1, OUTPUT); 
    pinMode(PIN_DIG_2, OUTPUT); 
    pinMode(PIN_DIG_3, OUTPUT);
    
    // Apagar dígitos
    digitalWrite(PIN_DIG_1, LOW);
    digitalWrite(PIN_DIG_2, LOW);
    digitalWrite(PIN_DIG_3, LOW);
    
    // Inicializar pines de LEDs aquí para asegurar estado inicial
    pinMode(PIN_LED_L1, OUTPUT); pinMode(PIN_LED_L2, OUTPUT);
    pinMode(PIN_LED_L3, OUTPUT); pinMode(PIN_LED_L4, OUTPUT);
    digitalWrite(PIN_LED_L1, LOW); digitalWrite(PIN_LED_L2, LOW);
    digitalWrite(PIN_LED_L3, LOW); digitalWrite(PIN_LED_L4, LOW);
}

void disp_mux() {
    uint32_t now = micros();
    if (now - last_mux_us < DISP_MUX_US) return;
    last_mux_us = now;

    // 1. Obtener velocidad actual (multiplicada para efecto visual)
    int speed = (int)(motor_get_speed() * SPEED_DISPLAY_MULT);
    if (speed > 999) speed = 999;

    // 2. Descomponer
    digits[0] = speed / 100;
    digits[1] = (speed / 10) % 10;
    digits[2] = speed % 10;

    // 3. Blanking (Apagar todo) - NPN Base LOW = OFF
    digitalWrite(PIN_DIG_1, LOW);
    digitalWrite(PIN_DIG_2, LOW);
    digitalWrite(PIN_DIG_3, LOW);

    // 4. Escribir BCD
    write_bcd(digits[digit_idx]);

    // 5. Encender dígito actual - NPN Base HIGH = ON
    switch (digit_idx) {
        case 0: digitalWrite(PIN_DIG_1, HIGH); break;
        case 1: digitalWrite(PIN_DIG_2, HIGH); break;
        case 2: digitalWrite(PIN_DIG_3, HIGH); break;
    }

    // 6. Avanzar
    digit_idx++;
    if (digit_idx > 2) digit_idx = 0;
}