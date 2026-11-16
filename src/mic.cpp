#include "mic.h"
#include <Arduino.h>
#include <driver/i2s.h>

#define I2S_PORT I2S_NUM_0

static bool s_right;

bool mic_init()
{
    i2s_config_t cfg = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = MIC_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = s_right ? I2S_CHANNEL_FMT_ONLY_RIGHT
                                 : I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = MIC_CHUNK,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0,
    };
    if (i2s_driver_install(I2S_PORT, &cfg, 0, NULL) != ESP_OK) {
        return false;
    }
    i2s_pin_config_t pins = {
        .bck_io_num = MIC_PIN_BCLK,
        .ws_io_num = MIC_PIN_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = MIC_PIN_SD,
    };
    if (i2s_set_pin(I2S_PORT, &pins) != ESP_OK) {
        return false;
    }
    i2s_set_clk(I2S_PORT, MIC_SAMPLE_RATE, I2S_BITS_PER_SAMPLE_32BIT,
                I2S_CHANNEL_MONO);
    i2s_zero_dma_buffer(I2S_PORT);
    return true;
}

bool mic_set_channel(int ch)
{
    i2s_driver_uninstall(I2S_PORT);
    s_right = ch == 1;
    return mic_init();
}

size_t mic_read(int32_t *buf, size_t max_samples)
{
    size_t nread = 0;
    esp_err_t err = i2s_read(I2S_PORT, buf, max_samples * sizeof(int32_t),
                             &nread, portMAX_DELAY);
    if (err != ESP_OK) {
        return 0;
    }
    return nread / sizeof(int32_t);
}
