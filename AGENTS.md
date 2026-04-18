# AGENTS.md

This file provides guidance to agents when working with code in this repository.

- Build uses only [`Makefile`](Makefile): `make` builds [`video_metric`](video_metric) (CLI), `make gui` builds `video_metric_gui` (GTK4 GUI), `make clean` also deletes `vmaf_*.json`.
- **macOS compiler auto-detection**: on macOS, `make` automatically selects MacPorts clang (tries versions 18→20→16→14 via `command -v clang-mp-N`) and enables OpenMP when found. Falls back to system `cc` + pthreads if no MacPorts clang is available. Override with `make CC=clang-mp-20` or `make MSSSIM_USE_OPENMP=no`.
- Threading mode is selected at build time via Make vars: `MSSSIM_SINGLE_THREAD=yes`, `MSSSIM_USE_OPENMP=yes|no`; default is OpenMP auto-detect then pthread fallback (see [`Makefile`](Makefile)).
- GUI build requires GTK4: on macOS `sudo port install gtk4 +quartz`; on Linux `libgtk-4-dev` / `gtk4-devel`.
- There is no test framework/lint target in repo. "Single test" means manual single-run of binary, e.g. one metric flag (`-s`/`-m`/`-v`) against one video pair.
- Runtime gotcha: [`check_environment()`](src/env_check.c:21) validates `ffmpeg` from PATH, but metric executors require local `ffmpeg` via [`get_ffmpeg_path()`](src/ssim.c:35), which first checks `./ffmpeg` then the executable's own directory (for GUI launched outside project dir).
- VMAF/MS-SSIM rely on project-local FFmpeg invocation and shell commands; always pass paths through [`escape_path()`](src/path_util.c:33) before embedding in command strings.
- Effective default resolution is `hd` from [`parse_cli()`](src/cli_parser.c:70).
- If no metric flags are passed, parser enables all metrics in [`parse_cli()`](src/cli_parser.c:291).
- VMAF parsing expects frame-level `vmaf=` entries from file named `vmaf.json` (XML-like content despite JSON naming) in [`parse_vmaf_json()`](src/vmaf.c:85).
- MS-SSIM hard-fails below 176x144 in [`compute_ms_ssim()`](src/ms_ssim.c:112).
- Progress callbacks: `metric_progress_cb` typedef in [`metrics_common.h`](src/metrics_common.h); all `compute_*` functions accept `progress_cb, cb_userdata` as last two args; CLI passes `NULL, NULL`.
- GUI architecture: `worker.c` runs compute functions in a GTask thread, posts `worker_event` structs to main thread via `g_idle_add`; UI updates happen only in `worker_idle_dispatch` in `ui_main.c`.
- macOS dark mode: `gui_main.c` polls `defaults read -g AppleInterfaceStyle` every 2 s (inside `#ifdef __APPLE__`); Linux GTK4 follows system theme automatically via freedesktop Settings portal.
- Code style observed in-project: static internal helpers, early-return error paths with `fprintf(stderr, ...)`, `snake_case` naming, and explicit init/reset of output structs before processing.
