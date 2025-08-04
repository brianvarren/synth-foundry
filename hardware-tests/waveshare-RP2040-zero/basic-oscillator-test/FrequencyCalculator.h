#ifndef FREQUENCY_CALCULATOR_H
#define FREQUENCY_CALCULATOR_H

#include <Arduino.h>
#include <cmath>

/**
 * FrequencyCalculator - Encapsulates all pitch and frequency calculations for digital oscillators
 * 
 * Features:
 * - V/oct (1V per octave) pitch tracking with configurable ADC range compensation
 * - Optional semitone quantization
 * - Linear FM with exponential depth scaling
 * - Octave transposition (-8 to +8 octaves)
 * - Efficient LUT-based calculation with interpolation
 * 
 * Phase Accumulator Format (32-bit):
 * [31:24] - Wavetable index (8 bits) - Integer sample position 0-255
 * [23:16] - Interpolation fraction (8 bits) - Sub-sample position for smooth interpolation
 * [15:0]  - Fine frequency resolution (16 bits) - Allows for very slow LFO rates
 * 
 * This format allows direct extraction of wavetable indices and interpolation values
 * without additional calculations in the audio loop.
 */
class FrequencyCalculator {
private:
    // Core configuration
    float sampleRate;
    float baseFrequency;     // Frequency at 0V input (e.g., 16.35Hz for C0)
    uint16_t adcMin;         // Minimum usable ADC value (accounts for end loss)
    uint16_t adcMax;         // Maximum usable ADC value
    bool quantizeToSemitones;
    
    // V/oct lookup table configuration
    static constexpr uint16_t VOCT_LUT_SIZE = 512;  // Covers 5 octaves at ~0.01V resolution
    static constexpr float VOCT_LUT_OCTAVES = 5.0f;  // Number of octaves covered by LUT
    uint32_t vOctLUT[VOCT_LUT_SIZE];
    
    // FM modulation lookup table configuration
    static constexpr uint16_t FM_LUT_SIZE = 4096;   // Must be power of 2 for fast modulo
    static constexpr uint16_t FM_LUT_CENTER = FM_LUT_SIZE / 2;
    int32_t fmModLUT[FM_LUT_SIZE];
    
    // Pre-calculated values for fast ADC to LUT mapping
    uint32_t adcToLutScale;  // Fixed-point scalar (16.16 format)
    uint16_t adcOffset;      // Pre-calculated minimum offset
    
    /**
     * Initialize the V/oct lookup table
     * Maps voltage (0-5V range) to phase increments with optional quantization
     */
    void initVOctLUT() {
        const float voltsPerEntry = VOCT_LUT_OCTAVES / VOCT_LUT_SIZE;
        
        for (uint16_t i = 0; i < VOCT_LUT_SIZE; i++) {
            float voltage = i * voltsPerEntry;
            float frequency = baseFrequency * powf(2.0f, voltage);  // f = f0 * 2^V
            
            if (quantizeToSemitones) {
                frequency = quantizeFrequencyToSemitone(frequency);
            }
            
            // Convert to 32-bit phase increment
            // Phase increment = (frequency / sampleRate) * 2^32
            vOctLUT[i] = (uint32_t)((frequency / sampleRate) * 4294967296.0f);
        }
        
        // Pre-calculate ADC to LUT mapping constants
        uint32_t adcRange = adcMax - adcMin;
        adcOffset = adcMin;
        // Scale to map full ADC range to LUT indices (using 16.16 fixed-point)
        // We map 3.3V (full ADC range) to 5 octaves (full LUT range)
        float scaleFactor = (VOCT_LUT_SIZE * (VOCT_LUT_OCTAVES / 3.3f)) / (float)adcRange;
        adcToLutScale = (uint32_t)(scaleFactor * 65536.0f);  // Convert to 16.16 fixed-point
    }
    
