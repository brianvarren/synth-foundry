/**
 * @file ladder_filter.h
 * @brief 8-pole ladder bandpass filter using fixed-point math
 * 
 * This header provides an 8-pole ladder bandpass filter implemented using 
 * fixed-point arithmetic for real-time audio processing. The filter is based 
 * on the classic Moog ladder filter design but extended to 8 poles for more 
 * aggressive filtering characteristics.
 * 
 * ## Filter Type
 * 
 * **8-Pole Bandpass Filter**: Cascaded 8-pole ladder filter that passes a 
 * specific frequency band while attenuating frequencies above and below the band.
 * The filter uses a combination of lowpass and highpass stages to create the
 * bandpass response.
 * 
 * ## Fixed-Point Math
 * 
 * All calculations use Q15 fixed-point format (16-bit signed integers) for
 * consistent performance and compatibility with the existing audio engine.
 * The filter uses a ladder structure with proper state management for
 * smooth bypass transitions.
 * 
 * ## Usage
 * 
 * The filter is designed to be controlled by ADC inputs:
 * - ADC5: Controls the center frequency (cutoff) of the bandpass filter
 * - ADC6: Controls the bandwidth (Q factor) of the bandpass filter
 * 
 * @author Brian Varren
 * @version 1.0
 * @date 2024
 */

#pragma once
#include <stdint.h>

/**
 * @class Ladder8PoleBandpassFilter
 * @brief 8-pole ladder bandpass filter using fixed-point math
 * 
 * Implements an 8-pole bandpass filter using a combination of lowpass and highpass
 * stages. The filter uses 4 poles for lowpass filtering and 4 poles for highpass
 * filtering to create a bandpass response. The center frequency and bandwidth
 * are independently controllable.
 * 
 * The bandpass filter is created by:
 * 1. Processing the input through 4 lowpass poles (removes high frequencies)
 * 2. Processing the result through 4 highpass poles (removes low frequencies)
 * 3. The center frequency is controlled by the cutoff coefficient
 * 4. The bandwidth is controlled by the Q coefficient (bandwidth = cutoff / Q)
 */
class Ladder8PoleBandpassFilter {
public:
    Ladder8PoleBandpassFilter() : 
        // Lowpass poles (first 4)
        lp_pole1(0), lp_pole2(0), lp_pole3(0), lp_pole4(0),
        // Highpass poles (last 4) 
        hp_pole1(0), hp_pole2(0), hp_pole3(0), hp_pole4(0),
        initialized(false), last_cutoff_coeff(0), last_q_coeff(0) {}
    
