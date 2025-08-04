#include <Arduino.h>
#include "samples.h"
#include "sampledata.h"
#include "DACless.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "VideoLight9pt7b.h"
#include <elapsedMillis.h>
#include "EEncoder.h"
#include "FastFrequencyCalculator.h"  // NEW: Include the frequency calculator

void updateDisplay();

// NEW: Create frequency calculator instance
//FrequencyCalculator freqCalc(audio_rate, 27.5f, 40, 4080);  // Using A0 as base like your old code

FastFrequencyCalculator freqCalc;

// NEW: Octave control
int8_t currentOctave = 0;  // -8 to +8 range for octave transposition

// display stuff
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
#define PIN_WIRE_SDA 14
#define PIN_WIRE_SCL 15
#define DISPLAY_BUF_SIZE 128
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire1, OLED_RESET);

int16_t map_frame_index = 0;
int16_t non_interp_frame_index = 0;
int16_t frame_y[DISPLAY_BUF_SIZE];
int16_t last_frame_y[DISPLAY_BUF_SIZE];
int16_t waveform_display[256];

bool show_quantization_message = false;
elapsedMillis quantization_message_millis;

#define ENCODER_PIN_A   4
#define ENCODER_PIN_B   5
#define ENCODER_BUTTON  3

EEncoder encoder(ENCODER_PIN_A, ENCODER_PIN_B, ENCODER_BUTTON, 4);
int currentValue = 0;
bool pos_adjust_mode = false; // false = select table, true = morph/pos

// Front panel controls
#define CONTROL_INTERVAL 20

elapsedMillis control_rate_millis;
elapsedMillis button_held_millis;
elapsedMillis debounce_timer;
elapsedMillis debug_print_timer;  // NEW: For periodic debug output
volatile bool lfo_button_pressed = false;
volatile bool lfo_button_state_changed = false;

#define PIN_LED_LFO 8
#define PIN_SW_LFO  0 
#define PIN_SW_OCT_UP   1   // NEW: Octave up switch
#define PIN_SW_OCT_DOWN 2   // NEW: Octave down switch

const unsigned long debounce_millis = 150;
const unsigned long debug_interval = 1000;  // Print debug every 1 second
unsigned long sw_prev_next_millis = to_ms_since_boot(get_absolute_time());
volatile bool sw_prev_flag = false;
volatile bool sw_next_flag = false;
volatile bool sw_lfo_flag = false;

// NEW: Track button states for edge detection
bool lastOctUpState = HIGH;
bool lastOctDownState = HIGH;
bool lastLFOState = HIGH;

// Oscillator core
volatile uint32_t phase_accum = 0;
volatile uint32_t index_0 = 0;
volatile uint32_t index_1 = 0;
volatile int out_0 = 0;

volatile uint16_t fm_adc_value = 2047;

// Wavetable stuff
#define SAMPLES_PER_FRAME 256
#define NUM_INTERP_FRAMES 256
#define INTERP_ARRAY_SIZE (SAMPLES_PER_FRAME * NUM_INTERP_FRAMES)
#define INTERPOLATE 1

volatile int current_table = 0;
volatile int frame_offset = 0;
volatile int16_t table_sine[256];
volatile int16_t waveform[256];
volatile int16_t interp_table[INTERP_ARRAY_SIZE];
volatile uint16_t frame_index_old = 0;
volatile uint8_t frame_changed_flag = 0;

// For audio engine
volatile int callback_flag = 0;
volatile uint16_t process_index = 0;

// V/oct stuff - SIMPLIFIED (most moved to FrequencyCalculator)
volatile bool quantize_increments_to_semitones = true;

// REMOVED: All the old frequency LUT arrays and functions
// - frequency_increment_lut[]
// - semitone_increment_lut[]
// - fmModulationLUT[]
// - initIncrementLUTs()
// - initFMModulationLUT()
// - mapAdcToFMModulation()
// - findNearestSemitoneIncrement()
// - correctionFactor()

MovingAverageFilter FMfilter(8);
MovingAverageFilter posFilter(8);

// Crossfade morph function (unchanged)
void crossfadeMorph(const int16_t* wavetable_data, volatile int16_t* interp_table, uint8_t frame_count, uint16_t samples_per_frame, uint16_t num_interp_frames) {
    uint16_t keyframe_interval = num_interp_frames / frame_count;

    for (int16_t sample = 0; sample < samples_per_frame; sample++) {
        for (int16_t index_x = 0; index_x < num_interp_frames; index_x++) {
            uint16_t keyframe_index = index_x / keyframe_interval;

            int16_t value_0 = wavetable_data[(samples_per_frame * keyframe_index) + sample];
            int16_t value_1;

            if (keyframe_index < frame_count - 1) {
                value_1 = wavetable_data[(samples_per_frame * (keyframe_index + 1)) + sample];
            } else {
                value_1 = value_0;
            }

            uint16_t position_within_interval = index_x % keyframe_interval;
            uint16_t mu_scaled = (position_within_interval * 255) / (keyframe_interval - 1);

            uint16_t interp_result = interpolate(value_0, value_1, mu_scaled);

            // Store the result into interp_table
            interp_table[(index_x * samples_per_frame) + sample] = interp_result;
        }
    }
}

