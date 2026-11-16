#include "rateest.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const int k_std[] = {14400, 17280, 18000, 19800,
                            21600, 25200, 28800, 36000};

void rateest_init(RateEst *r, double sample_rate)
{
    memset(r, 0, sizeof(*r));
    r->ts = 1.0 / sample_rate;
    r->auto_mode = true;
}

void rateest_set_nominal(RateEst *r, int bph)
{
    if (bph > 0) {
        r->nominal = bph;
        r->t_exp = 3600.0 / bph;
        r->locked = true;
        r->auto_mode = false;
    } else {
        r->nominal = 0;
        r->locked = false;
        r->auto_mode = true;
    }
}

static void push(RateEst *r, double d)
{
    r->buf[r->head] = d;
    r->head = (r->head + 1) % RATE_BUF;
    if (r->count < RATE_BUF) {
        r->count++;
    }
    r->n_beats++;
}

double rateest_median(RateEst *r)
{
    if (!r->count) {
        return 0.0;
    }
    double tmp[RATE_BUF];
    memcpy(tmp, r->buf, sizeof(double) * r->count);
    for (int i = 1; i < r->count; i++) { // insertion sort, <= 256 эл.
        double v = tmp[i];
        int j = i - 1;
        while (j >= 0 && tmp[j] > v) {
            tmp[j + 1] = tmp[j];
            j--;
        }
        tmp[j + 1] = v;
    }
    int m = r->count / 2;
    return (r->count & 1) ? tmp[m] : (tmp[m - 1] + tmp[m]) * 0.5;
}

// Среднее принятых интервалов: выбросы отсеяны при добавлении, а среднее,
// в отличие от медианы, сохраняет суб-сэмпловую точность усреднения
// (метки ударов квантуются по сетке сэмплов).
static double mean_interval(RateEst *r)
{
    if (!r->count) {
        return 0.0;
    }
    double s = 0.0;
    for (int i = 0; i < r->count; i++) {
        s += r->buf[i];
    }
    return s / r->count;
}

double rateest_bph(RateEst *r)
{
    double m = mean_interval(r);
    return m > 0.0 ? 3600.0 / m : 0.0;
}

double rateest_bph_smooth(RateEst *r)
{
    double m = rateest_bph(r);
    if (!r->ema_bph_init || m <= 0.0) {
        r->ema_bph = m;
        r->ema_bph_init = true;
        return m;
    }
    r->ema_bph += (m - r->ema_bph) * RATE_EMA_ALPHA;
    return r->ema_bph;
}

// Ближайший типовой ритм, если медиана в пределах RATE_SNAP_TOL, иначе 0.
static int snap(double median_s)
{
    double bph = 3600.0 / median_s;
    int best = 0;
    double best_rel = RATE_SNAP_TOL;
    for (size_t i = 0; i < sizeof(k_std) / sizeof(k_std[0]); i++) {
        double rel = fabs(bph - k_std[i]) / k_std[i];
        if (rel < best_rel) {
            best_rel = rel;
            best = k_std[i];
        }
    }
    return best;
}

double rateest_beaterr_ms(RateEst *r)
{
    if (!r->ema_init[0] || !r->ema_init[1]) {
        return 0.0;
    }
    return (r->ema[0] - r->ema[1]) * 500.0;
}

int rateest_add_beat(RateEst *r, double t_samp)
{
    double t = t_samp * r->ts;
    if (r->n_mark < 2) {
        r->last_t[r->n_mark++] = t;
        return -1;
    }
    // полный период = два полуколебания: асимметрия тик/так сокращается
    double d2 = t - r->last_t[0];
    double d1 = t - r->last_t[1];
    r->last_t[0] = r->last_t[1];
    r->last_t[1] = t;
    r->beat_cnt++;

    // накопление разнобоя по одиночным правдоподобным интервалам
    if (r->locked && d1 > 0.5 * r->t_exp && d1 < 1.5 * r->t_exp) {
        int p = (int)(r->beat_cnt & 1);
        if (!r->ema_init[p]) {
            r->ema[p] = d1;
            r->ema_init[p] = true;
        } else {
            r->ema[p] += (d1 - r->ema[p]) * 0.1;
        }
    }

    double d = d2 * 0.5; // эффективный межударный интервал
    if (d <= 0.0 || d > RATE_D_MAX * 3.0) {
        r->n_discarded++;
        return 0; // мусор: совпадение или длинная пауза
    }

    if (r->locked) {
        // валиден полный период ≈ 2×ожидаемого; пропуск удара даёт
        // 0.75× или 1.5× — за пределами допуска, отбрасываем
        if (fabs(d - r->t_exp) > RATE_VALID_TOL * r->t_exp) {
            r->n_discarded++;
            return 0;
        }
        push(r, d);
        return 1;
    }

    // автодетект: копим правдоподобные интервалы
    if (d < RATE_D_MIN || d > RATE_D_MAX) {
        r->n_discarded++;
        return 0;
    }
    push(r, d);
    if (r->count >= RATE_LOCK_N) {
        int s = snap(rateest_median(r));
        if (s) {
            r->nominal = s;
            r->t_exp = 3600.0 / s;
            r->locked = true;
            r->auto_mode = true;
            return 2;
        }
    }
    return 1;
}
