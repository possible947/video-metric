# Project Coding Rules (Non-Obvious Only)

- Keep runtime checks and metric execution aligned: [`check_environment()`](src/env_check.c:21) currently validates PATH `ffmpeg`, but execution paths in [`get_ffmpeg_path()`](src/ssim.c:35), [`get_ffmpeg_path()`](src/vmaf.c:36), and [`get_ffmpeg_path()`](src/pipe_decoder.c:26) require local `./ffmpeg`.
- Any shell command embedding user/video paths should use [`escape_path()`](src/path_util.c:33); some decoder code still uses raw quoting (`"%s"`) and is more fragile.
- Preserve CLI defaults from [`parse_cli()`](src/cli_parser.c:62): resolution default is `hd` and absence of `-s/-m/-v` enables all metrics at once.
- VMAF log handling is contract-sensitive: [`compute_vmaf()`](src/vmaf.c:173) writes `vmaf.json`, then [`parse_vmaf_json()`](src/vmaf.c:85) expects frame-level `vmaf=` attributes (XML-like payload despite `.json` name).
- MS-SSIM path must reject small inputs (<176x144) as enforced in [`compute_ms_ssim()`](src/ms_ssim.c:112); do not silently rescale in-code.
