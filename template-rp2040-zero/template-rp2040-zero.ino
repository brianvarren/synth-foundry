/**
 * @file minimal_main.ino
 * @brief Minimal Synth Foundry audio synthesis template
 * 
 * This is the absolute minimal implementation that demonstrates:
 * - Display setup and ADC value display
 * - Raw 12-bit ADC reading and display
 * - White noise generation via PWM/DMA on GP6
 * 
 * @author Brian Varren
 * @version 1.0
 * @date 2024
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config_pins.h"
#include "ADCless.h"
#include "DACless.h"
#include "audio_engine.h"

// ── Display Object ────────────────────────────────────────────────────────────────
Adafruit_SSD1306 display(DISPLAY_WIDTH, DISPLAY_HEIGHT, &Wire1, -1);

// ── Global State ─────────────────────────────────────────────────────────────────
static bool g_display_initialized = false;
static volatile bool g_core0_setup_complete = false;

// ── Core 0: Audio Processing Setup ────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    while (!Serial);
    
    Serial.println("=== Synth Foundry: Minimal Template ===");
    Serial.println("Initializing Core 0 (Audio Processing)...");
    
    // Initialize ADC system
    configureADC_DMA();
    Serial.println("✓ ADC system initialized");
    
    // Initialize DAC system  
    configurePWM_DMA();
    Serial.println("✓ DAC system initialized");
    
    Serial.println("Core 0 initialization complete");
    g_core0_setup_complete = true;
}

// ── Core 0: Audio Processing Loop ─────────────────────────────────────────────────
void loop() {
    audio_tick();
}

// ── Core 1: Display Setup ─────────────────────────────────────────────────────────
void setup1() {
    Serial.println("Initializing Core 1 (Display)...");
    
    // Wait for Core 0 setup to complete
    while (!g_core0_setup_complete) {
        delay(1);
    }
    
    // Initialize I2C for display
    
    Wire1.setSDA(PIN_DISPLAY_SDA);
    Wire1.setSCL(PIN_DISPLAY_SCL);
    Wire1.begin();
    
    // Initialize SSD1306 display
    if (!display.begin(SSD1306_SWITCHCAPVCC, DISPLAY_I2C_ADDR)) {
        Serial.println("✗ Display initialization failed");
        return;
    }
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.display();
    
    g_display_initialized = true;
    Serial.println("✓ Display initialized");
    Serial.println("=== System Ready ===");
}

// ── Core 1: Display Loop ──────────────────────────────────────────────────────────
void loop1() {
    if (!g_display_initialized) {
        delay(100);
        return;
    }
    
    static uint32_t last_update = 0;
    uint32_t now = millis();
    
    // Update display every 100ms
    if (now - last_update >= 100) {
        display.clearDisplay();
        
        // Draw header
        display.setCursor(0, 0);
        display.println("Synth Foundry");
        display.println("Minimal Template");
        display.drawLine(0, 18, DISPLAY_WIDTH, 18, SSD1306_WHITE);
        
        // Display ADC values as raw 12-bit numbers
        display.setCursor(0, 22);
        display.println("ADC Values:");
        
        for (int i = 0; i < NUM_ADC_INPUTS; i++) {
            display.setCursor(0, 34 + (i * 8));
            display.printf("Ch%d: %4d", i, adc_results_buf[i]);
        }
        
        // Display system status
        display.setCursor(0, 58);
        display.println("Noise: ON");
        
        display.display();
        last_update = now;
    }

}