#define CROSSFADE_SAMPLES 32  // Number of samples to crossfade
#define FIXED_POINT_SHIFT 16  // Shift amount for fixed-point arithmetic

volatile int16_t waveform_a[256];
volatile int16_t waveform_b[256];
volatile int16_t* current_waveform = waveform_a;
volatile int16_t* next_waveform = waveform_b;

// REFACTORED process() function using FrequencyCalculator
void process() {
    static bool crossfading = false;
    static uint16_t crossfadeCounter = 0;

    // Read amplitude once per block for smooth manual control
    uint16_t amp_adc = adc_results_buf[0];    // 0 = min, 4095 = max

    for (process_index = 0; process_index < AUDIO_BLOCK_SIZE; process_index++) {
        // Get ADC values
        // NOTE: The inversion here (4095 - adc) might be causing the mode confusion
        // In LFO mode, you might want raw ADC values instead of inverted
        //uint16_t vOctADC = 4095 - adc_results_buf[3];  // Inverted for V/oct
        
        // Alternative for debugging - try without inversion:
        uint16_t vOctADC = adc_results_buf[3];  // Raw ADC value
        
        // FM modulation - using actual ADC value now
        //fm_adc_value = 4095 - adc_results_buf[2];  // Re-enabled FM input
        fm_adc_value = 2047;    //temporary fixed value
        uint16_t smoothed_fm_value = FMfilter.process(fm_adc_value);
        
        // NEW: Use FrequencyCalculator for phase increment
        uint32_t phaseIncrement = freqCalc.getPhaseIncrement(vOctADC, smoothed_fm_value, currentOctave);
        
        // Update phase accumulator
        phase_accum += phaseIncrement;
        
        // Check if we need to start crossfading (for smooth morph transitions)
        bool start_crossfade = ((phase_accum + (phaseIncrement * (CROSSFADE_SAMPLES / 2))) >> 31) 
                              != (phase_accum >> 31);
        
        if (start_crossfade) {
            makeWaveform((int16_t*)next_waveform);
            crossfading = true;
            crossfadeCounter = 0;
        }
        
        // Extract wavetable indices from phase accumulator
        // [31:24] = wavetable index, [23:16] = interpolation fraction
        uint16_t index_0 = phase_accum >> 24;
        uint16_t index_1 = (index_0 + 1) & 0xFF;
        uint16_t mu_scaled = (phase_accum >> 16) & 0xFF;
        
        // Wavetable lookup with interpolation
        uint16_t current_sample = current_waveform[index_0];
        uint16_t next_sample = current_waveform[index_1];
        uint16_t out_0 = interpolate(current_sample, next_sample, mu_scaled);
        
        // Handle crossfading between waveforms
        if (crossfading) {
            uint16_t next_waveform_sample = next_waveform[index_0];
            uint16_t next_waveform_next_sample = next_waveform[index_1];
            uint16_t next_out = interpolate(next_waveform_sample, next_waveform_next_sample, mu_scaled);
            
            // Crossfade calculation
            uint32_t fade_factor = (crossfadeCounter << FIXED_POINT_SHIFT) / CROSSFADE_SAMPLES;
            uint32_t inv_fade_factor = (1 << FIXED_POINT_SHIFT) - fade_factor;
            out_0 = (out_0 * inv_fade_factor + next_out * fade_factor) >> FIXED_POINT_SHIFT;
            
            if (++crossfadeCounter >= CROSSFADE_SAMPLES) {
                crossfading = false;
                // Swap waveform pointers
                volatile int16_t* temp = current_waveform;
                current_waveform = next_waveform;
                next_waveform = temp;
            }
        }
        
        int16_t delta = out_0 - 2048;           // 1 SUB
        int32_t scaled = (int32_t)delta * amp_adc >> 12; // 1 MUL, 1 SHR
        out_buf_ptr[process_index] = scaled + 2048;         // 1 ADD
    }
}

