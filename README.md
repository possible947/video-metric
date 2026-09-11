# Video Metric

**Video Metric** – утилита для Linux и macOS, написанная на GNU‑C, которая сравнивает два видеофайла и выводит метрики качества: **SSIM** (Structural Similarity Index), **MS-SSIM** (Multi-Scale SSIM) или **VMAF** (Video Multi‑Method Assessment Fusion). Утилита использует локальный `ffmpeg`, расположенный рядом с исполняемым файлом, и пред‑обученные модели VMAF.

Доступны два режима работы: **консольный** (`video_metric`) и **графический** (`video_metric_gui`) — GTK4-приложение с прогресс-барами и наглядным отображением результатов.

Все метрики вычисляются покадрово и выводятся в виде **min/max/mean** статистики.

---

## 1. Структура проекта

```text
video_metric/
├── Makefile
├── config.h
├── src/
│   ├── main.c              # Точка входа CLI и оркестратор
│   ├── cli_parser.c        # Парсинг аргументов командной строки
│   ├── cli_options.h       # Структура опций CLI
│   ├── env_check.c         # Проверка окружения (ffmpeg, фильтры, модели)
│   ├── path_util.c         # Утилиты для работы с путями
│   ├── ssim.c              # Вычисление SSIM через FFmpeg-фильтр
│   ├── ssim.h
│   ├── vmaf.c              # Вычисление VMAF через FFmpeg-фильтр
│   ├── vmaf.h
│   ├── ms_ssim.c           # Вычисление MS-SSIM (оркестратор)
│   ├── ms_ssim.h
│   ├── msssim_core.c       # Алгоритм MS-SSIM (чистый C)
│   ├── msssim_core.h
│   ├── pipe_decoder.c      # FFmpeg pipe-декодер для MS-SSIM
│   ├── pipe_decoder.h
│   ├── metrics_common.h    # Общая структура статистики и progress_cb
│   ├── gui_main.c          # Точка входа GUI-приложения
│   ├── ui_main.c           # GTK4 окно, виджеты, обработчики событий
│   ├── ui_main.h
│   ├── worker.c            # Фоновые вычисления (GTask)
│   ├── worker.h
│   ├── app_state.c         # Состояние приложения
│   └── app_state.h
├── model/
│   ├── vmaf_v0.6.1.json        # Модель VMAF для HD
│   └── vmaf_4k_v0.6.1.json     # Модель VMAF для 4K
├── video_metric            # Скомпилированный CLI-бинарник
├── video_metric_gui        # Скомпилированный GUI-бинарник
├── ffmpeg                  # Локальный бинарник FFmpeg (не входит в репозиторий)
└── README.md
```

---

## 2. Возможности

### Метрики

- **SSIM** – Structural Similarity Index для оценки сходства структур изображения
- **MS-SSIM** – Multi-Scale SSIM (5-уровневая пирамида) для более точной оценки качества
- **VMAF** – Video Multi-Method Assessment Fusion от Netflix

### Статистика

Все метрики вычисляются для каждого кадра и выводятся в виде:

- **Min** – минимальное значение по всем кадрам
- **Max** – максимальное значение по всем кадрам
- **Mean** – среднее значение по всем кадрам

Пример вывода:

```text
SSIM - Min: 0.862503, Max: 0.904785, Mean: 0.892969
MS-SSIM - Min: 0.996753, Max: 0.997313, Mean: 0.997075
VMAF - Min: 94.004434, Max: 96.322506, Mean: 95.976802
```

### Поддерживаемые платформы

- **Linux** – основная платформа разработки
- **macOS** – полная поддержка (ARM64 и x86_64), включая GTK4-GUI через MacPorts

---

## 3. GUI-приложение (video_metric_gui)

`video_metric_gui` — GTK4-приложение с Quartz-бэкендом на macOS.

### Возможности GUI

- Выбор файлов через диалог
- Переключатели метрик: SSIM / MS-SSIM / VMAF
- Выбор разрешения VMAF: HD (1080p) / 4K (2160p)
- Настройка числа потоков
- Два прогресс-бара: общий (`Step N/M`) и текущей метрики (`%`)
- Кнопка отмены
- Панель результатов (Min / Max / Mean для каждой метрики)
- Автоматическое следование светлой/тёмной теме ОС

