#ifndef DACLESS_H
#define DACLESS_H

#include <Arduino.h>
#include <hardware/adc.h>
#include <hardware/pwm.h>
#include <hardware/dma.h>
#include "hardware/interp.h"
#include <hardware/regs/dreq.h>
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"

#define AUDIO_BLOCK_SIZE    16
#define PIN_PWM_OUT         6
#define PWM_RESOLUTION      4096
#define NUM_ADC_INPUTS      4

volatile uint16_t adc_results_buf[NUM_ADC_INPUTS] __attribute__((aligned(NUM_ADC_INPUTS * sizeof(uint16_t))));;
volatile uint16_t pwm_out_buf_a[AUDIO_BLOCK_SIZE] __attribute__((aligned(AUDIO_BLOCK_SIZE * sizeof(uint16_t))));
volatile uint16_t pwm_out_buf_b[AUDIO_BLOCK_SIZE] __attribute__((aligned(AUDIO_BLOCK_SIZE * sizeof(uint16_t))));

static const uint size_bits = 5; // 128 = 8
static_assert((1<<size_bits) == AUDIO_BLOCK_SIZE * sizeof(uint16_t));

static volatile uint16_t* adc_results_ptr[1] = {adc_results_buf};
static volatile uint16_t* out_buf_ptr;

volatile extern int callback_flag;

uint    dma_chan_a,
        dma_chan_b,
        adc_samp_chan,
        adc_ctrl_chan;

float audio_rate = clock_get_hz(clk_sys) / (PWM_RESOLUTION - 1);

class MovingAverageFilter {
private:
  uint16_t *buffer;
  uint32_t index = 0;
  uint32_t sum = 0;
  uint8_t filterSize;

public:
  MovingAverageFilter(uint8_t size) : filterSize(size) {
    buffer = new uint16_t[filterSize];
    for (uint32_t i = 0; i < filterSize; ++i) {
      buffer[i] = 0;
    }
  }

  uint16_t process(uint16_t input) {
    sum -= buffer[index];
    buffer[index] = input;
    sum += input;
    index = (index + 1) % filterSize;

    return static_cast<uint16_t>(sum / filterSize);
  }
};

void muteAudioOutput() {
    uint slice_num = pwm_gpio_to_slice_num(PIN_PWM_OUT);
    pwm_set_gpio_level(PIN_PWM_OUT, PWM_RESOLUTION / 2); // Mute by setting to midpoint
    pwm_set_enabled(slice_num, false); // Turn off PWM to ensure it's muted
}

void unmuteAudioOutput() {
    uint slice_num = pwm_gpio_to_slice_num(PIN_PWM_OUT);
    pwm_set_enabled(slice_num, true); // Re-enable PWM
}

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

inline uint16_t interpolate(uint16_t x, uint16_t y, uint16_t mu_scaled) {
    // Assume x and y are your two data points and x is the base
    interp0->base[0] = x;
    interp0->base[1] = y;

    // Accumulator setup (mu value)
    uint16_t acc = mu_scaled;  // Scaled "mu" value
    interp0->accum[1] = acc;

    // Perform the interpolation
    uint16_t result = interp0->peek[1];

    return result;
}

inline uint16_t interpolate1(uint16_t x, uint16_t y, uint16_t mu_scaled) {
    // Assume x and y are your two data points and x is the base
    interp1->base[0] = x;
    interp1->base[1] = y;

    // Accumulator setup (mu value)
    uint16_t acc = mu_scaled;  // Scaled "mu" value
    interp1->accum[1] = acc;

    // Perform the interpolation
    uint16_t result = interp1->peek[1];

    return result;
}

void PWM_DMATransCpltCallback(){
    uint32_t pending = dma_hw->ints1;  // Get all pending interrupts on IRQ 1

    // Handle the interrupt for channel A
    if (pending & (1u << dma_chan_a)) {
        dma_hw->ints1 = 1u << dma_chan_a; // clear the interrupt request
        out_buf_ptr   = &pwm_out_buf_a[0];
        callback_flag = 1;
    }

    // Handle the interrupt for channel B
    if (pending & (1u << dma_chan_b)) {
        dma_hw->ints1 = 1u << dma_chan_b; // clear the interrupt request
        out_buf_ptr   = &pwm_out_buf_b[0];
        callback_flag = 1;
    }
}

