#ifndef MAIN_H
#define MAIN_H

#include <Arduino.h>

#define  MIN_RFPOWER -30
#define  MAX_RFPOWER 31

#define FAN_PIN        GPIO_NUM_1          // GPIO where the fan control line is connected
#define FAN_ON_TEMP    45.0    // Temperature to turn the fan ON (°C)
#define FAN_OFF_TEMP   40.0    // Temperature to turn the fan OFF (°C) - hysteresis

extern String currentFrequency; 
extern String currentAmplitude;  
extern String currentFreqUnit;  
extern bool FilterStatus; 
extern bool RFStatus;

// Firmware build timestamp, generated at compile time from __DATE__/__TIME__.
// Format matches: "Aug 13 2026 14:32:51"
extern const char* FW_BUILD_TIMESTAMP;

extern bool rfOutputEnabled; 

char* FloatToChar(float num);
char* DoubleToChar(double num);

void saveRFSettings();
void readRFSettings();
void Fan_Init(void);
void Fan_Control(float temp_val);

#endif