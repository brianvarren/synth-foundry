/**
 * @file ladder_filter.h
 * @brief 8-pole ladder filters using fixed-point math
 * 
 * This header provides 8-pole ladder lowpass and highpass filters
 * implemented using fixed-point arithmetic for real-time audio processing.
 * The filters are based on the classic Moog ladder filter design but extended
 * to 8 poles for more aggressive filtering characteristics.
 * 
 * ## Filter Types
 * 
 * **8-Pole Lowpass Filter**: Cascaded 8-pole ladder filter that attenuates high frequencies
 * **8-Pole Highpass Filter**: Cascaded 8-pole ladder filter that attenuates low frequencies
 * 
 * ## Fixed-Point Math
 * 
 * All calculations use Q15 fixed-point format (16-bit signed integers) for
 * consistent performance and compatibility with the existing audio engine.
 * The filters use a ladder structure with proper state management for
 * smooth bypass transitions.
 * 
 * ## Usage
 * 
 * The filters are designed to be controlled by ADC inputs (ADC5 for lowpass,
 * ADC6 for highpass) and applied to the audio output in real-time.
 * 
 * @author Brian Varren
 * @version 1.0
 * @date 2024
 */

#pragma once
#include <stdint.h>

/**
 * @class Ladder8PoleLowpassFilter
 * @brief 8-pole ladder lowpass filter using fixed-point math
 * 
 * Implements an 8-pole ladder filter based on the Moog ladder design.
 * Each pole is a first-order lowpass filter cascaded together.
 * The cutoff frequency is controlled by the filter coefficient (0-32767).
 * Higher values = higher cutoff frequency.
 */
class Ladder8PoleLowpassFilter {
public:
    Ladder8PoleLowpassFilter() : 
        pole1(0), pole2(0), pole3(0), pole4(0), 
        pole5(0), pole6(0), pole7(0), pole8(0),
        initialized(false), last_coefficient(0) {}
    
    /**
     * @brief Process one audio sample through the 8-pole lowpass filter
     * @param input Input sample in Q15 format (-32768 to +32767)
     * @param coefficient Filter coefficient (0-32767, higher = higher cutoff)
     * @return Filtered output sample in Q15 format
     */
    inline int16_t process(int16_t input, uint16_t coefficient) {
        // If coefficient is 0, bypass the filter and reset state
        if (coefficient == 0) {
            if (last_coefficient != 0) {
                // Reset all poles when transitioning from active to bypass
                pole1 = pole2 = pole3 = pole4 = 0;
                pole5 = pole6 = pole7 = pole8 = 0;
                initialized = false;
            }
            last_coefficient = 0;
            return input;
        }
        
        // Initialize with current input if first time
        if (!initialized) {
            pole1 = pole2 = pole3 = pole4 = input;
            pole5 = pole6 = pole7 = pole8 = input;
            initialized = true;
        }
        
        // Process through 8 cascaded poles
        // Each pole: output = prev_output + coefficient * (input - prev_output) / 32768
        int32_t diff1 = (int32_t)input - (int32_t)pole1;
        int32_t scaled_diff1 = ((int64_t)diff1 * (int64_t)coefficient) >> 15;
        pole1 = pole1 + (int16_t)scaled_diff1;
        
        int32_t diff2 = (int32_t)pole1 - (int32_t)pole2;
        int32_t scaled_diff2 = ((int64_t)diff2 * (int64_t)coefficient) >> 15;
        pole2 = pole2 + (int16_t)scaled_diff2;
        
        int32_t diff3 = (int32_t)pole2 - (int32_t)pole3;
        int32_t scaled_diff3 = ((int64_t)diff3 * (int64_t)coefficient) >> 15;
        pole3 = pole3 + (int16_t)scaled_diff3;
        
        int32_t diff4 = (int32_t)pole3 - (int32_t)pole4;
        int32_t scaled_diff4 = ((int64_t)diff4 * (int64_t)coefficient) >> 15;
        pole4 = pole4 + (int16_t)scaled_diff4;
        
        int32_t diff5 = (int32_t)pole4 - (int32_t)pole5;
        int32_t scaled_diff5 = ((int64_t)diff5 * (int64_t)coefficient) >> 15;
        pole5 = pole5 + (int16_t)scaled_diff5;
        
        int32_t diff6 = (int32_t)pole5 - (int32_t)pole6;
        int32_t scaled_diff6 = ((int64_t)diff6 * (int64_t)coefficient) >> 15;
        pole6 = pole6 + (int16_t)scaled_diff6;
        
        int32_t diff7 = (int32_t)pole6 - (int32_t)pole7;
        int32_t scaled_diff7 = ((int64_t)diff7 * (int64_t)coefficient) >> 15;
        pole7 = pole7 + (int16_t)scaled_diff7;
        
        int32_t diff8 = (int32_t)pole7 - (int32_t)pole8;
        int32_t scaled_diff8 = ((int64_t)diff8 * (int64_t)coefficient) >> 15;
        pole8 = pole8 + (int16_t)scaled_diff8;
        
        last_coefficient = coefficient;
        return pole8;
    }
    
