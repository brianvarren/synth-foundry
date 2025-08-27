#include <Arduino.h>
#include "DACless.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <elapsedMillis.h>

// ---- Display Config ----
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
#define PIN_WIRE_SDA 14
#define PIN_WIRE_SCL 15
#define DISPLAY_BUF_SIZE 128
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire1, OLED_RESET);

// ---- Display Buffer Globals ----
volatile uint16_t live_display_buf[DISPLAY_BUF_SIZE];
volatile int live_display_index = 0;
volatile bool live_display_ready = false;

// ---- Morph Filter ----
class MovingAverageFilter {
public:
    MovingAverageFilter(size_t window_size = 8)
        : _window_size(window_size), _sum(0), _index(0), _count(0)
    {
        _buffer = new uint16_t[_window_size]();
    }
    ~MovingAverageFilter() { delete[] _buffer; }
    uint16_t process(uint16_t in) {
        _sum -= _buffer[_index];
        _buffer[_index] = in;
        _sum += in;
        _index = (_index + 1) % _window_size;
        if (_count < _window_size) ++_count;
        return _sum / _count;
    }
private:
    size_t _window_size;
    uint16_t* _buffer;
    uint32_t _sum;
    size_t _index;
    size_t _count;
};

// ---- Wavetable ----
#include "sampledata.h" // Must provide brainwavetable[256*2048] as int16_t array

// ---- DACless Config ----
#define PIN_PWM 6  // Default: GP6
#define AUDIO_BLOCK_SIZE 128
#define N_ADC_INPUTS 4
DAClessConfig dcfg = {
    .pinPWM = PIN_PWM,
    .pwmBits = 12,
    .blockSize = AUDIO_BLOCK_SIZE,
    .nAdcInputs = N_ADC_INPUTS
};
DAClessAudio audio(dcfg);

#define PWM_RESOLUTION (1 << 12) // 4096 for 12 bits

// ---- Morph Filter ----
MovingAverageFilter morphFilter(8);

// ---- ADC Index Definitions ----
#define ADC_FREQ 0
#define ADC_MORPH 2

// ---- Pitch LUT ----
uint32_t freq_inc_lut[4096];

// ---- Octave and LFO Controls ----
#define PIN_OCTAVE_UP   2
#define PIN_OCTAVE_DOWN 3
volatile int octave_offset = 0;
const int OCTAVE_MIN = 0;     // A0 (27.5 Hz)
const int OCTAVE_MAX = 8;     // A8 (7040 Hz)

#define PIN_SW_LFO 0
#define PIN_LED_LFO 4
volatile bool sw_lfo_flag = false;

#define LFO_STEPS 10
#define LFO_MIN_PERIOD 0.2
#define LFO_MAX_PERIOD 1200.0
#define LFO_MIN_FREQ (1.0 / LFO_MIN_PERIOD)
#define LFO_MAX_FREQ (1.0 / LFO_MAX_PERIOD)
float lfo_freq_lut[LFO_STEPS];
volatile int lfo_speed_index = 0;

// ---- Debug ----
volatile float debug_last_freq = 0.0f;
elapsedMillis debug_millis = 0;


// ---- Forward Declarations ----
void build_pitch_lut(float base_freq, float sample_rate);
void build_lfo_freq_lut();
void pollOctaveSwitch();
void updateLFOSwitch();
void displayTask();
void audioBlockCallback(void* userdata, uint16_t* out_buf);

// ---- Build pitch LUT ----
void build_pitch_lut(float base_freq, float sample_rate) {
    for (int i = 0; i < 4096; ++i) {
        float semitones = (i / 4096.0f) * 12.0f; // 0-12 semitones (1 octave)
        float freq = base_freq * powf(2.0f, semitones / 12.0f);
        freq_inc_lut[i] = (uint32_t)(freq * 4294967296.0f / sample_rate);
    }
}

