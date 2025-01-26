#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/gpio.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

//------------------------------------------------------------------------------
// Display Hardware Configuration
//------------------------------------------------------------------------------
#define I2C_SDA_PIN        6
#define I2C_SCL_PIN        7
#define SCREEN_WIDTH       128
#define SCREEN_HEIGHT      32
#define OLED_RESET        -1
#define SCREEN_ADDRESS    0x3C

// Initialize display object
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire1, OLED_RESET);

//------------------------------------------------------------------------------
// GPIO Configuration
//------------------------------------------------------------------------------
// Control inputs
#define PIN_SW_UP         14
#define PIN_SW_DN         15

// System dimensions
#define NUM_OUTPUTS        5
#define NUM_ADC_INPUTS     4

// Pin mappings
const unsigned int out_pins[NUM_OUTPUTS] = { 1, 2, 3, 4, 0 };
const unsigned int knob_pins[NUM_ADC_INPUTS] = { 27, 26, 28, 29 };

//------------------------------------------------------------------------------
// Timing Configuration
//------------------------------------------------------------------------------
// Tempo bounds
const unsigned int bpm_min = 1;
const unsigned int bpm_max = 999;
const unsigned int period_min_us = 100;

// Clock scaling
// Period scale factor of 120000 was chosen to generate timer periods that:
// - Map to whole numbers in thresholds array (e.g. 41 ticks for 1/32 triplets)
// - Provide enough ticks per period for accurate divisions at very fast tempos
// - Stay within timer resolution limits at very slow tempos
const unsigned int period_scale_factor = 120000;

// Musical timing
const unsigned int num_divisors = 21;                    // Total number of available divisions
const unsigned int ticks_per_whole_note = 2000;         // Base unit: 2000 timer ticks = one whole note
const unsigned int pulse_duration_us = 20000;           // Fixed pulse duration of 20ms (20000us)
#define QUARTER_NOTE_LANE 10                            // Index of the quarter-note lane in thresholds[]

//------------------------------------------------------------------------------
// Interface Timing
//------------------------------------------------------------------------------
const unsigned int control_interval = 100;              // How often to poll controls (100ms)
const unsigned int knob_timeout_ms = 3000;             // Display timeout after knob inactive (3s)

//------------------------------------------------------------------------------
// System State Variables
//------------------------------------------------------------------------------
// Knob tracking
unsigned long knob_last_touch_time[NUM_ADC_INPUTS] = {0};
unsigned long last_activity_time = 0;                   // For tracking user input activity
bool knob_timeout_states[NUM_ADC_INPUTS] = {false};
unsigned int knob_last_touched = 0;

// Timing state
unsigned int bpm = 120;

// Hardware timer expects a signed int64_t due to the optional pos/neg behavior of delay_us
volatile int64_t period_us = period_scale_factor / bpm;

// Number of timer ticks to keep pulse high
// Calculated as (pulse_duration_us / period_us) to maintain constant pulse width at any tempo
unsigned int pulse_duration_threshold = pulse_duration_us / period_us;

// Lane state tracking
volatile unsigned int lane_counters[num_divisors];
volatile unsigned int lane_states[num_divisors];
volatile unsigned int output_lanes[NUM_OUTPUTS];


// Define ratios for note subdivisions (triplets, straight, dotted)
const float note_ratios[num_divisors] = {
//      Triplets                    Straight              Dotted
    (2.0f / 3.0f) * (1.0f / 32.0f), 1.0f / 32.0f, 1.5f * (1.0f / 32.0f), // 1/32 notes
    (2.0f / 3.0f) * (1.0f / 16.0f), 1.0f / 16.0f, 1.5f * (1.0f / 16.0f), // 1/16 notes
    (2.0f / 3.0f) * (1.0f / 8.0f),  1.0f / 8.0f,  1.5f * (1.0f / 8.0f),  // 1/8 notes
    (2.0f / 3.0f) * (1.0f / 4.0f),  1.0f / 4.0f,  1.5f * (1.0f / 4.0f),  // 1/4 notes
    (2.0f / 3.0f) * (1.0f / 2.0f),  1.0f / 2.0f,  1.5f * (1.0f / 2.0f),  // 1/2 notes

    1.0f,                    2.0f,         3.0f,                         // whole notes and bars
    4.0f,                    8.0f,         16.0f                        
};

