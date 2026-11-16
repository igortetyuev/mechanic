#include <Arduino.h>
#include "mic.h"
#include "rateest.h"
#include "tickdsp.h"

#define REPORT_MS 2500

static TickDsp dsp;
static RateEst est;
static int32_t buf[MIC_CHUNK];
static double beats[4];
static uint32_t last_report;
static bool dbg;
static char cmd[64];
static size_t cmd_len;

static void print_help()
{
    Serial.println(F("команды:"));
    Serial.println(F("  bph <n>  номинальный ритм, пк/ч (напр. bph 21600)"));
    Serial.println(F("  auto     автодетект ритма"));
    Serial.println(F("  dbg      вкл/выкл отладку детектора"));
    Serial.println(F("  ch       переключить слот I2S левый/правый"));
    Serial.println(F("  raw      дамп сырых сэмплов I2S"));
    Serial.println(F("  cap      захват 0.7 с, огибающая по окнам"));
    Serial.println(F("  rst      сброс статистики"));
}

static void run_cmd()
{
    cmd[cmd_len] = 0;
    cmd_len = 0;
    char *p = cmd;
    while (*p == ' ') {
        p++;
    }
    if (!strncmp(p, "bph", 3)) {
        int v = atoi(p + 3);
        if (v >= 12000 && v <= 40000) {
            rateest_set_nominal(&est, v);
            Serial.printf("номинал задан: %d пк/ч\n", v);
        } else {
            Serial.println("bph: ожидается 12000..40000");
        }
    } else if (!strcmp(p, "cap")) {
        // захват ~0.5 с в статике, вывод max|dv| по окнам 128 сэмплов
        const size_t CAP = 24576;
        static int32_t cap[24576];
        size_t got = 0;
        while (got < CAP) {
            got += mic_read(cap + got, CAP - got);
        }
        Serial.println("--- огибающая |dv|, окно 128 сэмплов ---");
        for (size_t w = 0; w < CAP / 128; w++) {
            int32_t m = 0;
            for (size_t i = 1; i < 128; i++) {
                int32_t d = abs(cap[w * 128 + i] - cap[w * 128 + i - 1]);
                if (d > m) {
                    m = d;
                }
            }
            Serial.printf("%ld%s", (long)(m >> 8),
                          (w % 16 == 15) ? "\n" : " ");
        }
    } else if (!strcmp(p, "raw")) {
        size_t n = mic_read(buf, MIC_CHUNK);
        int32_t mn = INT32_MAX, mx = INT32_MIN;
        int64_t sum = 0;
        uint32_t nz_diff = 0, big_diff = 0;
        for (size_t i = 0; i < n; i++) {
            if (buf[i] < mn) mn = buf[i];
            if (buf[i] > mx) mx = buf[i];
            sum += buf[i];
            if (i && buf[i] != buf[i - 1]) nz_diff++;
            if (i && labs(buf[i] - buf[i - 1]) > (1 << 20)) big_diff++;
        }
        Serial.printf("n=%d min=%ld max=%ld mean=%ld diffs=%lu big=%lu\n",
                      (int)n, (long)mn, (long)mx, (long)(sum / n),
                      (unsigned long)nz_diff, (unsigned long)big_diff);
        for (size_t i = 0; i < n && i < 16; i++) {
            Serial.printf("%4d %12ld\n", (int)i, (long)buf[i]);
        }
    } else if (!strcmp(p, "ch")) {
        static int ch; // 0 = левый, 1 = правый
        ch = !ch;
        Serial.printf("слот I2S: %s\n", mic_set_channel(ch) ? (ch ? "правый" : "левый") : "ошибка");
    } else if (!strcmp(p, "auto")) {
        rateest_set_nominal(&est, 0);
        Serial.println("автодетект ритма");
    } else if (!strcmp(p, "dbg")) {
        dbg = !dbg;
        Serial.printf("отладка %s\n", dbg ? "вкл" : "выкл");
    } else if (!strcmp(p, "rst")) {
        rateest_init(&est, MIC_SAMPLE_RATE);
        Serial.println("статистика сброшена");
    } else if (!strcmp(p, "help") || !strcmp(p, "?")) {
        print_help();
    } else if (*p) {
        Serial.println("неизвестная команда, help — список");
    }
}

static void report()
{
    if (dbg) {
        Serial.printf("пол %.0f | env %.0f | raw %ld | диф %lu | событий %lu | ударов %lu\n",
                      (double)dsp.floor, (double)dsp.env_peak,
                      (long)dsp.raw_peak, (unsigned long)dsp.nz_diff,
                      (unsigned long)dsp.n_events,
                      (unsigned long)dsp.n_beats);
        dsp.raw_peak = 0;
        dsp.env_peak = 0;
        dsp.nz_diff = 0;
    }
    if (!est.locked) {
        double m = rateest_median(&est);
        if (est.count > 5 && m > 0) {
            Serial.printf("подбор ритма… оценка %.0f пк/ч, интервалов %d\n",
                          3600.0 / m, est.count);
        } else {
            Serial.printf("подбор ритма… интервалов %d\n", est.count);
        }
        return;
    }
    if (est.count < 10) {
        Serial.printf("ритм %d пк/ч, накопление данных… %d\n", est.nominal,
                      est.count);
        return;
    }
    double meas = rateest_bph_smooth(&est);
    double pct = (meas / est.nominal - 1.0) * 100.0;
    double sday = pct * 864.0;
    const char *dir;
    if (sday > 2.0) {
        dir = "спешит -> УВЕЛИЧИТЬ период";
    } else if (sday < -2.0) {
        dir = "отстаёт -> УМЕНЬШИТЬ период";
    } else {
        dir = "в норме";
    }
    Serial.printf("ПК/Ч %.0f (%s%d) | %+.2f%% | %+.1f сек/сут | %s | "
                  "разнобой %+.1f мс | ударов %lu, мимо %lu\n",
                  meas, est.auto_mode ? "авто " : "ном ", est.nominal, pct,
                  sday, dir, rateest_beaterr_ms(&est),
                  (unsigned long)est.n_beats,
                  (unsigned long)est.n_discarded);
}

void setup()
{
    Serial.begin(115200);
    delay(300);
    if (!mic_init()) {
        Serial.println("ошибка инициализации I2S");
        while (true) {
            delay(1000);
        }
    }
    tickdsp_init(&dsp, MIC_SAMPLE_RATE);
    rateest_init(&est, MIC_SAMPLE_RATE);
    Serial.println("таймграфер готов: I2S-микрофон 48 кГц, help — команды");
}

void loop()
{
    size_t n = mic_read(buf, MIC_CHUNK);
    if (n) {
        int nb = tickdsp_feed(&dsp, buf, n, beats, 4);
        for (int i = 0; i < nb; i++) {
            int r = rateest_add_beat(&est, beats[i]);
            if (r == 2) {
                Serial.printf("ритм опознан: %d пк/ч\n", est.nominal);
            } else if (dbg && r == 1) {
                Serial.printf("удар: dt=%.1f мс\n",
                              est.buf[(est.head + RATE_BUF - 1) % RATE_BUF] *
                                  1000.0);
            }
        }
    }

    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (cmd_len) {
                run_cmd();
            }
        } else if (cmd_len < sizeof(cmd) - 1) {
            cmd[cmd_len++] = c;
        }
    }

    uint32_t now = millis();
    if (now - last_report >= REPORT_MS) {
        last_report = now;
        report();
    }
}
