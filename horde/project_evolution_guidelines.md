# Synth Foundry Project Evolution Guidelines

This document outlines how to evolve the minimal audio synthesis template into a full-featured system while maintaining Synth Foundry design principles.

## Current Minimal State

The project currently contains only the absolute essentials:
- **Display setup**: SSD1306 OLED initialization and basic text output
- **ADC reading**: 4-channel ADC with raw 12-bit value display
- **White noise output**: PWM/DMA system generating noise on GP6

## Core Design Principles (Always Maintain)

### Memory Management
- **No dynamic allocation** in audio paths (`new`, `malloc`, `String`, `STL`)
- **Static/global storage** only for audio processing
- **Initialize all variables** to prevent undefined behavior

### Real-Time Requirements
- **No blocking I/O** in audio/DMA interrupt handlers
- **No floating-point** in hot loops (audio processing paths)
- **Integer arithmetic** and lookup tables for expensive calculations
- **Fixed-size blocks** with DMA ping-pong buffering

### Fixed-Point Math Standards
- **Audio signals**: Q1.15 format (`int16_t`, -32768..+32767 ≈ -1.0..+1.0)
- **Oscillator phase**: Q0.32 format (`uint32_t`) for phase accumulators
- **Sample playback**: Q24.8 signed (`int32_t`) for sample phase
- **Mix bus**: 32-bit accumulator with appropriate headroom

### Code Organization
- **`#pragma once`** instead of include guards
- **`snake_case`** file names
- **Centralized hardware** configuration in `config_pins.h`
- **Orchestrate from main** `.ino` file
- **`@author Brian Varren`** in all headers
- **Project name**: "Synth Foundry"

## Evolution Path

### Phase 1: Minimal Core (Current State)
```
Files:
├── config_pins.h          # Hardware pin definitions
├── fixed_point_utils.h    # Q1.15 math functions
├── ADCless.h/cpp          # ADC reading (keep intact)
├── DACless.h/cpp          # PWM audio output (keep intact)
├── adc_filter.cpp         # ADC filtering (keep intact)
└── main.ino              # Minimal main loop

Features:
- Display ADC values as raw 12-bit numbers
- Generate white noise via PWM/DMA
- Basic dual-core architecture
```

### Phase 2: Audio Processing Integration
```
Add:
├── audio_engine.h/cpp     # Audio synthesis engine
├── shared_state.h/cpp     # Simple inter-core communication

Features:
- Connect ADC values to audio parameters
- Apply filtering to ADC inputs
- Generate filtered white noise
- Real-time parameter updates
```

### Phase 3: User Interface
```
Add:
├── ui_display.h/cpp       # Display system
├── ui_input.h/cpp         # Button input handling

Features:
- Parameter display with progress bars
- Button navigation for parameter selection
- Real-time value updates
- System status indicators
```

### Phase 4: Advanced Features
```
Add:
├── sf_globals_bridge.h/cpp # Complex inter-core communication
├── performance_monitoring  # CPU load and statistics
├── menu_system            # Hierarchical navigation
├── preset_management      # Parameter saving/loading

Features:
- Complex menu systems
- Performance monitoring
- Preset management
- Advanced parameter control
```

## Implementation Guidelines

### When Adding New Features

1. **Start Simple**: Begin with the minimal implementation
2. **Add Gradually**: Introduce complexity incrementally
3. **Test Each Phase**: Ensure compilation and basic functionality
4. **Maintain Principles**: Always follow core design principles
5. **Document Changes**: Update this guide with new patterns

### Audio Processing Pipeline Evolution

#### Phase 1 (Current)
```
ADC → Raw Display
PWM → White Noise
```

#### Phase 2
```
ADC → Filter → Audio Engine → PWM → Filtered Noise
```

#### Phase 3
```
ADC → Filter → Audio Engine → PWM → Filtered Noise
     ↓
   UI Display ← Shared State
```

#### Phase 4
```
ADC → Filter → Audio Engine → PWM → Complex Synthesis
     ↓              ↓
   UI Display ← Shared State ← Performance Monitor
     ↓
   Menu System → Preset Manager
```

### Memory Allocation Rules

#### Always Allowed
```cpp
// Static/global variables
static int16_t g_audio_buffer[BUFFER_SIZE];
volatile uint32_t g_shared_state;

// Stack allocation in non-audio functions
void setup() {
    int temp_var = 0;  // OK - not in audio path
}
```