    /**
     * @brief Reset the filter state
     */
    inline void reset() {
        pole1 = pole2 = pole3 = pole4 = 0;
        pole5 = pole6 = pole7 = pole8 = 0;
        initialized = false;
        last_coefficient = 0;
    }
    
private:
    int16_t pole1, pole2, pole3, pole4;  // First 4 poles
    int16_t pole5, pole6, pole7, pole8;  // Last 4 poles
    bool initialized;
    uint16_t last_coefficient;  // Track coefficient changes for proper bypass
};

/**
 * @class Ladder8PoleHighpassFilter
 * @brief 8-pole ladder highpass filter using fixed-point math
 * 
 * Implements an 8-pole highpass filter using a more efficient design.
 * Instead of cascading 8 highpass stages (which causes too much attenuation),
 * this uses a combination of lowpass stages and subtraction to create highpass.
 * The cutoff frequency is controlled by the filter coefficient (0-32767).
 * Higher values = higher cutoff frequency.
 */
class Ladder8PoleHighpassFilter {
public:
    Ladder8PoleHighpassFilter() : 
        pole1(0), pole2(0), pole3(0), pole4(0), 
        pole5(0), pole6(0), pole7(0), pole8(0),
        initialized(false), last_coefficient(0) {}
    
    /**
     * @brief Process one audio sample through the 8-pole highpass filter
     * @param input Input sample in Q15 format (-32768 to +32767)
     * @param coefficient Filter coefficient (0-32767, higher = higher cutoff)
     * @return Filtered output sample in Q15 format
     */
    inline int16_t process(int16_t input, uint16_t coefficient) {
        // If coefficient is 0, bypass the filter and reset state
        if (coefficient == 0) {
            if (last_coefficient != 0) {
                // Reset all poles when transitioning from active to bypass
                pole1 = pole2 = pole3 = pole4 = 0;
                pole5 = pole6 = pole7 = pole8 = 0;
                initialized = false;
            }
            last_coefficient = 0;
            return input;
        }
        
        // Initialize with current input if first time
        if (!initialized) {
            pole1 = pole2 = pole3 = pole4 = input;
            pole5 = pole6 = pole7 = pole8 = input;
            initialized = true;
        }
        
        // Process through 8 cascaded lowpass poles to get the low frequencies
        // Then subtract from input to get high frequencies
        int16_t current = input;
        
        // First 4 poles
        int32_t diff1 = (int32_t)current - (int32_t)pole1;
        int32_t scaled_diff1 = ((int64_t)diff1 * (int64_t)coefficient) >> 15;
        pole1 = pole1 + (int16_t)scaled_diff1;
        current = pole1;
        
        int32_t diff2 = (int32_t)current - (int32_t)pole2;
        int32_t scaled_diff2 = ((int64_t)diff2 * (int64_t)coefficient) >> 15;
        pole2 = pole2 + (int16_t)scaled_diff2;
        current = pole2;
        
        int32_t diff3 = (int32_t)current - (int32_t)pole3;
        int32_t scaled_diff3 = ((int64_t)diff3 * (int64_t)coefficient) >> 15;
        pole3 = pole3 + (int16_t)scaled_diff3;
        current = pole3;
        
        int32_t diff4 = (int32_t)current - (int32_t)pole4;
        int32_t scaled_diff4 = ((int64_t)diff4 * (int64_t)coefficient) >> 15;
        pole4 = pole4 + (int16_t)scaled_diff4;
        current = pole4;
        
        // Last 4 poles
        int32_t diff5 = (int32_t)current - (int32_t)pole5;
        int32_t scaled_diff5 = ((int64_t)diff5 * (int64_t)coefficient) >> 15;
        pole5 = pole5 + (int16_t)scaled_diff5;
        current = pole5;
        
        int32_t diff6 = (int32_t)current - (int32_t)pole6;
        int32_t scaled_diff6 = ((int64_t)diff6 * (int64_t)coefficient) >> 15;
        pole6 = pole6 + (int16_t)scaled_diff6;
        current = pole6;
        
        int32_t diff7 = (int32_t)current - (int32_t)pole7;
        int32_t scaled_diff7 = ((int64_t)diff7 * (int64_t)coefficient) >> 15;
        pole7 = pole7 + (int16_t)scaled_diff7;
        current = pole7;
        
        int32_t diff8 = (int32_t)current - (int32_t)pole8;
        int32_t scaled_diff8 = ((int64_t)diff8 * (int64_t)coefficient) >> 15;
        pole8 = pole8 + (int16_t)scaled_diff8;
        current = pole8;
        
        // Highpass = input - lowpass (current contains the lowpass result)
        int32_t highpass_result = (int32_t)input - (int32_t)current;
        
        // Clamp to prevent overflow
        if (highpass_result > 32767) highpass_result = 32767;
        if (highpass_result < -32768) highpass_result = -32768;
        
        last_coefficient = coefficient;
        return (int16_t)highpass_result;
    }
    
