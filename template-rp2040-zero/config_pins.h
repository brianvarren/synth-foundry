/**
 * @file config_pins.h
 * @brief Hardware pin configuration for Waveshare RP2040-Zero audio synthesis module
 * 
 * This file centralizes all hardware pin definitions and configuration constants
 * for the Waveshare RP2040-Zero microcontroller. The module provides:
 * 
 * - 4-channel ADC input for control parameters
 * - Single PWM audio output
 * - SSD1306 OLED display via I2C
 * - Button inputs for navigation
 * 
 * ## Hardware Configuration
 * 
 * **Microcontroller**: RP2040 (no PSRAM, no SD card)
 * **Display**: SSD1306 128x64 OLED using Adafruit GFX library
 * **Audio Input**: 4 ADC channels using ADCless system
 * **Audio Output**: Single PWM output channel using DACless system
 * **Navigation**: Simple button inputs (no rotary encoder)
 * 
 * @author Brian Varren
 * @version 1.0
 * @date 2024
 */

#pragma once

// ── Display Configuration ──────────────────────────────────────────────────────
#define DISPLAY_WIDTH        128
#define DISPLAY_HEIGHT       64
#define DISPLAY_I2C_ADDR     0x3C
#define PIN_DISPLAY_SDA      14   // I2C SDA pin
#define PIN_DISPLAY_SCL      15   // I2C SCL pin

// ── Audio Output Configuration ─────────────────────────────────────────────────
#define PIN_PWM_AUDIO_OUT    6  // PWM audio output pin

// ── ADC Input Configuration ────────────────────────────────────────────────────
#define NUM_ADC_INPUTS       4     // Number of ADC input channels
#define PIN_ADC_0            26    // ADC channel 0 (GP26)
#define PIN_ADC_1            27    // ADC channel 1 (GP27)
#define PIN_ADC_2            28    // ADC channel 2 (GP28)
#define PIN_ADC_3            29    // ADC channel 3 (GP29)

// ── Button Input Configuration ─────────────────────────────────────────────────
#define NUM_BUTTONS          4     // Number of navigation buttons
#define PIN_BUTTON_UP        6     // Up navigation button
#define PIN_BUTTON_DOWN      7     // Down navigation button
#define PIN_BUTTON_SELECT    8     // Select/Enter button
#define PIN_BUTTON_BACK      9     // Back/Escape button


// ── System Configuration ───────────────────────────────────────────────────────
#define CORE_AUDIO           0     // Core 0: Audio processing
#define CORE_DISPLAY         1     // Core 1: Display and UI
#define DISPLAY_UPDATE_RATE  30    // Display update rate (Hz)
#define BUTTON_DEBOUNCE_MS   50    // Button debounce time (ms)

// ── ADC Channel Mapping ────────────────────────────────────────────────────────
// Map ADC channels to control functions
#define ADC_CHANNEL_NOISE_LEVEL   0  // Noise generator level control
#define ADC_CHANNEL_FILTER_CUTOFF 1  // Filter cutoff frequency
#define ADC_CHANNEL_RESONANCE     2  // Filter resonance/Q
#define ADC_CHANNEL_OUTPUT_LEVEL  3  // Master output level


// ── Hardware Validation ────────────────────────────────────────────────────────
// Compile-time checks to ensure valid configuration
static_assert(NUM_ADC_INPUTS <= 4, "RP2040 supports maximum 4 ADC channels");
