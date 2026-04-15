# Project Debug Rules (Non-Obvious Only)

- If runtime check passes but metrics fail immediately with "local ffmpeg not found", reconcile PATH/local mismatch between [`check_environment()`](src/env_check.c:21) and local-only resolvers in [`get_ffmpeg_path()`](src/ssim.c:35), [`get_ffmpeg_path()`](src/vmaf.c:36), [`get_ffmpeg_path()`](src/pipe_decoder.c:26).
- VMAF failures often come from parsing assumptions, not filter execution: inspect `vmaf.json` for frame `vmaf=` attributes expected by [`parse_vmaf_json()`](src/vmaf.c:85), despite the `.json` extension.
- MS-SSIM returns hard error below 176x144 at [`compute_ms_ssim()`](src/ms_ssim.c:112); this is an intentional guard, not a decode bug.
- For path-related failures, check whether command strings bypass [`escape_path()`](src/path_util.c:33) (notably raw-quoted paths in decoder command construction at [`get_video_info()`](src/pipe_decoder.c:46) / [`pipe_decoder_open()`](src/pipe_decoder.c:165)).