### Требования для GUI

- **macOS:** MacPorts + `sudo port install gtk4 +quartz`
- **Linux:** системный пакет `libgtk-4-dev` (apt) / `gtk4-devel` (dnf)

GUI вычисляет метрики напрямую через те же функции, что и CLI — без запуска дочернего процесса.

---

## 3. Проверка окружения

Перед выполнением любой операции утилита выводит статус проверки окружения:

```text
ffmpeg: <status>, ssim: <status>, vmaf: <status>
```

`<status>` принимает значения **ok** или **fail**.

Проверка выполняет следующее:

1. **Наличие FFmpeg** – ищет бинарник `ffmpeg` в той же директории, что и `video_metric`
2. **Фильтры FFmpeg** – проверяет наличие фильтров `ssim` и `libvmaf`
3. **Модели VMAF** – убеждается в наличии файлов в директории `model/`
4. **Тестовый запуск VMAF** – запускает `ffmpeg` с `libvmaf` для проверки работоспособности

Если `ffmpeg` отсутствует, утилита завершается сразу без выполнения дальнейших шагов.

---

## 4. Требования к локальному FFmpeg

- **Путь** – бинарник `ffmpeg` должен находиться в том же каталоге, что и `video_metric`
- **Версия** – любой FFmpeg, поддерживающий фильтры `ssim` и `libvmaf`; для GPU-ускоренного VMAF дополнительно нужен фильтр `libvmaf_cuda` и рабочий CUDA runtime
- **Потоки** – при расчёте SSIM используется `-threads <N>`, при VMAF – `n_threads=<N>`
- **Автоматический режим** – если параметр `-n` не указан или равен `0`, SSIM передаёт `-threads 0` в FFmpeg, а VMAF и MS-SSIM определяют число доступных CPU и используют его.
- **VMAF и память** – используемая сборка FFmpeg/libvmaf должна содержать исправление утечки памяти. Программа не устанавливает искусственный лимит памяти для VMAF и использует память, необходимую FFmpeg.

### Выбор backend для VMAF

По умолчанию VMAF работает в автоматическом режиме: утилита проверяет локальный FFmpeg, пробует `libvmaf_cuda` только для совместимых 8-битных входов `yuv420p`, и откатывается на CPU-фильтр `libvmaf`, если CUDA backend недоступен, входной формат несовместим или расчёт завершается ошибкой.

CUDA backend используется только для расчёта самой метрики VMAF. Декодирование входных видео по умолчанию остаётся CPU-декодированием, после чего кадры загружаются в CUDA через `hwupload_cuda`. CUDA не используется для 10-битных, 4:2:2, HDR и других non-`yuv420p` входов, потому что для них потребовалось бы преобразование кадра перед метрикой, а это меняет интерпретацию результата. Аппаратное декодирование не включается автоматически и может рассматриваться только как отдельный явный performance-режим.

Режим можно переопределить переменной окружения:

```bash
# Автоматический режим: CUDA при возможности, иначе CPU
VIDEO_METRIC_VMAF_BACKEND=auto ./video_metric -o original.mkv -t test.mp4 -v

# Только CPU libvmaf
VIDEO_METRIC_VMAF_BACKEND=cpu ./video_metric -o original.mkv -t test.mp4 -v

# Запросить CUDA libvmaf_cuda; несовместимые входы всё равно пойдут через CPU
VIDEO_METRIC_VMAF_BACKEND=cuda ./video_metric -o original.mkv -t test.mp4 -v
```

Если переменная не задана, используется `auto`. Проверка выполняется для bundled FFmpeg, который используется метрикой VMAF, а не для системного `ffmpeg` из `PATH`.

**Для MS-SSIM** FFmpeg используется только для декодирования через pipe (не требуется специальный фильтр).

---

## 5. Требования к моделям VMAF

- `model/vmaf_v0.6.1.json` – модель для HD (1080p)
- `model/vmaf_4k_v0.6.1.json` – модель для 4K (2160p)
- При расчёте VMAF утилита выбирает модель в зависимости от флага `-r` (`hd` по умолчанию)

---

