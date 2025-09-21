#pragma once

#define AUDIO_BLOCK_SIZE    16
#define PIN_PWM_OUT         6
#define PWM_RESOLUTION      4096

// DECLARATIONS only (using extern keyword)
extern volatile uint16_t pwm_out_buf_a[AUDIO_BLOCK_SIZE];
extern volatile uint16_t pwm_out_buf_b[AUDIO_BLOCK_SIZE];
extern volatile uint16_t* out_buf_ptr;
extern volatile int callback_flag;
extern uint32_t dma_chan_a, dma_chan_b;
extern float audio_rate;

// Function declarations only
void muteAudioOutput();
void unmuteAudioOutput();
void PWM_DMATransCpltCallback();
void configurePWM_DMA();