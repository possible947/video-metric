# Project Architecture Rules (Non-Obvious Only)

- Metric execution has a split dependency contract: preflight uses PATH `ffmpeg` in [`check_environment()`](src/env_check.c:21), but actual compute modules hard-require local `./ffmpeg` via per-module [`get_ffmpeg_path()`](src/ssim.c:35), [`get_ffmpeg_path()`](src/vmaf.c:36), [`get_ffmpeg_path()`](src/pipe_decoder.c:26).
- VMAF pipeline is tightly coupled to a fixed artifact name and parser behavior: [`compute_vmaf()`](src/vmaf.c:173) writes `vmaf.json`, then [`parse_vmaf_json()`](src/vmaf.c:85) scans XML-style `vmaf=` frame attributes.
- MS-SSIM architecture is decode-pipe + pure-C compute (not ffmpeg filter): metadata/parsing in [`get_video_info()`](src/pipe_decoder.c:46), raw YUV stream in [`pipe_decoder_open()`](src/pipe_decoder.c:165), then frame scoring in [`compute_msssim_frame()`](src/msssim_core.c:626).
- Build-time threading policy (OpenMP/pthreads/single-thread) is selected in [`Makefile`](Makefile), but runtime thread count is injected via [`msssim_set_num_threads()`](src/msssim_core.c:46) and may still auto-resolve from `MSSSIM_THREADS` env var in pthread mode.
- Resolution handling differs by metric: VMAF model selection depends on CLI `-r` in [`main()`](src/main.c:169), while MS-SSIM ignores that flag and enforces only dimension parity + minimum size in [`compute_ms_ssim()`](src/ms_ssim.c:105).
