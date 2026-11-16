#pragma once
#include <stddef.h>
#include <stdint.h>

// Детектор ударов спускового механизма.
// На вход — сырые 32-битные слоты I2S INMP441 (24 бита в старших),
// на выход — суб-сэмпловые метки ударов в номерах сэмплов.
struct TickDsp {
    // настройки
    double sample_rate;
    float thr_ratio;     // порог = floor * thr_ratio
    float min_env;       // минимальный абсолютный порог огибающей
    uint32_t refractory; // мин. пауза между событиями, сэмплы
    uint32_t cluster_gap; // тишина, закрывающая удар, сэмплы
    // состояние фильтра
    int32_t v_prev;
    float env;
    float prev_env;
    float e_det;     // сглаженная огибающая для триггера (τ≈1 мс)
    float floor;
    bool in_event;
    uint64_t last_event_n;
    // кластер (удар)
    bool cluster_open;
    double cluster_t;      // метка первого события кластера, сэмплы
    double cw_num;         // Σ t·env для центроида
    double cw_den;         // Σ env
    uint64_t cluster_last; // сэмпл последнего события кластера
    float cluster_peak;
    // статистика
    uint64_t n_abs;    // абсолютный счётчик сэмплов
    uint32_t n_events; // всего событий
    uint32_t n_beats;  // всего закрытых ударов
    int32_t raw_peak;  // пик |raw| с момента сброса (диагностика)
    float env_peak;    // пик огибающей с момента сброса
    uint32_t nz_diff;  // сэмплы с |v-v_prev|>0 за период
};

void tickdsp_init(TickDsp *d, double sample_rate);

// Скармливает чанк сырых сэмплов. Записывает метки закрывшихся ударов
// (в сэмплах, дробные) в beats, возвращает их число (<= max_beats).
int tickdsp_feed(TickDsp *d, const int32_t *raw, size_t n, double *beats,
                 int max_beats);

// Принудительно закрыть открытый кластер в конце потока. -1 если нечего.
double tickdsp_flush(TickDsp *d);
