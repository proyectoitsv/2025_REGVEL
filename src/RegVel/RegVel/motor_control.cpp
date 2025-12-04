//=== motor_control.cpp ===
#include "motor_control.h"
#include "config.h"
#include "display.h" // Necesario para apagar/encender LEDs físicos

// --- Variables Volátiles (ISR Hall - MANTENIDO DEL ORIGINAL) ---
static volatile uint32_t hall_pulses = 0;
static volatile uint32_t hall_last_ms = 0;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

// --- Variables de Estado (FUSIONADO) ---
static float    current_speed = 0.0;
static uint16_t current_adc = 0;
static uint8_t  current_pwm = 255; // 255 = Parado (Lógica Inversa)
static uint8_t  current_limit_idx = 0;

// --- Variables Override (DEL DEMO) ---
static bool     ovr_active = false;
static uint32_t ovr_end_ms = 0;
static uint32_t tap_last_ms = 0;
static bool     tap_flag_high = false;

// --- Variables Animación LED (DEL DEMO) ---
enum AnimState { ANIM_STEADY, ANIM_ENTRY, ANIM_OVERRIDE };
static AnimState led_state = ANIM_STEADY;
static uint32_t  anim_timer = 0;
static uint8_t   anim_counter = 0;
static bool      led_toggle = false;

// --- ISR Sensor Hall (MANTENIDO) ---
void IRAM_ATTR isr_hall() {
    portENTER_CRITICAL_ISR(&timerMux);
    hall_pulses++;
    hall_last_ms = millis();
    portEXIT_CRITICAL_ISR(&timerMux);
}

// --- Helper: Obtener Pin LED (DEL DEMO) ---
int get_limit_led_pin(uint8_t limit) {
    switch(limit) {
        case 1: return PIN_LED_L1;
        case 2: return PIN_LED_L2;
        case 3: return PIN_LED_L3;
        case 4: return PIN_LED_L4;
        default: return -1;
    }
}

// --- Inicialización (FUSIONADO) ---
void motor_control_init() {
    // Hardware Original + Demo
    pinMode(PIN_HALL, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_HALL), isr_hall, RISING);
    
    pinMode(PIN_PEDAL, INPUT);
    pinMode(PIN_PWM, OUTPUT);
    
    // Configurar PWM (Core 3.x compatible)
    ledcAttach(PIN_PWM, PWM_FREQ, PWM_RES);
    ledcWrite(PIN_PWM, 255); // Motor Parado (Lógica Inversa)
}

// --- Detección Doble Pisotón (DEL DEMO) ---
void check_override() {
    uint32_t now = millis();
    bool is_high = (current_adc >= ADC_DBL_TAP_THR);

    if (is_high && !tap_flag_high) {
        uint32_t diff = now - tap_last_ms;
        if (diff >= TAP_MIN_MS && diff <= TAP_MAX_MS) {
            if (current_limit_idx > 0) {
                ovr_active = true;
                
                uint32_t duration = 5;
                switch(current_limit_idx) {
                    case 1: duration = TIME_OVR_L1; break;
                    case 2: duration = TIME_OVR_L2; break;
                    case 3: duration = TIME_OVR_L3; break;
                    case 4: duration = TIME_OVR_L4; break;
                }
                ovr_end_ms = now + (duration * 1000);
                led_state = ANIM_OVERRIDE; // Activar animación override
            }
        }
        tap_last_ms = now;
    }
    tap_flag_high = is_high;

    if (ovr_active && now > ovr_end_ms) {
        ovr_active = false;
        led_state = ANIM_STEADY; // Volver a fijo
    }
}

// --- Loop Principal Motor (FUSIONADO) ---
void motor_update() {
    uint32_t now = millis();

    // 1. Cálculo de Velocidad (DEL ORIGINAL)
    static uint32_t last_calc = 0;
    if (now - last_calc >= 100) {
        uint32_t pulses;
        uint32_t last_pulse_time;
        portENTER_CRITICAL(&timerMux);
        pulses = hall_pulses;
        hall_pulses = 0;
        last_pulse_time = hall_last_ms;
        portEXIT_CRITICAL(&timerMux);

        if (pulses > 0) {
            float rpm = (pulses * 600.0) / HALL_PULSES_REV;
            current_speed = rpm * WHEEL_CIRC_M * 0.06;
        } else {
            if (now - last_pulse_time > 1000) current_speed = 0;
        }
        last_calc = now;
    }

    // 2. Lectura y Override (DEL DEMO)
    current_adc = analogRead(PIN_PEDAL);
    check_override();

    // 3. Cálculo PWM Inverso (DEL DEMO)
    // ADC 0 (Suelto) -> PWM 255 (Parado)
    // ADC 4095 (Fondo) -> PWM 0 (Max)
    int pwm_calc = map(current_adc, ADC_MIN, ADC_MAX, 255, 0);
    if (pwm_calc < 0) pwm_calc = 0;
    if (pwm_calc > 255) pwm_calc = 255;

    // 4. Aplicar Límites (DEL DEMO)
    if (current_limit_idx > 0 && !ovr_active) {
        int pwm_floor = 0;
        switch(current_limit_idx) {
            case 1: pwm_floor = PWM_LIM_1; break;
            case 2: pwm_floor = PWM_LIM_2; break;
            case 3: pwm_floor = PWM_LIM_3; break;
            case 4: pwm_floor = PWM_LIM_4; break;
        }
        // En inversa, limitar es impedir bajar de X
        if (pwm_calc < pwm_floor) {
            pwm_calc = pwm_floor;
        }
    }

    // 5. Escribir Motor
    current_pwm = (uint8_t)pwm_calc;
    ledcWrite(PIN_PWM, current_pwm);

    // 6. Gestión LEDs y Animaciones (DEL DEMO)
    // Apagar todos primero para evitar "fantasmas"
    digitalWrite(PIN_LED_L1, LOW); digitalWrite(PIN_LED_L2, LOW);
    digitalWrite(PIN_LED_L3, LOW); digitalWrite(PIN_LED_L4, LOW);

    int active_pin = get_limit_led_pin(current_limit_idx);
    
    if (active_pin != -1) {
        switch (led_state) {
            case ANIM_ENTRY: // 5 Destellos 2Hz
                if (now - anim_timer >= BLINK_ENTRY_MS) {
                    anim_timer = now;
                    led_toggle = !led_toggle;
                    if (led_toggle) anim_counter++;
                }
                if (anim_counter >= 5) led_state = ANIM_STEADY;
                digitalWrite(active_pin, led_toggle ? HIGH : LOW);
                break;

            case ANIM_OVERRIDE: // Oscilación 10Hz
                if (now - anim_timer >= BLINK_OVR_MS) {
                    anim_timer = now;
                    led_toggle = !led_toggle;
                }
                digitalWrite(active_pin, led_toggle ? HIGH : LOW);
                break;

            case ANIM_STEADY: // Fijo
                digitalWrite(active_pin, HIGH);
                break;
        }
    }
}

void motor_set_limit(uint8_t limit_idx) {
    if (limit_idx != current_limit_idx) {
        current_limit_idx = limit_idx;
        ovr_active = false;
        
        // Iniciar animación si es límite válido
        if (current_limit_idx > 0) {
            led_state = ANIM_ENTRY;
            anim_counter = 0;
            anim_timer = millis();
        } else {
            led_state = ANIM_STEADY;
        }
    }
}

// Getters
float motor_get_speed() { return current_speed; }
bool motor_is_override() { return ovr_active; }
uint16_t motor_get_adc() { return current_adc; }
uint8_t motor_get_pwm() { return current_pwm; }