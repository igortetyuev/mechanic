// Хостовый тест DSP: синтетические удары с известным ритмом + шум.
// pio test -e native
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <vector>
#include <unity.h>

#include "../../src/rateest.h"
#include "../../src/tickdsp.h"

#define FS 48000.0

static uint32_t s_rng = 0x9e3779b9u;
static uint32_t xr()
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}
// равномерно [-1, 1)
static float frand() { return (float)(xr() >> 8) * (1.0f / 8388608.0f) - 1.0f; }

// Генерирует сигнал: шумовой пол + удары (3 затухающие синусоиды ~6 кГц
// за ~9 мс — модель щелчков спуска). sday > 0 — «часы спешат».
// be_s — разнобой: чередование ±be_s на удар. miss_pct — пропуски [0..1).
static std::vector<int32_t> synth(double bph, double sday, int n_beats,
                                  double noise, double tick_amp,
                                  double jitter_s, double miss_pct,
                                  double be_s)
{
    double T = 3600.0 / bph * (1.0 - sday / 86400.0);
    size_t total = (size_t)((n_beats + 1) * T * FS) + FS / 10;
    std::vector<int32_t> sig(total);
    for (size_t i = 0; i < total; i++) {
        sig[i] = (int32_t)(noise * frand() * 256.0);
    }
    const double pulse_off[] = {0.0, 0.004, 0.009};
    const double pulse_ph[] = {0.0, 1.9, 4.2}; // разная фаза у под-импульсов
    int pulse_len = (int)(0.004 * FS);          // затухание ~4 мс
    for (int k = 0; k < n_beats; k++) {
        if ((frand() + 1.0f) * 0.5f < miss_pct) { // frand() -> [0,1)
            continue;
        }
        double tb = (k + 0.5) * T + jitter_s * frand() +
                    (k & 1 ? be_s : -be_s);
        for (int p = 0; p < 3; p++) {
            int t0 = (int)((tb + pulse_off[p]) * FS);
            double amp = tick_amp * (p == 1 ? 1.0 : 0.6);
            for (int j = 0; j < pulse_len && t0 + j < (int)total; j++) {
                double e = amp * exp(-(double)j / (0.0007 * FS));
                double v = (double)sig[t0 + j] +
                           e * sin(pulse_ph[p] + j * 2.0 * M_PI * 6000.0 / FS) *
                               256.0;
                if (v > 2.0e9) {
                    v = 2.0e9;
                } else if (v < -2.0e9) {
                    v = -2.0e9;
                }
                sig[t0 + j] = (int32_t)v;
            }
        }
    }
    return sig;
}

// Прогоняет сигнал через TickDsp -> RateEst чанками по 512 сэмплов.
static void run(const std::vector<int32_t> &sig, TickDsp *dsp, RateEst *est)
{
    double beats[8];
    size_t pos = 0;
    while (pos < sig.size()) {
        size_t n = sig.size() - pos;
        if (n > 512) {
            n = 512;
        }
        int nb = tickdsp_feed(dsp, &sig[pos], n, beats, 8);
        for (int i = 0; i < nb; i++) {
            rateest_add_beat(est, beats[i]);
        }
        pos += n;
    }
    double f = tickdsp_flush(dsp);
    if (f > 0) {
        rateest_add_beat(est, f);
    }
}

static double meas_sday(RateEst *est)
{
    return (rateest_bph(est) / est->nominal - 1.0) * 86400.0;
}

static void check_rate(double bph, double sday_in, double miss_pct,
                       double be_s)
{
    TickDsp dsp;
    RateEst est;
    tickdsp_init(&dsp, FS);
    rateest_init(&est, FS);
    rateest_set_nominal(&est, (int)bph);
    auto sig = synth(bph, sday_in, 120, 300.0, 200000.0, 0.00002, miss_pct,
                     be_s);
    run(sig, &dsp, &est);
    double got = meas_sday(&est);
    printf("bph=%.0f in=%+.1f got=%+.2f s/day (beats %u, disc %u)\n", bph,
           sday_in, got, est.n_beats, est.n_discarded);
    TEST_ASSERT_GREATER_THAN_UINT32(80, est.n_beats);
    TEST_ASSERT_FLOAT_WITHIN(3.0, sday_in, got);
}

void test_rate_28800() { check_rate(28800, +5.0, 0.0, 0.0); }
void test_rate_21600() { check_rate(21600, -8.0, 0.0, 0.0); }
void test_rate_18000() { check_rate(18000, +15.0, 0.0, 0.0); }
void test_rate_36000() { check_rate(36000, -3.0, 0.0, 0.0); }
void test_missed_beats() { check_rate(28800, +5.0, 0.1, 0.0); }
// ±1 мс разнобоя: парные интервалы должны его сократить
void test_beat_error() { check_rate(28800, +5.0, 0.0, 0.001); }

void test_autodetect()
{
    TickDsp dsp;
    RateEst est;
    tickdsp_init(&dsp, FS);
    rateest_init(&est, FS); // автодетект
    auto sig = synth(25200, +2.0, 80, 300.0, 200000.0, 0.00002, 0.0, 0.0);
    run(sig, &dsp, &est);
    TEST_ASSERT_TRUE(est.locked);
    TEST_ASSERT_EQUAL_INT(25200, est.nominal);
    TEST_ASSERT_FLOAT_WITHIN(3.0, 2.0, meas_sday(&est));
}

// EMA отчёта: первый вызов = сырой оценке, шаг не прыгает к цели,
// повторные вызовы сходятся к ней.
void test_ema_smooth()
{
    RateEst est;
    rateest_init(&est, FS);
    rateest_set_nominal(&est, 28800);
    double t = 0.0;
    for (int i = 0; i < 40; i++) { // 6000 сэмплов/удар = ровно 28800 пк/ч
        t += 6000.0;
        rateest_add_beat(&est, t);
    }
    double raw = rateest_bph(&est);
    TEST_ASSERT_FLOAT_WITHIN(1.0, raw, rateest_bph_smooth(&est));
    for (int i = 0; i < 300; i++) { // 6060 сэмплов/удар ≈ 28515 пк/ч
        t += 6060.0;
        rateest_add_beat(&est, t);
    }
    double target = rateest_bph(&est);
    double gap = fabs(target - raw);
    double v1 = rateest_bph_smooth(&est); // один шаг EMA
    TEST_ASSERT_TRUE(fabs(v1 - raw) < gap * 0.5);
    double v = v1;
    for (int i = 0; i < 40; i++) {
        v = rateest_bph_smooth(&est);
    }
    TEST_ASSERT_FLOAT_WITHIN(1.0, target, v);
}

void test_noise_only_no_lock()
{
    TickDsp dsp;
    RateEst est;
    tickdsp_init(&dsp, FS);
    rateest_init(&est, FS);
    std::vector<int32_t> sig(FS * 4);
    for (auto &s : sig) {
        s = (int32_t)(300.0 * frand() * 256.0);
    }
    run(sig, &dsp, &est);
    TEST_ASSERT_LESS_THAN_UINT32(5, est.n_beats);
    TEST_ASSERT_FALSE(est.locked);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_rate_28800);
    RUN_TEST(test_rate_21600);
    RUN_TEST(test_rate_18000);
    RUN_TEST(test_rate_36000);
    RUN_TEST(test_missed_beats);
    RUN_TEST(test_beat_error);
    RUN_TEST(test_autodetect);
    RUN_TEST(test_ema_smooth);
    RUN_TEST(test_noise_only_no_lock);
    return UNITY_END();
}
