#include <Arduino.h>
#include <hardware/pwm.h>
#include <hardware/dma.h>
#include <hardware/regs/dreq.h>
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "DACless.h"

// Fixed a/b output buffers
volatile uint16_t pwm_out_buf_a[AUDIO_BLOCK_SIZE] __attribute__((aligned(AUDIO_BLOCK_SIZE * sizeof(uint16_t))));
volatile uint16_t pwm_out_buf_b[AUDIO_BLOCK_SIZE] __attribute__((aligned(AUDIO_BLOCK_SIZE * sizeof(uint16_t))));

static const uint size_bits = 5; // 128 = 8
static_assert((1<<size_bits) == AUDIO_BLOCK_SIZE * sizeof(uint16_t));

volatile uint16_t* out_buf_ptr;
volatile int callback_flag;

int dma_chan_a, dma_chan_b;

float audio_rate = clock_get_hz(clk_sys) / (PWM_RESOLUTION - 1);

// Function definitions
void muteAudioOutput() {
    uint slice_num = pwm_gpio_to_slice_num(PIN_PWM_OUT);
    pwm_set_gpio_level(PIN_PWM_OUT, PWM_RESOLUTION / 2); // Mute by setting to midpoint
    pwm_set_enabled(slice_num, false); // Turn off PWM to ensure it's muted
}

void unmuteAudioOutput() {
    uint slice_num = pwm_gpio_to_slice_num(PIN_PWM_OUT);
    pwm_set_enabled(slice_num, true); // Re-enable PWM
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
        &cfg_0_a,           // The configuration we just created
        &pwm_hw->slice[slice_num].cc, // write address
        pwm_out_buf_a,      // The initial read address
        AUDIO_BLOCK_SIZE,   // Number of transfers
        false               // Start immediately?
    );

    dma_channel_set_irq1_enabled(dma_chan_a, true);

    dma_channel_config cfg_0_b = dma_channel_get_default_config(dma_chan_b);
    channel_config_set_transfer_data_size(&cfg_0_b, DMA_SIZE_16);
    channel_config_set_read_increment(&cfg_0_b, true);
    channel_config_set_dreq(&cfg_0_b, DREQ_PWM_WRAP0 + slice_num); // write data at pwm frequency
    channel_config_set_ring(&cfg_0_b, false, size_bits);
    channel_config_set_chain_to(&cfg_0_b, dma_chan_a); // start chan a when chan b completed
    
    dma_channel_configure(
        dma_chan_b,         // Channel to be configured
        &cfg_0_b,           // The configuration we just created
        &pwm_hw->slice[slice_num].cc, // write address
        pwm_out_buf_b,      // The initial read address
        AUDIO_BLOCK_SIZE,   // Number of transfers
        false               // Start immediately?
    );

    dma_channel_set_irq1_enabled(dma_chan_b, true);
    irq_set_exclusive_handler(DMA_IRQ_1, PWM_DMATransCpltCallback);
    irq_set_enabled(DMA_IRQ_1, true);

    dma_channel_start(dma_chan_a); // seems to start something
}