## 6. Обработка путей

Утилита корректно обрабатывает:

- пробелы, кавычки, символы UTF‑8
- escape‑последовательности (например, `\n`, `\t`, `\\`)
- пути, перетаскиваемые из файловых менеджеров

Все пути, передаваемые в вызов `ffmpeg`, заключаются в двойные кавычки и экранируются функцией `escape_path`.

Пути к `ffmpeg` и моделям VMAF проверяются на переполнение внутренних буферов. Если путь не помещается целиком, утилита завершает операцию с ошибкой, не используя усечённое значение.

---

## 7. Вычисление метрик

### 7.1 SSIM

**Метод:** FFmpeg фильтр `ssim` с параметром `stats_file` для покадровой статистики.

Команда:

```bash
./ffmpeg -i "<original>" -i "<test>" \
-vf "ssim=stats_file=ssim_stats_temp.log" \
-f null - -threads <N>
```

Утилита парсит файл статистики и извлекает значение `All:` для каждого кадра.

### 7.2 MS-SSIM

**Метод:** Декодирование через FFmpeg pipe и вычисление MS-SSIM в чистом C по алгоритму Wang et al. (2003).

Алгоритм:

1. Декодирование обоих видео в raw YUV через FFmpeg pipe
2. Извлечение Y-plane (яркость) для каждого кадра
3. Построение 5-уровневой Gaussian пирамиды
4. Вычисление SSIM на каждом уровне с Gaussian окном 11×11
5. Комбинирование уровней с весами: `{0.0448, 0.2856, 0.3001, 0.2363, 0.1333}`

**Преимущества:**

- Не требует специального ffmpeg фильтра
- Полный контроль над алгоритмом
- Поддержка любых форматов (через FFmpeg декодирование)

**Минимальное разрешение:** 176×144 (для 5-уровневой пирамиды)

### 7.3 VMAF

**Метод:** FFmpeg фильтр `libvmaf_cuda` или `libvmaf` с XML выводом.

В автоматическом режиме утилита сначала пытается использовать CUDA backend, если оба входных видео имеют пиксельный формат `yuv420p`:

CUDA-команда не включает `-hwaccel`: декодирование выполняется на CPU, а CUDA применяется после `hwupload_cuda` только для вычисления VMAF.

```bash
./ffmpeg -i "<original>" -i "<test>" \
-filter_complex "[0:v]format=yuv420p,hwupload_cuda[dist];[1:v]format=yuv420p,hwupload_cuda[ref];[dist][ref]libvmaf_cuda=model=path=<model_path>:n_threads=0:log_path=vmaf.json" \
-f null -
```

Если CUDA backend недоступен или расчёт завершается ошибкой, `auto` режим повторяет расчёт на CPU.

CPU-команда:

```bash
./ffmpeg -i "<original>" -i "<test>" \
-filter_complex "[0:v][1:v]libvmaf=model=path=<model_path>:n_threads=<N>:log_path=vmaf.json" \
-f null - -threads <N>
```

Утилита парсит XML-файл и извлекает атрибуты `vmaf="value"` для каждого кадра.

---

## 8. Формат запуска

```text
./video_metric [OPTIONS]
```

### Опции

| Флаг | Псевдоним | Описание |
| ---- | --------- | -------- |
| `-o` | `--output` | Путь к оригинальному видео (референс) |
| `-t` | `--test` | Путь к проверяемому видео |
| `-s` | `--ssim` | Вычислить SSIM |
| `-m` | `--ms-ssim` | Вычислить MS-SSIM |
| `-v` | `--vmaf` | Вычислить VMAF |
| `-r` | `--resolution` | `hd` или `4k` (по умолчанию `hd`, только для VMAF) |
| `-n` | `--num_threads` | Число потоков для FFmpeg |

### Поведение по умолчанию

**Если не указаны флаги метрик** (`-s`, `-m`, `-v`), утилита вычисляет **все доступные метрики**.

### Примеры