#### Never Allowed in Audio Paths
```cpp
// These are forbidden in audio processing functions
new int16_t[BUFFER_SIZE];        // ❌ Dynamic allocation
malloc(sizeof(int16_t) * size);  // ❌ Dynamic allocation
String("Hello");                 // ❌ Dynamic strings
std::vector<int> vec;            // ❌ STL containers
```

### Fixed-Point Math Examples

#### Audio Signal Processing
```cpp
// Q1.15 multiplication with saturation
int16_t result = mul_q15(input_sample, gain_level);

// Q1.15 addition with saturation
int16_t mixed = add_q15(left_channel, right_channel);

// Convert to PWM
uint16_t pwm_value = q15_to_pwm(audio_sample);
```

#### Oscillator Phase Accumulation
```cpp
// Q0.32 phase increment
uint32_t inc = hz_to_inc_q32(frequency_hz, sample_rate);

// Update phase
phase_accumulator += inc;

// Extract table index
uint32_t index = phase_to_index(phase_accumulator, table_bits);
```

### Inter-Core Communication Evolution

#### Phase 1: Direct Access
```cpp
// Simple volatile variables
volatile float g_noise_level = 0.5f;
volatile bool g_display_update = true;
```

#### Phase 2: Structured State
```cpp
// Structured shared state
typedef struct {
    float noise_level;
    float filter_cutoff;
    bool display_needs_update;
} shared_state_t;

volatile shared_state_t g_shared_state;
```

#### Phase 3: Protected Access
```cpp
// Simple access functions
void update_audio_params(float level, float cutoff);
bool get_audio_params(audio_params_t* params);
```

#### Phase 4: Advanced Synchronization
```cpp
// Sequence locks for complex data
sequence_lock_t g_params_lock;
uint32_t seq = sequence_lock_write_begin(&g_params_lock);
// ... update data ...
sequence_lock_write_end(&g_params_lock, seq);
```

## Testing Strategy

### Phase Testing
1. **Compilation Test**: Ensure all phases compile successfully
2. **Basic Functionality**: Verify core features work
3. **Performance Test**: Check CPU usage and audio quality
4. **Integration Test**: Verify inter-core communication
5. **Stress Test**: Run for extended periods

### Regression Prevention
- **Keep working phases**: Don't break previous functionality
- **Incremental commits**: Commit after each phase completion
- **Documentation updates**: Keep this guide current
- **Example preservation**: Keep minimal examples working

## Common Pitfalls to Avoid

### Memory Issues
- **Stack overflow**: Large arrays on stack
- **Heap fragmentation**: Dynamic allocation in audio paths
- **Cache misses**: Non-sequential memory access patterns

### Real-Time Issues
- **Blocking operations**: Serial.print in audio interrupts
- **Floating-point**: Expensive FP operations in hot loops
- **Variable timing**: Non-deterministic audio processing

### Design Issues
- **Premature optimization**: Adding complexity before needed
- **Over-engineering**: Complex solutions to simple problems
- **Inconsistent patterns**: Mixing different approaches

## Recovery Strategies

### If Project Becomes Too Complex
1. **Revert to minimal state**: Go back to Phase 1
2. **Identify core issues**: What made it complex?
3. **Simplify incrementally**: Add features one at a time
4. **Document lessons learned**: Update this guide

### If Performance Degrades
1. **Profile the system**: Identify bottlenecks
2. **Optimize hot paths**: Focus on audio processing
3. **Reduce complexity**: Remove unnecessary features
4. **Test incrementally**: Verify improvements

## Future Extensions

### Potential Additions
- **MIDI input**: External control interface
- **Wavetable synthesis**: More complex audio generation
- **Effects processing**: Reverb, delay, distortion
- **Multi-voice polyphony**: Multiple simultaneous voices
- **Preset management**: Save/load parameter sets
- **Network control**: Remote parameter adjustment

### Extension Guidelines
- **Maintain core principles**: Never compromise on fundamentals
- **Add incrementally**: One feature at a time
- **Test thoroughly**: Verify each addition works
- **Document patterns**: Update this guide with new approaches

---

*This document should be updated as the project evolves to maintain a clear path from minimal implementation to full-featured system.*
