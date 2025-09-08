/**
 * @file ADCless.h
 * @brief ADC (Analog-to-Digital Converter) driver for control input processing
 * 
 * This header provides the interface for the RP2040's ADC system, which is used
 * to read analog control inputs (pots, knobs) for real-time parameter control.
 * The system uses DMA (Direct Memory Access) for continuous, non-blocking
 * sampling of all control inputs.
 * 
 * ## ADC Configuration
 * 
 * **8-Channel Input**: Supports up to 8 analog control inputs
 * **DMA Sampling**: Continuous sampling without CPU intervention
 * **12-bit Resolution**: 0-4095 range for precise control
 * **Real-time Processing**: Samples are processed in the audio interrupt
 * 
 * ## Control Inputs
 * 
 * The ADC channels are mapped to specific control functions:
 * - Channel 0: Loop start position
 * - Channel 1: Loop length
 * - Channel 2: Pitch fine tune
 * - Channel 3: Phase modulation (future use)
 * - Channel 4: Crossfade length
 * - Channel 5: Effect 1 (future use)
 * - Channel 6: Effect 2 (future use)
 * - Channel 7: Reserved
 * 
 * ## Hardware Compatibility
 * 
 * The system supports both RP2040 and RP2350B microcontrollers with
 * different ADC pin configurations.
 * 
 * @author Brian Varren
 * @version 1.0
 * @date 2024
 */

#pragma once

// ── ADC Configuration ──────────────────────────────────────────────────────────
#define NUM_ADC_INPUTS      8  // Number of analog control inputs

// ── Hardware Target Selection ──────────────────────────────────────────────────
// Uncomment the appropriate target for your hardware
#define ADCLESS_RP2350B     // Olimex Pico2-XXL (RP2350B)
//#define ADCLESS_RP2040    // Standard RP2040

// ── Pin Configuration ──────────────────────────────────────────────────────────
#ifdef ADCLESS_RP2350B
    #define BASE_ADC_PIN 40  // Starting ADC pin for RP2350B
#else
    #define BASE_ADC_PIN 26  // Starting ADC pin for RP2040
#endif

// ── ADC State Variables ────────────────────────────────────────────────────────
extern volatile uint16_t adc_results_buf[NUM_ADC_INPUTS];  // DMA buffer for ADC results
extern volatile uint16_t* adc_results_ptr[1];              // DMA pointer (must be array of 1)
extern int adc_samp_chan, adc_ctrl_chan;                   // DMA channel assignments

// ── ADC Interface Functions ────────────────────────────────────────────────────

/**
 * @brief Configure ADC DMA for continuous sampling
 * 
 * Sets up the ADC and DMA system for continuous, non-blocking sampling
 * of all control inputs. Must be called during system initialization.
 */
void configureADC_DMA();