    /**
     * Initialize FM modulation lookup table
     * Creates exponential curves for musical FM depth control
     */
    void initFMModLUT(bool lfoMode = false) {
        // Generate exponential modulation curves
        // Centered at FM_LUT_CENTER (2048) with positive and negative sides
        
        for (uint16_t i = 0; i < FM_LUT_CENTER; i++) {
            float fraction = (float)i / (float)(FM_LUT_CENTER - 1);
            
            if (lfoMode) {
                // LFO mode: Quadratic curve for finer control at low modulation depths
                float modDepth = powf(fraction, 2.0f);
                int32_t modValue = (int32_t)(modDepth * (1 << 24));  // Lower scaling for fine control
                fmModLUT[FM_LUT_CENTER + i] = modValue;
                fmModLUT[FM_LUT_CENTER - i - 1] = -modValue;
            } else {
                // Audio mode: Linear to exponential curve for musical FM
                float modDepth = powf(fraction, 1.5f);  // Slightly exponential for musical response
                int32_t modValue = (int32_t)(modDepth * (1 << 28));  // Higher scaling for audio-rate FM
                fmModLUT[FM_LUT_CENTER + i] = modValue;
                fmModLUT[FM_LUT_CENTER - i - 1] = -modValue;
            }
        }
    }
    
    /**
     * Quantize frequency to nearest equal-tempered semitone
     */
    float quantizeFrequencyToSemitone(float frequency) const {
        const float semitoneRatio = 1.059463094359f;  // 2^(1/12)
        float octaves = log2f(frequency / baseFrequency);
        int semitones = roundf(octaves * 12.0f);
        return baseFrequency * powf(semitoneRatio, (float)semitones);
    }
    
public:
    /**
     * Constructor - Initialize with system parameters
     * 
     * @param sampleRate Audio sample rate in Hz
     * @param baseFreq Base frequency at 0V (typically 16.35Hz for C0)
     * @param adcMin Minimum usable ADC value (compensates for end loss)
     * @param adcMax Maximum usable ADC value (compensates for end loss)
     */
    FrequencyCalculator(float sampleRate = 48000.0f, 
                       float baseFreq = 16.35f,
                       uint16_t adcMin = 30, 
                       uint16_t adcMax = 4080) 
        : sampleRate(sampleRate), 
          baseFrequency(baseFreq),
          adcMin(adcMin), 
          adcMax(adcMax),
          quantizeToSemitones(false) {
        
        initVOctLUT();
        initFMModLUT(false);  // Default to audio mode
    }
    
    /**
     * Main calculation function - converts ADC values to phase increment
     * Call this from your audio ISR for each sample
     * 
     * @param vOctADC V/oct input ADC value (0-4095)
     * @param fmADC FM input ADC value (0-4095, centered at 2048)
     * @param octaveShift Octave transposition (-8 to +8)
     * @return 32-bit phase increment value
     */
    inline uint32_t calculatePhaseIncrement(uint16_t vOctADC, 
                                           uint16_t fmADC = 2048,
                                           int8_t octaveShift = 0) const {
        // 1. Clamp and scale V/oct ADC to LUT index
        uint16_t clampedADC = constrain(vOctADC, adcMin, adcMax);
        uint32_t scaledADC = (clampedADC - adcOffset) * adcToLutScale;  // 16.16 fixed-point
        uint16_t lutIndex = scaledADC >> 16;     // Integer part
        uint16_t lutFraction = scaledADC;        // Fractional part (lower 16 bits)
        
        // 2. Handle LUT bounds and interpolation
        uint32_t baseIncrement;
        
        if (lutIndex >= VOCT_LUT_SIZE - 1) {
            // Beyond LUT range - use last entry and apply octave scaling
            baseIncrement = vOctLUT[VOCT_LUT_SIZE - 1];
            
            // Calculate additional octaves beyond LUT
            uint16_t excessIndices = lutIndex - (VOCT_LUT_SIZE - 1);
            uint8_t extraOctaves = excessIndices / (VOCT_LUT_SIZE / VOCT_LUT_OCTAVES);
            
            // Each octave doubles the frequency (left shift by 1)
            baseIncrement <<= extraOctaves;
        } else {
            // Linear interpolation between adjacent LUT entries
            uint32_t inc0 = vOctLUT[lutIndex];
            uint32_t inc1 = vOctLUT[lutIndex + 1];
            
            // Fixed-point interpolation: result = inc0 + ((inc1 - inc0) * fraction) >> 16
            int32_t delta = (int32_t)(inc1 - inc0);
            baseIncrement = inc0 + ((delta * lutFraction) >> 16);
        }
        
        // 3. Apply octave transposition
        if (octaveShift > 0) {
            baseIncrement <<= octaveShift;  // Each octave up doubles frequency
        } else if (octaveShift < 0) {
            baseIncrement >>= (-octaveShift);  // Each octave down halves frequency
        }
        
        // 4. Apply FM modulation
        if (fmADC != 2048) {  // Skip if no modulation
            // Center FM input around zero
            int16_t centeredFM = fmADC - 2048;
            uint16_t absFM = abs(centeredFM);
            
            // Clamp to valid LUT range
            if (absFM >= FM_LUT_CENTER) {
                absFM = FM_LUT_CENTER - 1;
            }
            
            // Look up modulation amount and restore sign
            int32_t modAmount = fmModLUT[FM_LUT_CENTER + absFM];
            if (centeredFM < 0) {
                modAmount = -modAmount;
            }
            
            // Apply modulation with saturation protection
            int64_t modulated = (int64_t)baseIncrement + modAmount;
            
            // Clamp to valid 32-bit range
            if (modulated < 0) {
                return 0;
            } else if (modulated > 0xFFFFFFFF) {
                return 0xFFFFFFFF;
            } else {
                return (uint32_t)modulated;
            }
        }
        
        return baseIncrement;
    }
    
