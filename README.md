# mechanic — таймграфер на ESP32 / ESP32 timegrapher

[Русский](#русский) | [English](#english)

---

## Русский

Слушает механические часы через I2S-микрофон и выводит в Serial характеристики
хода: ритм (пк/ч), отклонение в % и сек/сут, разнобой и указание, в какую
сторону крутить регулятор. Номинальный ритм определяется автоматически.

### Железо

- ESP32-WROOM-32 (плата `esp32dev`)
- I2S MEMS-микрофон: **INMP441** или **SPH0645** — работают оба (см. про слот ниже)

#### Распайка

| Микрофон | ESP32 | Назначение |
|---|---|---|
| VDD | 3V3 | питание, только 3.3 В |
| GND | GND | земля |
| SCK / BCLK | GPIO26 | I2S бит-клок |
| WS / LRCK | GPIO25 | I2S выбор слова |
| SD / DOUT | GPIO32 | данные |
| L/R / SEL | GND или 3V3 | выбор слота I2S |

Пины меняются в `src/mic.h` (`MIC_PIN_*`).

Про слот: INMP441 с L/R→GND отдаёт данные в левом слоте (умолчание прошивки).
SPH0645 (и INMP441 с L/R→VDD) — в правом. Команда `ch` переключает слот на лету.

### Сборка и прошивка

```bash
pio run -e esp32dev -t upload
pio device monitor          # 115200 бод
```

### Использование

Положите микрофон на корпус часов — со стороны стекла/циферблата звук
громче, чем через заднюю крышку. После перекладки часов дайте 15–20 с на
затихание и отправьте `rst` для чистого замера. Показания стабилизируются
за ~60–90 с (буфер + EMA-сглаживание).

#### Пример отчёта

```
ритм опознан: 21600 пк/ч
ПК/Ч 21582 (авто 21600) | -0.07% | -58.1 сек/сут | отстаёт -> УМЕНЬШИТЬ период | разнобой +1.5 мс | ударов 501, мимо 144
```

#### Команды Serial

| Команда | Действие |
|---|---|
| `bph <n>` | задать номинальный ритм вручную, 12000–40000 пк/ч |
| `auto` | вернуться к автодетекту ритма |
| `rst` | сбросить статистику (после перекладки часов) |
| `ch` | переключить слот I2S левый/правый |
| `dbg` | отладка детектора: шумовой пол, огибающая, интервалы ударов |
| `raw` | дамп сырых сэмплов I2S |
| `cap` | захват ~0.5 с, огибающая по окнам |
| `help` | список команд |

### Как это работает

1. I2S-приёмник читает 48 кГц × 32 бита чанками по 512 сэмплов; метки
   времени — абсолютный счётчик сэмплов (не `millis()`).
2. `tickdsp`: дифференциатор (срез DC и низких частот) → огибающая
   fast-attack/slow-decay → адаптивный порог к шумовому полу → события с
   рефракторным периодом ~2 мс → кластеризация щелчков спуска (~20 мс) в
   удары. Метка удара — энергетический центроид всплеска.
3. `rateest`: кольцевой буфер парных интервалов (2 полуколебания — так
   сокращается асимметрия тик/так), отброс выбросов и кратных интервалов,
   автодетект номинала из типовых ритмов (14400–36000 пк/ч), среднее по
   буферу + EMA для отчёта. Разнобой — EMA одиночных интервалов по
   паритету удара.

### Тесты

DSP-модули не зависят от Arduino и тестируются на хосте синтетическим
сигналом (шум + затухающие импульсы с заданным ритмом, джиттером,
пропусками и разнобоем):

```bash
pio test -e native          # 9 тестов
```

### Структура

```
src/mic.{h,cpp}      — I2S-приёмник (legacy driver), выбор слота
src/tickdsp.{h,cpp}  — детектор ударов (чистый C++)
src/rateest.{h,cpp}  — статистика интервалов, автодетект, разнобой
src/main.cpp         — цикл, отчёты, команды Serial
test/test_dsp/       — нативные тесты на синтетике
.devin/skills/       — скилл: методика замера в разных положениях
example-session.md   — пример сессии анализа часов (DG2813)
```

### Ограничения

- Амплитуда баланса не измеряется (для неё нужен угол подъёма спуска).
- Разнобой ненадёжен при доле отброшенных интервалов > ~10 % — пропущенный
  удар переворачивает паритет тик/так.
- Выбор слота I2S не запоминается: после ресета приёмник снова на левом —
  при необходимости отправляйте `ch`.
- Для измерений критично положение микрофона: через заднюю крышку звук
  слабее и часть ударов теряется.

---

## English

Listens to a mechanical watch through an I2S microphone and prints movement
rate characteristics to the serial console: beats per hour, deviation in %
and s/day, beat error, and which way to turn the regulator. The nominal
rate is detected automatically.

### Hardware

- ESP32-WROOM-32 (`esp32dev` board)
- I2S MEMS microphone: **INMP441** or **SPH0645** — both work (see slot note below)

#### Wiring

| Microphone | ESP32 | Function |
|---|---|---|
| VDD | 3V3 | power, 3.3 V only |
| GND | GND | ground |
| SCK / BCLK | GPIO26 | I2S bit clock |
| WS / LRCK | GPIO25 | I2S word select |
| SD / DOUT | GPIO32 | data |
| L/R / SEL | GND or 3V3 | I2S slot select |

Pins can be changed in `src/mic.h` (`MIC_PIN_*`).

Slot note: an INMP441 with L/R→GND outputs on the left slot (firmware
default). SPH0645 (and INMP441 with L/R→VDD) outputs on the right slot.
The `ch` command switches slots at runtime.

### Build and flash

```bash
pio run -e esp32dev -t upload
pio device monitor          # 115200 baud
```

### Usage

Place the microphone on the watch case — the sound is louder on the
crystal/dial side than through the case back. After repositioning the
watch, wait 15–20 s for transients to settle and send `rst` for a clean
measurement. Readings stabilize within ~60–90 s (buffer + EMA smoothing).

#### Sample report

```
ритм опознан: 21600 пк/ч
ПК/Ч 21582 (авто 21600) | -0.07% | -58.1 сек/сут | отстаёт -> УМЕНЬШИТЬ период | разнобой +1.5 мс | ударов 501, мимо 144
```

Report fields (the console output itself is in Russian):

- `ПК/Ч` — measured rate, beats/hour; `(авто 21600)` — auto-detected nominal
- `-0.07%` / `-58.1 сек/сут` — deviation in % and seconds/day
- `спешит -> УВЕЛИЧИТЬ период` / `отстаёт -> УМЕНЬШИТЬ период` — fast →
  increase the period / slow → decrease the period
- `разнобой` — beat error, ms (tick/tock asymmetry)
- `ударов N, мимо M` — accepted / rejected intervals

#### Serial commands

| Command | Action |
|---|---|
| `bph <n>` | set nominal rate manually, 12000–40000 bph |
| `auto` | return to automatic rate detection |
| `rst` | reset statistics (after repositioning the watch) |
| `ch` | toggle I2S left/right slot |
| `dbg` | detector debug: noise floor, envelope, beat intervals |
| `raw` | dump raw I2S samples |
| `cap` | ~0.5 s capture, per-window envelope |
| `help` | command list |

### How it works

1. The I2S receiver reads 48 kHz × 32-bit in 512-sample chunks; timestamps
   come from an absolute sample counter (not `millis()`).
2. `tickdsp`: differentiator (removes DC and low frequencies) →
   fast-attack/slow-decay envelope → adaptive threshold above the noise
   floor → events with a ~2 ms refractory period → clustering of escapement
   clicks (~20 ms) into beats. The beat timestamp is the energy centroid
   of the burst.
3. `rateest`: ring buffer of paired intervals (two half-swings, cancelling
   tick/tock asymmetry), outlier and integer-multiple rejection, nominal-rate
   autodetection from common rates (14400–36000 bph), buffer mean + EMA for
   the report. Beat error is an EMA of single intervals tracked separately
   for each beat parity.

### Tests

The DSP modules have no Arduino dependencies and are tested on the host
with a synthetic signal (noise + decaying pulses at a known rate, jitter,
missed beats, and beat error):

```bash
pio test -e native          # 9 tests
```

### Project layout

```
src/mic.{h,cpp}      — I2S receiver (legacy driver), slot selection
src/tickdsp.{h,cpp}  — beat detector (plain C++)
src/rateest.{h,cpp}  — interval statistics, autodetect, beat error
src/main.cpp         — main loop, reports, serial commands
test/test_dsp/       — host tests on synthetic data
.devin/skills/       — skill: multi-position measurement workflow
example-session.md   — worked example: watch analysis session (RU)
```

### Limitations

- Balance amplitude is not measured (it requires the escapement lift angle).
- Beat error is unreliable when more than ~10 % of intervals are rejected —
  a missed beat flips the tick/tock parity.
- The I2S slot selection is not persisted: after reset the receiver is back
  on the left slot — send `ch` if needed.
- Microphone placement matters: through the case back the sound is weaker
  and some beats are lost.

## License

[MIT](LICENSE)
