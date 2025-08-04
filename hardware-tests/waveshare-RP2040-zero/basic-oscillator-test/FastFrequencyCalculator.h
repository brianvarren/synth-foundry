#ifndef FAST_FREQUENCY_CALCULATOR_H
#define FAST_FREQUENCY_CALCULATOR_H

#include <Arduino.h>

// Optimized frequency calculator for real-time audio
class FastFrequencyCalculator {
private:
    // Constants for V/oct standard
    static constexpr float VOLTS_PER_OCTAVE = 1.0f;
    static constexpr uint16_t OCTAVES_IN_LUT = 5;
    static constexpr uint16_t ENTRIES_PER_OCTAVE = 120;  // 10 cents resolution
    static constexpr uint16_t LUT_SIZE = OCTAVES_IN_LUT * ENTRIES_PER_OCTAVE;
    static constexpr uint8_t LUT_BITS = 10;  // ceil(log2(600))
    
    // Pre-calculated constants to avoid division in ISR
    uint32_t adcToLutScale;      // Fixed-point scalar for ADC->LUT mapping
    int32_t adcOffset;           // Signed for proper underflow handling
    uint16_t adcRange;           // Store for bounds checking
    
    // V/oct LUT storing phase increments (32-bit fixed point)
    uint32_t vOctLUT[LUT_SIZE];
    
    // FM modulation parameters
    const int32_t* fmLUT;
    uint16_t fmLUTSize;
    uint16_t fmLUTMask;      // For fast modulo (size-1)
    uint16_t fmCenterValue;  // ADC center value (typically 2048 for 12-bit)
    
    // Base frequency for calibration
    float baseFrequency;
    float sampleRate;
    
public:
    FastFrequencyCalculator() : 
        fmLUT(nullptr), 
        fmLUTSize(0), 
        fmLUTMask(0),
        fmCenterValue(2048) {}
    
    // Initialize with pre-calculated values
    void init(float sampleRate_, float baseFreq, uint16_t adcMin, uint16_t adcMax, 
              bool quantize = false, uint16_t fmCenter = 2048) {
        sampleRate = sampleRate_;
        baseFrequency = baseFreq;
        fmCenterValue = fmCenter;
        
        // Calculate ADC scaling factors with bounds checking
        adcRange = adcMax - adcMin;
        if (adcRange == 0) adcRange = 1;  // Prevent division by zero
        
        adcOffset = adcMin;
        
        // Scale to LUT indices with 16-bit fractional part
        // Use 64-bit intermediate to prevent overflow
        adcToLutScale = (uint32_t)(((uint64_t)LUT_SIZE << 16) / adcRange);
        
        // Build V/oct LUT
        const float voltsPerEntry = (float)OCTAVES_IN_LUT / LUT_SIZE;
        
        for (uint16_t i = 0; i < LUT_SIZE; i++) {
            float octaves = i * voltsPerEntry;
            float frequency = baseFreq * powf(2.0f, octaves);
            
            if (quantize) {
                // Quantize to nearest semitone
                int semitones = roundf(octaves * 12.0f);
                frequency = baseFreq * powf(2.0f, semitones / 12.0f);
            }
            
            // Store phase increment as 32-bit fixed point
            // phaseInc = (frequency / sampleRate) * 2^32
            vOctLUT[i] = (uint32_t)((frequency / sampleRate) * 4294967296.0);
        }
    }
    
    // Set external FM LUT (must be power-of-2 size)
    void setFMLUT(const int32_t* lut, uint16_t size) {
        if (lut && (size & (size - 1)) == 0) {  // Check power of 2
            fmLUT = lut;
            fmLUTSize = size;
            fmLUTMask = size - 1;
        }
    }
    
    // Get current frequency in Hz (for display/debugging)
    float getFrequency(uint16_t vOctADC, int8_t octaveShift = 0) const {
        uint32_t phaseInc = getPhaseIncrementNoFM(vOctADC, octaveShift);
        return (phaseInc / 4294967296.0f) * sampleRate;
    }
    