    /**
     * @brief Process one audio sample through the 8-pole bandpass filter
     * @param input Input sample in Q15 format (-32768 to +32767)
     * @param cutoff_coeff Filter cutoff coefficient (0-32767, higher = higher center frequency)
     * @param q_coeff Filter Q coefficient (0-32767, higher = narrower bandwidth)
     * @return Filtered output sample in Q15 format
     */
    inline int16_t process(int16_t input, uint16_t cutoff_coeff, uint16_t q_coeff) {
        // If cutoff is 0, bypass the filter and reset state
        if (cutoff_coeff == 0) {
            if (last_cutoff_coeff != 0) {
                // Reset all poles when transitioning from active to bypass
                lp_pole1 = lp_pole2 = lp_pole3 = lp_pole4 = 0;
                hp_pole1 = hp_pole2 = hp_pole3 = hp_pole4 = 0;
                initialized = false;
            }
            last_cutoff_coeff = 0;
            last_q_coeff = 0;
            return input;
        }
        
        // Initialize with current input if first time
        if (!initialized) {
            lp_pole1 = lp_pole2 = lp_pole3 = lp_pole4 = input;
            hp_pole1 = hp_pole2 = hp_pole3 = hp_pole4 = input;
            initialized = true;
        }
        
        // Calculate effective coefficients based on Q factor
        // Higher Q = narrower bandwidth = lower effective cutoff for highpass
        uint16_t lp_cutoff = cutoff_coeff;
        uint16_t hp_cutoff = cutoff_coeff;
        
        // Ensure minimum cutoff for stability
        if (lp_cutoff < 1024) lp_cutoff = 1024;  // Higher minimum for stability
        if (lp_cutoff > 32767) lp_cutoff = 32767;
        
        // Adjust highpass cutoff based on Q to control bandwidth
        // Q = 0 means very wide bandwidth (highpass cutoff much lower than lowpass)
        // Q = 32767 means very narrow bandwidth (highpass cutoff close to lowpass)
        if (q_coeff > 0) {
            // Map Q from 0-32767 to a multiplier from 0.3 to 0.8
            // This creates bandwidth control: higher Q = narrower band
            // Use very conservative range to prevent instability and pops
            uint32_t q_mult = 9830 + ((uint32_t)q_coeff * 16384) / 32767;  // 9830 to 26214 (0.3 to 0.8)
            hp_cutoff = (uint16_t)(((uint32_t)lp_cutoff * q_mult) >> 15);
            if (hp_cutoff > lp_cutoff) hp_cutoff = lp_cutoff;
        } else {
            // When Q is 0, make highpass cutoff much lower for wide bandwidth
            hp_cutoff = lp_cutoff >> 2;  // Much lower cutoff for wide bandwidth
        }
        
        // Ensure minimum highpass cutoff for stability
        if (hp_cutoff < 512) hp_cutoff = 512;
        if (hp_cutoff > lp_cutoff) hp_cutoff = lp_cutoff;
        
        // Additional safety: ensure highpass cutoff is not too close to lowpass cutoff
        // This prevents extreme narrow bandwidth that could cause instability
        uint16_t min_gap = lp_cutoff >> 4;  // Minimum gap between cutoffs
        if (hp_cutoff > (lp_cutoff - min_gap)) {
            hp_cutoff = (lp_cutoff > min_gap) ? (lp_cutoff - min_gap) : (lp_cutoff >> 1);
        }
        
        // ── Lowpass Stage (4 poles) ──────────────────────────────────────────────
        // Process through 4 cascaded lowpass poles to remove high frequencies
        int16_t current = input;
        
        int32_t diff1 = (int32_t)current - (int32_t)lp_pole1;
        int32_t scaled_diff1 = ((int64_t)diff1 * (int64_t)lp_cutoff) >> 15;
        int32_t new_pole1 = (int32_t)lp_pole1 + scaled_diff1;
        // Clamp pole values to prevent overflow
        if (new_pole1 > 32767) new_pole1 = 32767;
        if (new_pole1 < -32768) new_pole1 = -32768;
        lp_pole1 = (int16_t)new_pole1;
        current = lp_pole1;
        
        int32_t diff2 = (int32_t)current - (int32_t)lp_pole2;
        int32_t scaled_diff2 = ((int64_t)diff2 * (int64_t)lp_cutoff) >> 15;
        int32_t new_pole2 = (int32_t)lp_pole2 + scaled_diff2;
        if (new_pole2 > 32767) new_pole2 = 32767;
        if (new_pole2 < -32768) new_pole2 = -32768;
        lp_pole2 = (int16_t)new_pole2;
        current = lp_pole2;
        
        int32_t diff3 = (int32_t)current - (int32_t)lp_pole3;
        int32_t scaled_diff3 = ((int64_t)diff3 * (int64_t)lp_cutoff) >> 15;
        int32_t new_pole3 = (int32_t)lp_pole3 + scaled_diff3;
        if (new_pole3 > 32767) new_pole3 = 32767;
        if (new_pole3 < -32768) new_pole3 = -32768;
        lp_pole3 = (int16_t)new_pole3;
        current = lp_pole3;
        
        int32_t diff4 = (int32_t)current - (int32_t)lp_pole4;
        int32_t scaled_diff4 = ((int64_t)diff4 * (int64_t)lp_cutoff) >> 15;
        int32_t new_pole4 = (int32_t)lp_pole4 + scaled_diff4;
        if (new_pole4 > 32767) new_pole4 = 32767;
        if (new_pole4 < -32768) new_pole4 = -32768;
        lp_pole4 = (int16_t)new_pole4;
        current = lp_pole4;
        
        // ── Highpass Stage (4 poles) ─────────────────────────────────────────────
        // Process through 4 cascaded highpass poles to remove low frequencies
        // Highpass is implemented as: output = input - lowpass(input)
        int16_t hp_input = current;  // Input to highpass stage
        
        // First highpass pole
        int32_t hp_diff1 = (int32_t)hp_input - (int32_t)hp_pole1;
        int32_t hp_scaled_diff1 = ((int64_t)hp_diff1 * (int64_t)hp_cutoff) >> 15;
        int32_t new_hp_pole1 = (int32_t)hp_pole1 + hp_scaled_diff1;
        if (new_hp_pole1 > 32767) new_hp_pole1 = 32767;
        if (new_hp_pole1 < -32768) new_hp_pole1 = -32768;
        hp_pole1 = (int16_t)new_hp_pole1;
        current = hp_pole1;
        
        // Second highpass pole
        int32_t hp_diff2 = (int32_t)current - (int32_t)hp_pole2;
        int32_t hp_scaled_diff2 = ((int64_t)hp_diff2 * (int64_t)hp_cutoff) >> 15;
        int32_t new_hp_pole2 = (int32_t)hp_pole2 + hp_scaled_diff2;
        if (new_hp_pole2 > 32767) new_hp_pole2 = 32767;
        if (new_hp_pole2 < -32768) new_hp_pole2 = -32768;
        hp_pole2 = (int16_t)new_hp_pole2;
        current = hp_pole2;
        
        // Third highpass pole
        int32_t hp_diff3 = (int32_t)current - (int32_t)hp_pole3;
        int32_t hp_scaled_diff3 = ((int64_t)hp_diff3 * (int64_t)hp_cutoff) >> 15;
        int32_t new_hp_pole3 = (int32_t)hp_pole3 + hp_scaled_diff3;
        if (new_hp_pole3 > 32767) new_hp_pole3 = 32767;
        if (new_hp_pole3 < -32768) new_hp_pole3 = -32768;
        hp_pole3 = (int16_t)new_hp_pole3;
        current = hp_pole3;
        
        // Fourth highpass pole
        int32_t hp_diff4 = (int32_t)current - (int32_t)hp_pole4;
        int32_t hp_scaled_diff4 = ((int64_t)hp_diff4 * (int64_t)hp_cutoff) >> 15;
        int32_t new_hp_pole4 = (int32_t)hp_pole4 + hp_scaled_diff4;
        if (new_hp_pole4 > 32767) new_hp_pole4 = 32767;
        if (new_hp_pole4 < -32768) new_hp_pole4 = -32768;
        hp_pole4 = (int16_t)new_hp_pole4;
        current = hp_pole4;
        
        // Highpass = input - lowpass (current contains the lowpass result)
        int32_t highpass_result = (int32_t)hp_input - (int32_t)current;
        
        // Clamp to prevent overflow with more conservative limits
        if (highpass_result > 32767) highpass_result = 32767;
        if (highpass_result < -32768) highpass_result = -32768;
        
        last_cutoff_coeff = cutoff_coeff;
        last_q_coeff = q_coeff;
        return (int16_t)highpass_result;
    }
    
