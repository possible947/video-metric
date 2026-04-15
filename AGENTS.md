# AGENTS.md

This file provides guidance to agents when working with code in this repository.

- Build uses only [`Makefile`](Makefile): `make` builds [`video_metric`](video_metric), `make clean` also deletes `vmaf_*.json`.
- Threading mode is selected at build time via Make vars: `MSSSIM_SINGLE_THREAD=yes`, `MSSSIM_USE_OPENMP=yes|no`; default is OpenMP auto-detect then pthread fallback (see [`Makefile`](Makefile)).
- There is no test framework/lint target in repo. “Single test” means manual single-run of binary, e.g. one metric flag (`-s`/`-m`/`-v`) against one video pair.
- Runtime gotcha: [`check_environment()`](src/env_check.c:21) validates `ffmpeg` from PATH, but metric executors require local `./ffmpeg` via [`get_ffmpeg_path()`](src/ssim.c:35), [`get_ffmpeg_path()`](src/vmaf.c:36), [`get_ffmpeg_path()`](src/pipe_decoder.c:26).
- VMAF/MS-SSIM rely on project-local FFmpeg invocation and shell commands; always pass paths through [`escape_path()`](src/path_util.c:33) before embedding in command strings.
- Effective default resolution is `hd` from [`parse_cli()`](src/cli_parser.c:70), despite README text claiming `4k` default.
- If no metric flags are passed, parser enables all metrics in [`parse_cli()`](src/cli_parser.c:291).
- VMAF parsing expects frame-level `vmaf=` entries from file named `vmaf.json` (XML-like content despite JSON naming) in [`parse_vmaf_json()`](src/vmaf.c:85).
- MS-SSIM hard-fails below 176x144 in [`compute_ms_ssim()`](src/ms_ssim.c:112).
- Code style observed in-project: static internal helpers, early-return error paths with `fprintf(stderr, ...)`, `snake_case` naming, and explicit init/reset of output structs before processing.
