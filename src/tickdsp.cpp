#include "tickdsp.h"
#include <string.h>

// Спад огибающей ~3 мс при 48 кГц
#define ENV_DECAY 0.993f
// Сглаживание огибающей для триггера ~1 мс: устойчивый всплеск тика
// проходит, по-сэмпловые шумовые выбросы давятся
#define EDET_A 0.02f
// Отпускание события: огибающая чуть выше пола
#define RELEASE_K 1.5f
// Максимальная длительность события: длиннее — это не тик, а шум,
// принудительно отпускаем, чтобы пол мог адаптироваться
#define MAX_EVENT_MS 40
// Трекер шумового пола: быстрый подъём, медленный спад
#define FLOOR_UP 0.005f
#define FLOOR_DOWN 0.0002f

void tickdsp_init(TickDsp *d, double sample_rate)
{
    memset(d, 0, sizeof(*d));
    d->sample_rate = sample_rate;
    d->thr_ratio = 3.0f;
    d->min_env = 1500.0f;
    d->refractory = (uint32_t)(0.002 * sample_rate);  // 2 мс
    d->cluster_gap = (uint32_t)(0.020 * sample_rate); // 20 мс
    d->floor = d->min_env;
}

static int emit(TickDsp *d, double *beats, int nb, int max_beats)
{
    if (nb < max_beats) {
        // метка удара — энергетический центроид всплеска
        beats[nb++] =
            d->cw_den > 0.0 ? d->cw_num / d->cw_den : d->cluster_t;
    }
    d->cluster_open = false;
    d->n_beats++;
    return nb;
}

int tickdsp_feed(TickDsp *d, const int32_t *raw, size_t n, double *beats,
                 int max_beats)
{
    int nb = 0;
    for (size_t i = 0; i < n; i++) {
        uint64_t n_cur = d->n_abs + i;

        // тишина дольше cluster_gap закрывает удар
        if (d->cluster_open && n_cur - d->cluster_last > d->cluster_gap) {
            nb = emit(d, beats, nb, max_beats);
        }

        // дифференциатор: подчёркивает щелчки, убирает DC и НЧ-гул
        int32_t rv = raw[i];
        if (rv < 0) {
            rv = -rv;
        }
        if (rv > d->raw_peak) {
            d->raw_peak = rv;
        }
        int32_t v = raw[i] >> 8;
        float y = (float)(v - d->v_prev);
        d->v_prev = v;
        if (y < 0.0f) {
            y = -y;
        }
        if (y > 0.0f) {
            d->nz_diff++;
        }
        d->env = (y > d->env) ? y : d->env * ENV_DECAY;
        if (d->env > d->env_peak) {
            d->env_peak = d->env;
        }
        d->e_det += (y - d->e_det) * EDET_A;

        float thr = d->floor * d->thr_ratio;
        if (thr < d->min_env) {
            thr = d->min_env;
        }

        if (!d->in_event && d->e_det > thr &&
            n_cur - d->last_event_n >= d->refractory) {
            d->in_event = true;
            d->last_event_n = n_cur;
            d->n_events++;
            if (d->cluster_open) {
                d->cluster_last = n_cur;
            } else {
                d->cluster_open = true;
                d->cluster_t = (double)n_cur;
                d->cluster_last = n_cur;
                d->cluster_peak = 0.0f;
                d->cw_num = 0.0;
                d->cw_den = 0.0;
            }
            if (d->env > d->cluster_peak) {
                d->cluster_peak = d->env;
            }
        } else if (d->in_event &&
                   (d->e_det < d->floor * RELEASE_K ||
                    n_cur - d->last_event_n >
                        (uint64_t)(MAX_EVENT_MS * d->sample_rate / 1000))) {
            d->in_event = false;
        }

        // накопление центроида всплеска выше уровня шумового пола
        if (d->cluster_open && d->env > d->floor * 2.0f) {
            d->cw_num += (double)n_cur * d->env;
            d->cw_den += (double)d->env;
        }

        // пол адаптируется только в паузах между ударами
        if (!d->cluster_open) {
            float a = d->e_det > d->floor ? FLOOR_UP : FLOOR_DOWN;
            d->floor += (d->e_det - d->floor) * a;
        }

        d->prev_env = d->env;
    }
    d->n_abs += n;
    return nb;
}

double tickdsp_flush(TickDsp *d)
{
    if (!d->cluster_open) {
        return -1.0;
    }
    d->cluster_open = false;
    d->n_beats++;
    return d->cw_den > 0.0 ? d->cw_num / d->cw_den : d->cluster_t;
}