// ---- Build LFO LUT ----
void build_lfo_freq_lut() {
    float min_freq = LFO_MAX_FREQ;
    float max_freq = LFO_MIN_FREQ;
    for (int i = 0; i < LFO_STEPS; ++i) {
        float t = i / (float)(LFO_STEPS - 1);
        lfo_freq_lut[i] = min_freq * powf((max_freq / min_freq), 1.0f - t);
    }
}

// ---- Octave or LFO rate up/down debounce ----
void pollOctaveSwitch() {
    static bool last_up = HIGH;
    static bool last_down = HIGH;
    static unsigned long last_debounce_up = 0;
    static unsigned long last_debounce_down = 0;
    const unsigned long debounce_delay = 25;

    bool up_now = digitalRead(PIN_OCTAVE_UP);
    bool down_now = digitalRead(PIN_OCTAVE_DOWN);

    if (sw_lfo_flag) {
        if (last_up == HIGH && up_now == LOW && (millis() - last_debounce_up) > debounce_delay) {
            if (lfo_speed_index > 0) lfo_speed_index--;
            last_debounce_up = millis();
        }
        last_up = up_now;
        if (last_down == HIGH && down_now == LOW && (millis() - last_debounce_down) > debounce_delay) {
            if (lfo_speed_index < LFO_STEPS-1) lfo_speed_index++;
            last_debounce_down = millis();
        }
        last_down = down_now;
    } else {
        if (last_up == HIGH && up_now == LOW && (millis() - last_debounce_up) > debounce_delay) {
            if (octave_offset < OCTAVE_MAX) octave_offset++;
            last_debounce_up = millis();
        }
        last_up = up_now;
        if (last_down == HIGH && down_now == LOW && (millis() - last_debounce_down) > debounce_delay) {
            if (octave_offset > OCTAVE_MIN) octave_offset--;
            last_debounce_down = millis();
        }
        last_down = down_now;
    }
}

// ---- LFO Switch handler ----
void updateLFOSwitch() {
    static bool last_state = HIGH;
    static unsigned long last_debounce = 0;
    const unsigned long debounce_delay = 20;
    bool current_state = digitalRead(PIN_SW_LFO);

    if (current_state != last_state) last_debounce = millis();
    if ((millis() - last_debounce) > debounce_delay) {
        sw_lfo_flag = (current_state == LOW);
        digitalWrite(PIN_LED_LFO, sw_lfo_flag ? HIGH : LOW);
    }
    last_state = current_state;
}