void makeWaveform(int16_t* target_waveform) {
    uint16_t pos = adc_results_buf[0];
    uint16_t smoothed_pos = posFilter.process(pos);

    // Calculate frame_index using map function
    uint16_t frame_index = map(smoothed_pos, 0, 4095, 0, NUM_INTERP_FRAMES - (NUM_INTERP_FRAMES / wavetable[current_table].frames));

    frame_index_old = frame_index;

    // Fill the target_waveform array with samples from interp_table
    for (int16_t sample = 0; sample < SAMPLES_PER_FRAME; sample++) {
        int32_t sample_index = (SAMPLES_PER_FRAME * frame_index) + sample;
        target_waveform[sample] = interp_table[sample_index];
    }
}

void setup() {
    Serial.begin(115200);
    while(!Serial);
    Serial.println("Serial connected.");

    pinMode(ENCODER_BUTTON, INPUT_PULLUP);
    pinMode(PIN_SW_LFO, INPUT_PULLUP);
    pinMode(PIN_SW_OCT_UP, INPUT_PULLUP);    // NEW: Octave up button
    pinMode(PIN_SW_OCT_DOWN, INPUT_PULLUP);  // NEW: Octave down button

    encoder.setEncoderHandler(onEncoderRotate);
    pinMode(PIN_LED_LFO, OUTPUT);

    for (int i = 0; i < AUDIO_BLOCK_SIZE; i++) {
        pwm_out_buf_a[i] = i;
        pwm_out_buf_b[i] = i;
    }

    configurePWM_DMA();
    configureADC_DMA();

    sleep_ms(1000);

    freqCalc.init(audio_rate, 30, 30, 4080, 1);

    setupInterpolators();
    makeWaveform((int16_t*)current_waveform);
    
    sleep_ms(200);
    Serial.println("Setup complete.");
    Serial.print("Initial octave: ");
    Serial.println(currentOctave);
    
    crossfadeMorph(wavetable[current_table].data, interp_table, wavetable[current_table].frames, SAMPLES_PER_FRAME, NUM_INTERP_FRAMES);

    //changeOctave(3);

}

void changeOctave(int8_t direction) {
    int8_t oldOctave = currentOctave;
    currentOctave = constrain(currentOctave + direction, -4, 4);  // Limit to reasonable range
    
    if (currentOctave != oldOctave) {
        Serial.print("Octave changed: ");
        Serial.print(oldOctave);
        Serial.print(" -> ");
        Serial.print(currentOctave);
        
        // Calculate frequency multiplier
        float multiplier = pow(2.0f, currentOctave);
        Serial.print(" (x");
        Serial.print(multiplier, 2);
        Serial.println(")");
        
        // Show current frequency at center voltage
        uint32_t testInc = freqCalc.getPhaseIncrement(2048, 2048, currentOctave);
        float testFreq = freqCalc.getFrequency(testInc);
        Serial.print("Frequency at 1.65V: ");
        Serial.print(testFreq, 2);
        Serial.println(" Hz");
    }
}

// // UPDATED: Simplified frequency display functions
// String calculateBaseFrequency(uint32_t phase_increment) {
//     float frequency = freqCalc.getFrequencyHz(phase_increment);
//     return String(frequency, 6) + " Hz";
// }

// String calculateBasePeriod(uint32_t phase_increment) {
//     float period = freqCalc.getPeriodSeconds(phase_increment);
    
//     if (period < 1.0) {
//         return String(period * 1000.0, 3) + " ms";
//     } else if (period < 60.0) {
//         return String(period, 3) + " s";
//     } else {
//         return String(period / 60.0, 3) + " m";
//     }
// }