void configurePWM_DMA(){

    gpio_set_function(PIN_PWM_OUT, GPIO_FUNC_PWM);// set GP2 function PWM
    uint slice_num = pwm_gpio_to_slice_num(PIN_PWM_OUT);// GP2 PWM slice
    pwm_set_clkdiv(slice_num, 1);
    pwm_set_wrap(slice_num, PWM_RESOLUTION);
    pwm_set_enabled(slice_num, true);
    pwm_set_irq_enabled(slice_num, true); // Necessary? Yes

	dma_chan_a = dma_claim_unused_channel(true);
	dma_chan_b = dma_claim_unused_channel(true);

	dma_channel_config cfg_0_a = dma_channel_get_default_config(dma_chan_a);
	channel_config_set_transfer_data_size(&cfg_0_a, DMA_SIZE_16);
	channel_config_set_read_increment(&cfg_0_a, true);
	channel_config_set_dreq(&cfg_0_a, DREQ_PWM_WRAP0 + slice_num); // write data at pwm frequency
    channel_config_set_ring(&cfg_0_a, false, size_bits); //'false' refers to whether data is being written to the buffer
	channel_config_set_chain_to(&cfg_0_a, dma_chan_b); // start chan b when chan a completed

	dma_channel_configure(
		dma_chan_a,         // Channel to be configured
		&cfg_0_a,             // The configuration we just created
		&pwm_hw->slice[slice_num].cc, // write address
		pwm_out_buf_a,              // The initial read address
		AUDIO_BLOCK_SIZE,   // Number of transfers
		false               // Start immediately?
	);


    dma_channel_set_irq1_enabled(dma_chan_a, true);
    //DMAChanATransCpltCallback();

	dma_channel_config cfg_0_b = dma_channel_get_default_config(dma_chan_b);
    channel_config_set_transfer_data_size(&cfg_0_b, DMA_SIZE_16);
    channel_config_set_read_increment(&cfg_0_b, true);
    channel_config_set_dreq(&cfg_0_b, DREQ_PWM_WRAP0 + slice_num); // write data at pwm frequency
    channel_config_set_ring(&cfg_0_b, false, size_bits);
	channel_config_set_chain_to(&cfg_0_b, dma_chan_a); // start chan a when chan b completed
	dma_channel_configure(
        dma_chan_b,         // Channel to be configured
        &cfg_0_b,             // The configuration we just created
        &pwm_hw->slice[slice_num].cc, // write address
        pwm_out_buf_b,              // The initial read address
        AUDIO_BLOCK_SIZE,   // Number of transfers
        false               // Start immediately?
    );

    dma_channel_set_irq1_enabled(dma_chan_b, true);
    irq_set_exclusive_handler(DMA_IRQ_1, PWM_DMATransCpltCallback);
    irq_set_enabled(DMA_IRQ_1,true);
    //DMAChanBTransCpltCallback();

	dma_channel_start(dma_chan_a); // seems to start something
}

void configureADC_DMA(){

    // Setup ADC.
    adc_gpio_init(26);
    adc_gpio_init(27);
    adc_gpio_init(28);
    adc_gpio_init(29);
    adc_init();
    adc_set_clkdiv(1); // Run at max speed.
    adc_set_round_robin(0xF); // Enable round-robin sampling of all 5 inputs.
    adc_select_input(0); // Set starting ADC channel for round-robin mode.
    adc_fifo_setup(
        true,    // Write each completed conversion to the sample FIFO
        true,    // Enable DMA data request (DREQ)
        4,       // Assert DREQ (and IRQ) at least 1 sample present
        false,   // Omit ERR bit (bit 15) since we have 8 bit reads.
        false     // shift each sample to 8 bits when pushing to FIFO
    );
    adc_fifo_drain();

    // Get two open DMA channels.
    // samp_chan will sample the adc, paced by DREQ_ADC and chain to ctrl_chan.
    // ctrl_chan will reconfigure & retrigger samp_chan when samp_chan finishes.
    int samp_chan = dma_claim_unused_channel(true);
    int ctrl_chan = dma_claim_unused_channel(true);
    dma_channel_config samp_conf = dma_channel_get_default_config(samp_chan);
    dma_channel_config ctrl_conf = dma_channel_get_default_config(ctrl_chan);

    // Setup Sample Channel.
    channel_config_set_transfer_data_size(&samp_conf, DMA_SIZE_16);
    channel_config_set_read_increment(&samp_conf, false); // read from adc FIFO reg.
    channel_config_set_write_increment(&samp_conf, true);
    channel_config_set_irq_quiet(&samp_conf, true);
    channel_config_set_dreq(&samp_conf, DREQ_ADC); // pace data according to ADC
    channel_config_set_chain_to(&samp_conf, ctrl_chan);
    channel_config_set_enable(&samp_conf, true);
    // Apply samp_chan configuration.
    dma_channel_configure(
        samp_chan,          // Channel to be configured
        &samp_conf,
        nullptr,            // write (dst) address will be loaded by ctrl_chan.
        &adc_hw->fifo,      // read (source) address. Does not change.
        NUM_ADC_INPUTS, // Number of word transfers.
        false               // Don't Start immediately.
    );

    // Setup Reconfiguration Channel
    // This channel will Write the starting address to the write address
    // "trigger" register, which will restart the DMA Sample Channel.
    channel_config_set_transfer_data_size(&ctrl_conf, DMA_SIZE_32);
    channel_config_set_read_increment(&ctrl_conf, false); // read a single uint32.
    channel_config_set_write_increment(&ctrl_conf, false);
    channel_config_set_irq_quiet(&ctrl_conf, true);
    channel_config_set_dreq(&ctrl_conf, DREQ_FORCE); // Go as fast as possible.
    channel_config_set_enable(&ctrl_conf, true);
    // Apply reconfig channel configuration.
    dma_channel_configure(
        ctrl_chan,  // Channel to be configured
        &ctrl_conf,
        &dma_hw->ch[samp_chan].al2_write_addr_trig, // dst address. Writing here retriggers samp_chan.
        adc_results_ptr,   // Read (src) address is a single array with the starting address.
        1,          // Number of word transfers.
        false       // Don't Start immediately.
    );
    dma_channel_start(ctrl_chan);
    adc_run(true); // Kick off the ADC in free-running mode.

}


#endif // DACLESS_H