    /**
     * @brief Reset the filter state
     */
    inline void reset() {
        lp_pole1 = lp_pole2 = lp_pole3 = lp_pole4 = 0;
        hp_pole1 = hp_pole2 = hp_pole3 = hp_pole4 = 0;
        initialized = false;
        last_cutoff_coeff = 0;
        last_q_coeff = 0;
    }
    
private:
    // Lowpass poles (first 4 stages)
    int16_t lp_pole1, lp_pole2, lp_pole3, lp_pole4;
    // Highpass poles (last 4 stages) 
    int16_t hp_pole1, hp_pole2, hp_pole3, hp_pole4;
    bool initialized;
    uint16_t last_cutoff_coeff;  // Track coefficient changes for proper bypass
    uint16_t last_q_coeff;       // Track Q changes for proper bypass
};

/**
 * @brief Convert ADC value (0-4095) to filter cutoff coefficient (0-32767)
 * 
 * Maps a 12-bit ADC value to a filter coefficient for controlling
 * the bandpass filter center frequency. Uses conservative mapping to
 * prevent pops and instability.
 * 
 * @param adc_value 12-bit ADC value (0-4095)
 * @return Filter cutoff coefficient (0-32767)
 */
inline uint16_t adc_to_bandpass_cutoff(uint16_t adc_value) {
    // Map 0-4095 to 1024-32767 with smooth response
    // This gives musical control over the filter frequency without sudden jumps
    // Use conservative range to prevent instability
    
    // Linear mapping with higher minimum to prevent pops
    uint32_t result = 1024 + ((uint32_t)adc_value * 31743) / 4095;  // 1024 to 32767
    
    // Ensure safe coefficient range
    if (result < 1024) result = 1024;   // Minimum coefficient for stability
    if (result > 32767) result = 32767; // Maximum coefficient
    
    return (uint16_t)result;
}

/**
 * @brief Convert ADC value (0-4095) to filter Q coefficient (0-32767)
 * 
 * Maps a 12-bit ADC value to a Q coefficient for controlling
 * the bandpass filter bandwidth. Higher values = narrower bandwidth.
 * Uses conservative mapping to prevent extreme values that could cause pops.
 * 
 * @param adc_value 12-bit ADC value (0-4095)
 * @return Filter Q coefficient (0-32767)
 */
inline uint16_t adc_to_bandpass_q(uint16_t adc_value) {
    // Map 0-4095 to 512-24576 for Q control (more conservative range)
    // Q controls bandwidth: higher Q = narrower bandwidth
    // Use conservative range to prevent instability
    uint32_t result = 512 + ((uint32_t)adc_value * 24064) / 4095;  // 512 to 24576
    
    // Ensure safe Q range
    if (result < 512) result = 512;     // Minimum Q for some bandwidth control
    if (result > 24576) result = 24576; // Maximum Q to prevent extreme narrow bandwidth
    
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