    /**
     * Set base frequency (the frequency at 0V input)
     * Common values: 16.35Hz (C0), 32.70Hz (C1), 8.18Hz (C-1)
     * 
     * @param freq Base frequency in Hz
     */
    void setBaseFrequency(float freq) {
        baseFrequency = freq;
        initVOctLUT();  // Rebuild LUT with new base
    }
    
    /**
     * Set audio sample rate
     * 
     * @param rate Sample rate in Hz
     */
    void setSampleRate(float rate) {
        sampleRate = rate;
        initVOctLUT();  // Rebuild LUT for new rate
    }
    
    /**
     * Configure ADC range compensation
     * Use this to account for voltage reference inaccuracies or end loss
     * 
     * @param min Minimum usable ADC value
     * @param max Maximum usable ADC value
     */
    void setADCRange(uint16_t min, uint16_t max) {
        adcMin = min;
        adcMax = max;
        initVOctLUT();  // Recalculate scaling
    }
    
    /**
     * Enable/disable semitone quantization
     * When enabled, frequencies are rounded to nearest equal-tempered semitone
     * 
     * @param quantize Enable quantization
     */
    void setQuantization(bool quantize) {
        quantizeToSemitones = quantize;
        initVOctLUT();  // Rebuild LUT with quantization
    }
    
    /**
     * Configure for LFO mode operation
     * Adjusts FM scaling and base frequency for low-frequency operation
     * 
     * @param baseLFOFreq Base LFO frequency (e.g., 0.1Hz)
     */
    void setLFOMode(float baseLFOFreq = 0.1f) {
        baseFrequency = baseLFOFreq;
        initVOctLUT();
        initFMModLUT(true);  // Use LFO-optimized FM curves
    }
    
    /**
     * Configure for audio mode operation
     * 
     * @param baseAudioFreq Base frequency (e.g., 16.35Hz for C0)
     */
    void setAudioMode(float baseAudioFreq = 16.35f) {
        baseFrequency = baseAudioFreq;
        initVOctLUT();
        initFMModLUT(false);  // Use audio-optimized FM curves
    }
    
    /**
     * Utility: Convert phase increment back to frequency
     * Useful for display/debugging
     * 
     * @param phaseIncrement 32-bit phase increment
     * @return Frequency in Hz
     */
    float getFrequencyHz(uint32_t phaseIncrement) const {
        return (phaseIncrement * sampleRate) / 4294967296.0f;  // Divide by 2^32
    }
    
    /**
     * Utility: Get period from phase increment
     * Useful for LFO displays
     * 
     * @param phaseIncrement 32-bit phase increment
     * @return Period in seconds
     */
    float getPeriodSeconds(uint32_t phaseIncrement) const {
        float freq = getFrequencyHz(phaseIncrement);
        return (freq > 0.0f) ? (1.0f / freq) : 0.0f;
    }
};

#endif // FREQUENCY_CALCULATOR_H