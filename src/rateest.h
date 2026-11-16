#pragma once
#include <stdint.h>
#include <stdbool.h>

// Статистика межударных интервалов и оценка ритма механизма.
#define RATE_BUF 256
// Шаг EMA сглаженного ритма за вызов rateest_bph_smooth (раз в отчёт)
#define RATE_EMA_ALPHA 0.20
// Правдоподобные интервалы при автодетекте, с (≈12800–40000 пк/ч)
#define RATE_D_MIN 0.090
#define RATE_D_MAX 0.280
// Сколько интервалов нужно для автодетекта
#define RATE_LOCK_N 30
// Допуск при щелчке на типовой ритм и при валидации интервала
#define RATE_SNAP_TOL 0.06
#define RATE_VALID_TOL 0.20

struct RateEst {
    double ts;         // период сэмплирования, с
    double last_t[2];  // метки двух последних ударов, с
    int n_mark;        // сколько меток накоплено (0..2)
    uint64_t beat_cnt; // счётчик ударов (паритет для разнобоя)
    double buf[RATE_BUF]; // кольцевой буфер принятых полуинтервалов, с
    int head;
    int count;
    uint32_t n_beats;     // принятые интервалы
    uint32_t n_discarded; // отброшенные
    int nominal;          // номинальный ритм, пк/ч (0 = не задан)
    bool locked;          // ритм определён (авто или вручную)
    bool auto_mode;       // номинал подобран автоматически
    double t_exp;         // ожидаемый межударный интервал, с
    // разнобой: EMA одиночных интервалов по паритету удара
    double ema[2];
    bool ema_init[2];
    // EMA отображаемого ритма, пк/ч
    double ema_bph;
    bool ema_bph_init;
};

void rateest_init(RateEst *r, double sample_rate);

// Задать номинал вручную; bph <= 0 — вернуться в автодетект.
void rateest_set_nominal(RateEst *r, int bph);

// Скармливает метку удара в сэмплах.
// Возврат: -1 прогрев (первые два удара), 0 интервал отброшен,
// 1 принят, 2 принят и ритм только что опознан автоматически.
int rateest_add_beat(RateEst *r, double t_samp);

// Разнобой (beat error), мс: полуразность чередующихся интервалов.
// 0 пока данных мало.
double rateest_beaterr_ms(RateEst *r);

// Медиана принятых интервалов, с (0 если данных нет).
double rateest_median(RateEst *r);

// Измеренный ритм, пк/ч (0 если данных нет).
double rateest_bph(RateEst *r);

// Сглаженный (EMA) измеренный ритм для отчёта. Каждый вызов — один шаг
// сглаживания: вызывать раз в период отчёта. Первый вызов = сырой оценке.
double rateest_bph_smooth(RateEst *r);