// ---- Audio Block Callback ----
void audioBlockCallback(void* userdata, uint16_t* out_buf) {
    static uint32_t phase_accum = 0;
    const volatile uint16_t* adc_buf = audio.getAdcBuffer();

    uint16_t adc_pitch = adc_buf[ADC_FREQ];
    uint32_t phase_increment = 0;
    float freq = 0.0f;

    if (sw_lfo_flag) {
        float base_freq = lfo_freq_lut[lfo_speed_index];
        float mod_amt = ((adc_buf[ADC_FREQ] / 4095.0f) - 0.5f) * 1.0f;
        float modded_freq = base_freq * (1.0f + mod_amt);
        modded_freq = constrain(modded_freq, LFO_MAX_FREQ, LFO_MIN_FREQ);
        freq = modded_freq;
        phase_increment = (uint32_t)(freq * 4294967296.0f / audio.getSampleRate());
    } else {
        int oct = constrain(octave_offset, OCTAVE_MIN, OCTAVE_MAX);
        uint32_t base_inc = freq_inc_lut[adc_pitch];
        if (oct == 0)       phase_increment = base_inc;
        else if (oct > 0)   phase_increment = base_inc << oct;
        else                phase_increment = base_inc >> (-oct);

        float base_freq = 27.5f;
        float semitones = (adc_pitch / 4096.0f) * 12.0f;
        freq = base_freq * powf(2.0f, semitones / 12.0f);
        if (oct > 0)      freq *= (1 << oct);
        else if (oct < 0) freq /= (1 << (-oct));
    }
    debug_last_freq = freq;

    for (int i = 0; i < dcfg.blockSize; ++i) {
        phase_accum += phase_increment;

        // ---- Morphing and wavetable code ----
        uint16_t raw_morph = adc_buf[ADC_MORPH];
        uint16_t smoothed_morph = morphFilter.process(raw_morph);
        float morph_pos = map(smoothed_morph, 0, 4095, 0.0f, 255.0f);

        uint16_t frame_a = floor(morph_pos);
        uint16_t frame_b = min(frame_a + 1, 255U);
        float morph_frac = morph_pos - frame_a;

        uint16_t index_0 = phase_accum >> 21;
        uint16_t mu_scaled = (phase_accum >> 13) & 0xFF;
        int32_t offset_a = (frame_a * 2048) + index_0;
        int32_t offset_b = (frame_b * 2048) + index_0;

        int16_t sa_0 = brainwavetable[offset_a];
        int16_t sa_1 = brainwavetable[(offset_a + 1) & 0x7FFFF];
        int16_t sb_0 = brainwavetable[offset_b];
        int16_t sb_1 = brainwavetable[(offset_b + 1) & 0x7FFFF];

        uint16_t sa = interpolate(sa_0, sa_1, mu_scaled);
        uint16_t sb = interpolate(sb_0, sb_1, mu_scaled);

        uint16_t out_0 = (sa * (1.0f - morph_frac)) + (sb * morph_frac);
        out_buf[i] = constrain(out_0, 0, PWM_RESOLUTION - 1);

        // ---- Display Buffer Logic ----
        if (live_display_index < DISPLAY_BUF_SIZE) {
            live_display_buf[live_display_index++] = out_buf[i];
            if (live_display_index == DISPLAY_BUF_SIZE) {
                live_display_ready = true;
                live_display_index = 0;
            }
        }
    }
}

// ---- Display thread ----
void displayTask() {
    Wire1.setSDA(PIN_WIRE_SDA);
    Wire1.setSCL(PIN_WIRE_SCL);
    Wire1.begin();

    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) while (1);
    display.clearDisplay();
    display.display();

    while (1) {
        if (live_display_ready) {
            display.clearDisplay();
            for (int x = 0; x < DISPLAY_BUF_SIZE - 1; x++) {
                uint16_t s0 = live_display_buf[x];
                uint16_t s1 = live_display_buf[x + 1];
                int y0 = SCREEN_HEIGHT - 1 - ((s0 * (SCREEN_HEIGHT - 1)) / (PWM_RESOLUTION - 1));
                int y1 = SCREEN_HEIGHT - 1 - ((s1 * (SCREEN_HEIGHT - 1)) / (PWM_RESOLUTION - 1));
                display.drawLine(x, y0, x + 1, y1, SSD1306_WHITE);
            }
            display.display();
            live_display_ready = false;
        }
        delay(2);
        tight_loop_contents();
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(PIN_LED_LFO, OUTPUT);
    pinMode(PIN_SW_LFO, INPUT_PULLUP);
    pinMode(PIN_OCTAVE_UP, INPUT_PULLUP);
    pinMode(PIN_OCTAVE_DOWN, INPUT_PULLUP);

    octave_offset = 0;
    lfo_speed_index = 0;

    // Set block callback and initialize audio
    audio.setBlockCallback(audioBlockCallback, nullptr);
    audio.begin();

    // LUTs must be built *after* audio.begin() (so we get the real sample rate)
    float base_freq = 27.5f; // A0
    float sample_rate = audio.getSampleRate();
    build_pitch_lut(base_freq, sample_rate);
    build_lfo_freq_lut();

    multicore_launch_core1(displayTask);
    Serial.println("Audio-rate morph crossfade system ready");
}

void loop() {
    updateLFOSwitch();
    pollOctaveSwitch();

    if (debug_millis > 500){
        // Optional: Read and display ADC values
        Serial.print("ADC values: ");
        for (int i = 0; i < 4; i++) {
            Serial.print(audio.getADC(i));
            Serial.print(" ");
        }
        Serial.println();   
        debug_millis = 0;
    }


}
