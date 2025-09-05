#include <Arduino.h>
#include <hardware/adc.h>
#include <hardware/dma.h>
#include <hardware/regs/dreq.h>
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "ADCless.h"

// DEFINITIONS of the variables
volatile uint16_t adc_results_buf[NUM_ADC_INPUTS] __attribute__((aligned(NUM_ADC_INPUTS * sizeof(uint16_t))));
volatile uint16_t* adc_results_ptr[1] = {adc_results_buf};
int adc_samp_chan, adc_ctrl_chan;

// Function definition
void configureADC_DMA(){
    uint8_t mask = (1u << NUM_ADC_INPUTS) - 1;

    #ifdef ADCLESS_RP2350B
        Serial.println("RP2350B is defined.");
        Serial.println(mask, HEX);
    #else
        Serial.println("RP2350B is not defined.");
    #endif

    // Setup ADC.
    for (int i = 0; i < NUM_ADC_INPUTS; i++){
        adc_gpio_init(BASE_ADC_PIN + i);
    }

    adc_init();
    adc_set_clkdiv(1);
    adc_set_round_robin((1 << NUM_ADC_INPUTS) - 1);
    adc_select_input(0);
    adc_fifo_setup(true, true, 4, false, false);
    adc_fifo_drain();

    int samp_chan = dma_claim_unused_channel(true);
    int ctrl_chan = dma_claim_unused_channel(true);
    dma_channel_config samp_conf = dma_channel_get_default_config(samp_chan);
    dma_channel_config ctrl_conf = dma_channel_get_default_config(ctrl_chan);

    channel_config_set_transfer_data_size(&samp_conf, DMA_SIZE_16);
    channel_config_set_read_increment(&samp_conf, false);
    channel_config_set_write_increment(&samp_conf, true);
    channel_config_set_irq_quiet(&samp_conf, true);
    channel_config_set_dreq(&samp_conf, DREQ_ADC);
    channel_config_set_chain_to(&samp_conf, ctrl_chan);
    channel_config_set_enable(&samp_conf, true);

    dma_channel_configure(
        samp_chan,
        &samp_conf,
        nullptr,
        &adc_hw->fifo,
        NUM_ADC_INPUTS,
        false
    );

    channel_config_set_transfer_data_size(&ctrl_conf, DMA_SIZE_32);
    channel_config_set_read_increment(&ctrl_conf, false);
    channel_config_set_write_increment(&ctrl_conf, false);
    channel_config_set_irq_quiet(&ctrl_conf, true);
    channel_config_set_dreq(&ctrl_conf, DREQ_FORCE);
    channel_config_set_enable(&ctrl_conf, true);

    dma_channel_configure(
        ctrl_chan,
        &ctrl_conf,
        &dma_hw->ch[samp_chan].al2_write_addr_trig,
        adc_results_ptr,
        1,
        false
    );
    dma_channel_start(ctrl_chan);
    adc_run(true);
}