```bash
# Все метрики (по умолчанию)
./video_metric -o video_orig.mp4 -t video_test.mp4

# Все метрики с 20 потоками
./video_metric -o video_orig.mp4 -t video_test.mp4 -n 20

# SSIM отдельно
./video_metric -o video_orig.mp4 -t video_test.mp4 -s

# MS-SSIM отдельно
./video_metric -o video_orig.mp4 -t video_test.mp4 -m

# VMAF для HD с 4 потоками
./video_metric -o video_orig.mp4 -t video_test.mp4 -v -r hd -n 4

# VMAF для 4K с 8 потоками
./video_metric -o video_orig_4k.mp4 -t video_test_4k.mp4 -v -r 4k -n 8

# SSIM и VMAF одновременно
./video_metric -o video_orig.mp4 -t video_test.mp4 -s -v -n 20
```

---

## 9. Сборка проекта

### Linux

```bash
# Установить зависимости
sudo apt-get install build-essential

# Сборка CLI (OpenMP определяется автоматически)
make

# Сборка GUI (требует GTK4)
sudo apt-get install libgtk-4-dev
make gui

# Очистка
make clean
```

### macOS (MacPorts)

Мake **автоматически** определяет MacPorts clang (версии 18 → 20 → 16 → 14) и включает OpenMP. Вмешательство не требуется.

```bash
# Установить MacPorts: https://www.macports.org/install.php

# Установить clang с OpenMP (один раз)
sudo port install clang-18      # или актуальную версию

# Сборка CLI — компилятор и OpenMP выбираются автоматически
make

# Сборка GUI — дополнительно нужен GTK4
sudo port install gtk4 +quartz
make gui

# Сборка обоих бинарников
make && make gui

# Очистка
make clean
```

При запуске `make` консоль сообщает, какой компилятор выбран:

```text
[compiler] MacPorts clang-mp-18 auto-selected
[threading] OpenMP enabled (explicit)
```

Если MacPorts clang не найден:

```text
[compiler] MacPorts clang not found, using system cc + pthreads
[threading] OpenMP not available, using pthreads
```

#### Явное управление компилятором и потоками

```bash
# Конкретная версия clang
make CC=clang-mp-20

# Принудительно pthreads (например, для системного clang)
make MSSSIM_USE_OPENMP=no

# Принудительно OpenMP
make CC=clang-mp-18 MSSSIM_USE_OPENMP=yes

# Однопоточная сборка (отладка)
make MSSSIM_SINGLE_THREAD=yes
```

**Примечание:** Бинарник `ffmpeg` должен быть помещён в корень проекта вручную (не входит в репозиторий).

---

## 10. Зависимости времени компиляции

- **gcc** или **clang** – поддержка C11 (`-std=gnu11`)
- **make** – система сборки
- **libm** – математическая библиотека (для MS-SSIM: `pow()`, `sqrt()`)
- **pthreads** или **OpenMP** – многопоточность (выбирается автоматически или явно)

Дополнительные библиотеки **не требуются** (libavcodec, libavformat и т.д. не используются).

---

## 11. Архитектура

### SSIM и VMAF

```text
CLI → FFmpeg Filter → Parse Output → Statistics
      (./ffmpeg)     (text parsing)   (min/max/mean)
```

### MS-SSIM

```text
CLI → FFmpeg Pipe → Y-plane Extract → MS-SSIM Algorithm → Statistics
      (raw YUV)     (uint8→float)      (pure C)           (min/max/mean)
```

---

## 12. Производительность

Тестовое окружение: macOS, 4K видео (3840×2160), 30 fps

| Метрика | Скорость (1 сек видео) | Многопоточность |
| ------- | ---------------------- | --------------- |
| SSIM | ~8 сек | Да (`-n` флаг) |
| MS-SSIM | ~15-30 сек | Частично |
| VMAF | ~2-3 мин | Да (`-n` флаг) |

**Рекомендация:** Используйте `-n 20` для существенного ускорения.

---

## 13. Известные ограничения

1. **MS-SSIM** без OpenMP работает однопоточно (используйте MacPorts clang для 3–5x ускорения)
2. **VMAF** может быть медленным на больших видео (зависит от libvmaf в FFmpeg)
3. **Минимальное разрешение для MS-SSIM:** 176×144 (требование 5-уровневой пирамиды)

---

## 14. Лицензия

Проект распространяется под лицензией MIT. См. файл `LICENSE`.

---
