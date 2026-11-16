#pragma once
#include <stddef.h>
#include <stdint.h>

// I2S-микрофон (INMP441 или SPH0645): SCK=GPIO26, WS=GPIO25, SD=GPIO32.
// INMP441 L/R->GND — левый слот, SPH0645 — данные в правом (команда ch).
#define MIC_PIN_BCLK 26
#define MIC_PIN_WS 25
#define MIC_PIN_SD 32

#define MIC_SAMPLE_RATE 48000
#define MIC_CHUNK 512 // сэмплов за чтение (~10.7 мс)

// Запускает I2S-приёмник. Возвращает false при ошибке драйвера.
bool mic_init();

// Переустанавливает приёмник на другой слот: ch=0 левый, ch=1 правый
// (INMP441 L/R->GND = левый, L/R->VDD = правый).
bool mic_set_channel(int ch);

// Блокирующее чтение чанка сырых 32-битных слотов I2S.
// Возвращает число прочитанных сэмплов (0 при таймауте/ошибке).
size_t mic_read(int32_t *buf, size_t max_samples);
