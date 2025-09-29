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

    const float block_rate_hz = audio_rate / static_cast<float>(AUDIO_BLOCK_SIZE);
    adc_filter_init(block_rate_hz, 30.0f, 0u);

    Serial.println("Core 0 initialization complete");
    g_core0_setup_complete = true;
}

void loop() {
    audio_tick();
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
    display.println("ADC Values:");
    for (int i = 0; i < NUM_ADC_INPUTS; ++i) {
        display.setCursor(0, 34 + (i * 8));
        display.printf("Ch%d: %4d", i, adc_results_buf[i]);
    }

    display.setCursor(0, 58);
    display.println("Noise: ON");
    display.display();
}