int thresholds[num_divisors]; // Stores the actual timer tick counts derived by multiplying each ratio by ticks_per_whole_note

void calculateThresholds() {
    for (int i = 0; i < num_divisors; i++) {
        thresholds[i] = (int)(ticks_per_whole_note * note_ratios[i]);
    }
}

// Array of character strings for showing current divisor on screen
const char display_divisors[num_divisors][8] = {
    {"1/32t  "}, {"1/32   "}, {"1/32*  "},
    {"1/16t  "}, {"1/16   "}, {"1/16*  "},
    {"1/8t   "}, {"1/8    "}, {"1/8*   "},
    {"1/4t   "}, {"1/4    "}, {"1/4*   "},
    {"1/2t   "}, {"1/2    "}, {"1/2*   "},
    {"note   "}, {"1/2 bar"}, {"3/4 bar"},
    {"bar    "}, {"2 bar  "}, {"4 bar  "} 
};

class HysteresisFilter {
public:
    // Constructor that sets up the filter parameters
    HysteresisFilter(uint16_t input_range, uint16_t output_steps, uint16_t hysteresis_margin) :
        input_range(input_range),
        output_steps(output_steps),
        hysteresis_margin(hysteresis_margin),
        current_output_value(0) {}
    
    // Process new input value and return filtered output
    uint16_t getOutputValue(uint16_t input_value) {
        // Calculate boundaries for current step with hysteresis
        uint16_t lower_bound = (input_range * current_output_value) / output_steps;
        uint16_t upper_bound = (input_range * (current_output_value + 1)) / output_steps;
        
        // Add hysteresis margins, but only between steps
        if (current_output_value > 0) {
            lower_bound -= hysteresis_margin;
        }
        if (current_output_value < (output_steps - 1)) {
            upper_bound += hysteresis_margin;
        }

        // Check if input has moved outside hysteresis band
        if (input_value < lower_bound || input_value > upper_bound) {
            // Calculate new step
            current_output_value = (input_value * output_steps) / input_range;
            
            // Clamp to valid range
            if (current_output_value >= output_steps) {
                current_output_value = output_steps - 1;
            }
        }
        
        return current_output_value;
    }

private:
    const uint16_t input_range;        // Total range of input values
    const uint16_t output_steps;       // Number of discrete output steps
    const uint16_t hysteresis_margin;  // Size of hysteresis band
    uint16_t current_output_value;     // Current output step
};

HysteresisFilter knobFilter(4096, num_divisors, 30); // "num_divisors" represents the total number of output lanes

struct repeating_timer timer;

bool timer_callback(struct repeating_timer *t){

  int64_t current_period = period_us; // Create local copy

  // On each callback ("tick" of the timer), iterate through all lanes and update their states,
  for (int lane = 0; lane < num_divisors; lane++){ 
    // Check the value of the lane's counter against the threshold (divisor)
    if (++lane_counters[lane] >= thresholds[lane]){ // ++ increment must be on the left side, otherwise timing will drift.
      lane_states[lane] = true; // Flag to activate clock pulse
      lane_counters[lane] = 0;  // Reset the counter
    }

    // Check the value of the lane's counter against the pulse duration threshold
    if (lane_states[lane] && lane_counters[lane] >= pulse_duration_threshold){
      lane_states[lane] = false; // Flag to deactivate clock pulse
      // Don't reset the counter; pulse turns off but counter continues incrementing
    }
  }

  // Then iterate through the outputs, assigning them the values contained in lane_states[]
  // output_lanes[] points each output to a lane. 
  for (int output = 0; output < NUM_OUTPUTS; output++){
    int out_lane = output_lanes[output];  // Get the lane assigned to this output
    int out_val = lane_states[out_lane];   // Get the state of the lane
    int out_pin = out_pins[output];    // Get this output's GPIO pin
  
    gpio_put(out_pin, out_val); // Write the state of this output's assigned lane to its output pin
  }

  // Access timer struct member to update period_us
  t->delay_us = -current_period; // Keeping negative period_us ensures timer tracks time between starts of callbacks, vs. between when last callback ends and next begins

  return true;
}

