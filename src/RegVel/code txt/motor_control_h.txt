//=== motor_control.h ===
#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include <Arduino.h>

void motor_control_init();
void motor_update();
void motor_set_limit(uint8_t limit_idx);

// Getters para Display y Debug

float motor_get_speed();
bool motor_is_override();
uint16_t motor_get_adc(); // Nuevo para Debug
uint8_t motor_get_pwm();  // Nuevo para Debug

#endif