void loop() {
    encoder.update();

    if (callback_flag > 0) {
        process();
        callback_flag = 0;
    }
    
    // Periodic debug output
    if (debug_print_timer >= debug_interval) {
        debug_print_timer = 0;
        
        /* ------------------------------------------------------------------
        *  Debug print – call from loop() when you want a heartbeat
        * -----------------------------------------------------------------*/
        {
            /* Grab the *current* control values once so the printout is self-consistent */
            const uint16_t vOctADC   = adc_results_buf[3];                 // raw CV
            const uint16_t fmADC     = adc_results_buf[2];                 // raw FM CV
            const uint16_t fmSmooth  = FMfilter.process(fmADC);            // same filter as audio path
            const uint8_t  octave    = currentOctave;                      // external state

            /* Ask the fast calculator for phase-increment & frequency   */
            const uint32_t dbgInc  = freqCalc.getPhaseIncrement(vOctADC, fmSmooth, octave);
            const float    dbgFreq = freqCalc.getFrequency(vOctADC, octave);   // Hz, *no FM* component

            /* -------- pretty-print -------- */
            Serial.println(F("=== Debug Info ==="));
            Serial.print  (F("Mode:            "));
            Serial.println(sw_lfo_flag ? F("LFO") : F("Audio"));

            Serial.print  (F("LED:             "));
            Serial.println(digitalRead(PIN_LED_LFO) ? F("ON") : F("OFF"));

            Serial.print  (F("Switch (raw):    "));
            Serial.println(digitalRead(PIN_SW_LFO) ? F("HIGH (released)") : F("LOW  (pressed)"));

            Serial.print  (F("V/Oct ADC code:  "));
            Serial.println(vOctADC);

            Serial.print  (F("FM ADC (smth):   "));
            Serial.println(fmSmooth);

            Serial.print  (F("Octave shift:    "));
            Serial.println(octave);

            Serial.print  (F("Phase increment: 0x"));
            Serial.println(dbgInc, HEX);

            /* Audio-rate mode → show Hz  |  LFO mode → show period           */
            if (sw_lfo_flag)
            {
                const float period = (dbgFreq > 0.0f) ? (1.0f / dbgFreq) : 0.0f;

                Serial.print(F("Frequency:       "));
                Serial.print(dbgFreq, 6);
                Serial.print(F(" Hz  (Period: "));

                if (period < 1.0f) {                   // < 1 s  → print ms
                    Serial.print(period * 1000.0f, 1);
                    Serial.println(F(" ms)"));
                }
                else if (period < 60.0f) {             // < 1 min → print s
                    Serial.print(period, 2);
                    Serial.println(F(" s)"));
                }
                else {                                 // else    → print minutes
                    Serial.print(period / 60.0f, 2);
                    Serial.println(F(" min)"));
                }
            }
            else
            {
                Serial.print(F("Frequency:       "));
                Serial.print(dbgFreq, 2);
                Serial.println(F(" Hz"));
            }

            Serial.println(F("=================="));
        }
    }
}

void updateDisplay() {
    display.clearDisplay();
    display.setCursor(0, 0);

    // Draw terrain map for interpolated morph tables
    if (!wavetable[current_table].basic_shape) {
        for (map_frame_index = 0; map_frame_index < 32; map_frame_index += 2) {
            for (int i = 0; i < SAMPLES_PER_FRAME; i++) {
                long map_pos = (6 * SAMPLES_PER_FRAME * map_frame_index + i);
                waveform_display[i] = interp_table[map_pos];
            }

            for (int n = 0; n < DISPLAY_BUF_SIZE; n++) {
                frame_y[n] = (waveform_display[n * 2] / 256) - map_frame_index + 30;
                display.drawPixel((n + map_frame_index) / 1.2, frame_y[n], SSD1306_WHITE);
            }
        }
    } else {
        for (int i = 0; i < SAMPLES_PER_FRAME; i++) {
            waveform_display[i] = wavetable[current_table].data[i];
        }

        for (int n = 0; n < DISPLAY_BUF_SIZE - 1; n++) {
            display.drawLine(n, ((waveform_display[n * 2] / 96)), n + 1,
                             ((waveform_display[(n + 1) * 2] / 96)), SSD1306_WHITE);
        }
    }

    display.setCursor(0, 58);
    display.print(wavetable[current_table].name);

    // Visual Mode Indicator
    if (pos_adjust_mode) {
        display.fillRect(0, 54, SCREEN_WIDTH, 10, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
        display.setCursor(4, 56);
        display.print("MORPH MODE");
        display.setTextColor(SSD1306_WHITE);
    } else {
        display.setCursor(4, 56);
        display.print("WAVETABLE");
    }
    
    // NEW: Show current mode and octave in top right
    display.setCursor(90, 10);
    display.print(sw_lfo_flag ? "LFO" : "AUD");
    
    display.setCursor(90, 20);
    display.print("Oct:");
    display.print(currentOctave >= 0 ? "+" : "");
    display.print(currentOctave);
    
    // Show quantization message if active
    if (show_quantization_message && quantization_message_millis < 2000) {
        display.setCursor(70, 56);
        display.print(quantize_increments_to_semitones ? "QUANT" : "FREE");
    } else {
        show_quantization_message = false;
    }
    
    display.display();
    frame_index_old = non_interp_frame_index;
}

void onEncoderRotate(EEncoder& enc){
    int8_t increment = enc.getIncrement();
    
    encoder.setAcceleration(0);
    
    current_table += increment;

    // Wrap selection
    if (current_table > NUM_SAMPLES - 1) current_table = 0;
    if (current_table < 0) current_table = NUM_SAMPLES - 1;
    
    crossfadeMorph(wavetable[current_table].data, interp_table, wavetable[current_table].frames, SAMPLES_PER_FRAME, NUM_INTERP_FRAMES);
    updateDisplay();
}

void onButtonPress(EEncoder& enc) {
    currentValue = 0;
    Serial.println("Button pressed - Value reset to 0");
}