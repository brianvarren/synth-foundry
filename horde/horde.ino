/**
 * @file minimal_main.ino
 * @brief Minimal Synth Foundry audio synthesis template
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config_pins.h"
#include "ADCless.h"
#include "DACless.h"
#include "audio_engine.h"
#include "adc_filter.h"

Adafruit_SSD1306 display(DISPLAY_WIDTH, DISPLAY_HEIGHT, &Wire1, -1);
static bool g_display_initialized = false;
static volatile bool g_core0_setup_complete = false;

// ── Octave Control State ──────────────────────────────────────────────────────
static uint32_t g_octave_up_last_press = 0;
static uint32_t g_octave_down_last_press = 0;

void setup() {
    Serial.begin(115200);
    uint32_t start = millis();
    while (!Serial && (millis() - start) < 1000) {
        delay(10);
    }

    Serial.println("=== Synth Foundry: Minimal Template ===");
    Serial.println("Initializing Core 0 (Audio Processing)...");

    configureADC_DMA();
    Serial.println("✓ ADC system initialized");

    configurePWM_DMA();
    Serial.println("✓ DAC system initialized");

    // Configure octave control buttons
    pinMode(PIN_OCTAVE_UP, INPUT_PULLUP);
    pinMode(PIN_OCTAVE_DOWN, INPUT_PULLUP);

    const float block_rate_hz = audio_rate / static_cast<float>(AUDIO_BLOCK_SIZE);
    adc_filter_init(block_rate_hz, 30.0f, 0u);

    Serial.println("Core 0 initialization complete");
    g_core0_setup_complete = true;
}

void loop() {
    audio_tick();
    
    // Handle octave control buttons with debouncing
    uint32_t now = millis();
    const uint32_t debounce_ms = 200;
    
    if (digitalRead(PIN_OCTAVE_UP) == LOW && (now - g_octave_up_last_press) > debounce_ms) {
        int current_octave = ae_get_octave_shift();
        ae_set_octave_shift(current_octave + 1);
        g_octave_up_last_press = now;
        Serial.print(F("Octave up: "));
        Serial.println(ae_get_octave_shift());
    }
    
    if (digitalRead(PIN_OCTAVE_DOWN) == LOW && (now - g_octave_down_last_press) > debounce_ms) {
        int current_octave = ae_get_octave_shift();
        ae_set_octave_shift(current_octave - 1);
        g_octave_down_last_press = now;
        Serial.print(F("Octave down: "));
        Serial.println(ae_get_octave_shift());
    }
    
    static uint32_t last_switch = 0;
    // Rotate chord every 3 seconds for demo
    if (now - last_switch > 3000) {
        ae_next_glyph();
        last_switch = now;
    }
}

void setup1() {
    Serial.println("Initializing Core 1 (Display)...");
    while (!g_core0_setup_complete) {
        delay(1);
    }

    Wire1.setSDA(PIN_DISPLAY_SDA);
    Wire1.setSCL(PIN_DISPLAY_SCL);
    Wire1.begin();

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

void loop1() {
    if (!g_display_initialized) {
        delay(100);
        return;
    }

    static uint32_t last_update = 0;
    uint32_t now = millis();
    if (now - last_update < 100) {
        return;
    }
    last_update = now;

    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Synth Foundry");
    display.println("Minimal Template");
    display.drawLine(0, 18, DISPLAY_WIDTH, 18, SSD1306_WHITE);

    display.setCursor(0, 22);
    display.println("Current Chord:");
    display.setCursor(0, 34);
    display.println(ae_current_glyph_name());
    
    display.setCursor(0, 46);
    display.print("Octave: ");
    display.println(ae_get_octave_shift());
    display.display();
}
