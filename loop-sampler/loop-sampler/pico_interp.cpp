#include <stdint.h>
#include "pico_interp.h"
#include "hardware/interp.h"

void setupInterpolators() {
    interp_config cfg_0 = interp_default_config();
    interp_config_set_blend(&cfg_0, true);
    interp_set_config(interp0, 0, &cfg_0);
    cfg_0 = interp_default_config();
    interp_set_config(interp0, 1, &cfg_0);
    
    interp_config cfg_1 = interp_default_config();
    interp_config_set_blend(&cfg_1, true);
    interp_set_config(interp1, 0, &cfg_1);
    cfg_1 = interp_default_config();
    interp_set_config(interp1, 1, &cfg_1);
}

uint16_t interpolate(uint16_t x, uint16_t y, uint16_t mu_scaled) {
    // Assume x and y are your two data points and x is the base
    interp0->base[0] = x;
    interp0->base[1] = y;
    // Accumulator setup (mu value)
    uint16_t acc = mu_scaled; // Scaled "mu" value
    interp0->accum[1] = acc;
    // Perform the interpolation
    uint16_t result = interp0->peek[1];
    return result;
}

uint16_t interpolate1(uint16_t x, uint16_t y, uint16_t mu_scaled) {
    // Assume x and y are your two data points and x is the base
    interp1->base[0] = x;
    interp1->base[1] = y;
    // Accumulator setup (mu value)
    uint16_t acc = mu_scaled; // Scaled "mu" value
    interp1->accum[1] = acc;
    // Perform the interpolation
    uint16_t result = interp1->peek[1];
    return result;
}