void updateControl(){

  static int output_lanes_previous[NUM_OUTPUTS] = {0};  // Static variable is initialized once then retains its value between calls of updateControl
  
  // Determine knob positions to assign clock lanes to outputs
  for (int knob = 0; knob < NUM_ADC_INPUTS; knob++){
    output_lanes_previous[knob] = output_lanes[knob]; // Store the current lane assignments before reading ADCs to later detect changes
    int input_value = analogRead(knob_pins[knob]); // Get the value at the current knob's ADC pin

    // Filter the value through hysteresis to prevent noise from triggering changes.
    // Hysteresis maps raw input range to number of output lanes because we set "output_steps" parameter equal to the number of output lanes.
    output_lanes[knob] = knobFilter.getOutputValue(input_value);

    // Detect if this knob has been moved by the user (after hysteresis)
    if (output_lanes_previous[knob] != output_lanes[knob]){
      knob_last_touched = knob;
      last_activity_time = millis();
      knob_timeout_states[knob] = false;     // Reset timeout flag
    }
  }

  // Read the state of the switch
  bool sw_up_pressed = !digitalRead(PIN_SW_UP);
  bool sw_dn_pressed = !digitalRead(PIN_SW_DN);

  // Adjust bpm based on switch state
  if (sw_dn_pressed){ bpm--; } else
  if (sw_up_pressed){ bpm++; }

  // Keep the bpm within reasonable bounds
  // - High enough 
  bpm = constrain(bpm, bpm_min, bpm_max); 

  // Derive timer period_us from bpm
  period_us = (bpm > 0) ? period_scale_factor / bpm : period_scale_factor;

  // Recalculate pulse duration based on updated timer period
  pulse_duration_threshold = pulse_duration_us / period_us;

}

void updateDisplay() {
    display.clearDisplay();

    // Check for inactivity
    if (millis() - last_activity_time > knob_timeout_ms) {
        // Default display: Show BPM
        display.setCursor(0, 0);
        display.setTextSize(2);
        display.setTextColor(SSD1306_WHITE);
        display.print("BPM: ");
        display.print(bpm);
    } else {
        // Active display: Show knob/division state
        display.setCursor(0, 0);
        display.setTextSize(2);
        display.print("OUTPUT: ");
        display.print(knob_last_touched + 2);
        display.setCursor(0, 16);
        display.print("DIV: ");
        display.print(display_divisors[output_lanes[knob_last_touched]]);
    }
    
    display.display();
}

void setup(){

  // Serial.begin(115200);
  // while (!Serial);

  // Initialize I2C and display
  Wire1.setSDA(I2C_SDA_PIN);
  Wire1.setSCL(I2C_SCL_PIN);
  Wire1.begin();

  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
      Serial.println("SSD1306 allocation failed");
      while(1);
  }
  display.clearDisplay();
  display.display();

  pinMode(PIN_SW_UP, INPUT_PULLUP);
  pinMode(PIN_SW_DN, INPUT_PULLUP);

  // Set output pin modes
  for (int i = 0; i < NUM_OUTPUTS; i++){
    pinMode(out_pins[i], OUTPUT);
  }

  analogReadResolution(12);

  // Initialize knob ADC inputs
  for (int i = 0; i < NUM_ADC_INPUTS; i++){
    pinMode(knob_pins[i], INPUT);
  }

  updateControl();

  add_repeating_timer_us(-period_us, timer_callback, NULL, &timer);

  calculateThresholds();

  // Ensure output 4 (fifth physical jack) always uses the quarter-note lane
  output_lanes[4] = QUARTER_NOTE_LANE;

}

void loop(){
  unsigned long current_time = millis();
  static unsigned long previous_time = 0;

  // Update controls at the control interval
  if (current_time - previous_time >= control_interval){
    updateControl();
    updateDisplay();
    previous_time = current_time;
  }
}