    // Ultra-fast phase increment calculation with FM
    inline uint32_t __attribute__((always_inline)) 
    getPhaseIncrement(uint16_t vOctADC, uint16_t fmADC, int8_t octaveShift) const {
        // 1. Bounds check ADC input
        int32_t clampedADC = vOctADC;
        if (clampedADC < adcOffset) clampedADC = adcOffset;
        if (clampedADC > adcOffset + adcRange) clampedADC = adcOffset + adcRange;
        
        // 2. Map ADC to LUT index using pre-calculated scale
        uint32_t scaledADC = (clampedADC - adcOffset) * adcToLutScale;
        uint16_t lutIndex = scaledADC >> 16;     // Integer part
        uint16_t lutFrac = scaledADC & 0xFFFF;   // Fractional part for interpolation
        
        // 3. Get base phase increment with interpolation
        uint32_t baseInc;
        if (lutIndex >= LUT_SIZE - 1) {
            // Beyond LUT range - extrapolate using octave doubling
            baseInc = vOctLUT[LUT_SIZE - 1];
            uint16_t extraEntries = lutIndex - (LUT_SIZE - 1);
            uint8_t extraOctaves = extraEntries / ENTRIES_PER_OCTAVE;
            
            // Limit to prevent overflow
            if (extraOctaves > 15) extraOctaves = 15;
            baseInc <<= extraOctaves;
        } else {
            // Linear interpolation between adjacent LUT entries
            uint32_t inc0 = vOctLUT[lutIndex];
            uint32_t inc1 = vOctLUT[lutIndex + 1];
            
            // Interpolate: inc0 + (inc1 - inc0) * frac
            int32_t delta = (int32_t)(inc1 - inc0);
            baseInc = inc0 + ((delta * (int32_t)lutFrac) >> 16);
        }
        
        // 4. Apply octave shift (branchless)
        if (octaveShift > 0) {
            baseInc <<= (octaveShift > 15 ? 15 : octaveShift);
        } else if (octaveShift < 0) {
            baseInc >>= (-octaveShift > 15 ? 15 : -octaveShift);
        }
        
        // 5. Apply FM if enabled
        if (fmLUT && fmLUTSize > 0) {
            // Calculate bipolar FM amount (-1 to +1 range)
            int32_t fmOffset = (int32_t)fmADC - fmCenterValue;
            
            // Scale to LUT size and get modulation factor
            uint16_t fmIndex = (abs(fmOffset) * fmLUTSize) / fmCenterValue;
            if (fmIndex > fmLUTMask) fmIndex = fmLUTMask;
            
            int32_t modAmount = fmLUT[fmIndex];
            if (fmOffset < 0) modAmount = -modAmount;
            
            // Apply modulation with saturation protection
            int64_t modulated = (int64_t)baseInc + modAmount;
            
            if (modulated < 0) {
                baseInc = 0;
            } else if (modulated > 0xFFFFFFFFLL) {
                baseInc = 0xFFFFFFFF;
            } else {
                baseInc = (uint32_t)modulated;
            }
        }
        
        return baseInc;
    }
    
    // Optimized version without FM
    inline uint32_t __attribute__((always_inline)) 
    getPhaseIncrementNoFM(uint16_t vOctADC, int8_t octaveShift) const {
        // Same as above but without FM calculations
        int32_t clampedADC = vOctADC;
        if (clampedADC < adcOffset) clampedADC = adcOffset;
        if (clampedADC > adcOffset + adcRange) clampedADC = adcOffset + adcRange;
        
        uint32_t scaledADC = (clampedADC - adcOffset) * adcToLutScale;
        uint16_t lutIndex = scaledADC >> 16;
        uint16_t lutFrac = scaledADC & 0xFFFF;
        
        uint32_t baseInc;
        if (lutIndex >= LUT_SIZE - 1) {
            baseInc = vOctLUT[LUT_SIZE - 1];
            uint16_t extraEntries = lutIndex - (LUT_SIZE - 1);
            uint8_t extraOctaves = extraEntries / ENTRIES_PER_OCTAVE;
            if (extraOctaves > 15) extraOctaves = 15;
            baseInc <<= extraOctaves;
        } else {
            uint32_t inc0 = vOctLUT[lutIndex];
            uint32_t inc1 = vOctLUT[lutIndex + 1];
            int32_t delta = (int32_t)(inc1 - inc0);
            baseInc = inc0 + ((delta * (int32_t)lutFrac) >> 16);
        }
        
        if (octaveShift > 0) {
            baseInc <<= (octaveShift > 15 ? 15 : octaveShift);
        } else if (octaveShift < 0) {
            baseInc >>= (-octaveShift > 15 ? 15 : -octaveShift);
        }
        
        return baseInc;
    }
    
    // Debug/calibration helpers
    uint16_t getLUTSize() const { return LUT_SIZE; }
    uint16_t getEntriesPerOctave() const { return ENTRIES_PER_OCTAVE; }
    float getBaseFrequency() const { return baseFrequency; }
};

#endif // FAST_FREQUENCY_CALCULATOR_H