    /**
     * @brief Reset the filter state
     */
    inline void reset() {
        pole1 = pole2 = pole3 = pole4 = 0;
        pole5 = pole6 = pole7 = pole8 = 0;
        initialized = false;
        last_coefficient = 0;
    }
    
private:
    int16_t pole1, pole2, pole3, pole4;  // First 4 poles
    int16_t pole5, pole6, pole7, pole8;  // Last 4 poles
    bool initialized;
    uint16_t last_coefficient;  // Track coefficient changes for proper bypass
};

/**
 * @brief Convert ADC value (0-4095) to filter coefficient (0-32767)
 * 
 * Maps a 12-bit ADC value to a filter coefficient for controlling
 * the filter cutoff frequency. Uses a logarithmic mapping for
 * more musical control response.
 * 
 * @param adc_value 12-bit ADC value (0-4095)
 * @return Filter coefficient (0-32767)
 */
inline uint16_t adc_to_ladder_coefficient(uint16_t adc_value) {
    // Map 0-4095 to 0-32767 with smooth response
    // This gives musical control over the filter frequency without sudden jumps
    
    // Simple linear mapping for now to prevent pops
    // TODO: Implement proper logarithmic curve later if needed
    uint32_t result = ((uint32_t)adc_value * 32767) / 4095;
    
    // Ensure minimum coefficient for proper filtering even at lowest setting
    if (result < 512) result = 512;  // Minimum coefficient for gentle filtering
    if (result > 32767) result = 32767;  // Maximum coefficient
    
    return (uint16_t)result;
}

/**
 * @brief Convert ADC value to filter coefficient with linear mapping
 * 
 * Alternative linear mapping for more predictable control.
 * 
 * @param adc_value 12-bit ADC value (0-4095)
 * @return Filter coefficient (0-32767)
 */
inline uint16_t adc_to_ladder_coefficient_linear(uint16_t adc_value) {
    // Simple linear mapping: 0-4095 -> 0-32767
    return (uint16_t)(((uint32_t)adc_value * 32767) / 4095);
}
