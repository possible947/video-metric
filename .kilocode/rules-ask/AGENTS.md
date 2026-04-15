# Project Documentation Rules (Non-Obvious Only)

- README defaults are partially stale: it says VMAF default resolution is `4k`, but parser default is `hd` in [`parse_cli()`](src/cli_parser.c:70).
- Environment-check docs/comments overstate coverage: header/docs describe filter/model checks, while current implementation only verifies PATH `ffmpeg` in [`check_environment()`](src/env_check.c:21).
- File name `vmaf.json` is misleading: parser in [`parse_vmaf_json()`](src/vmaf.c:85) reads frame attributes like `vmaf=` from XML-like content.
- “Single test” in this repo means one direct binary invocation with one metric flag (e.g., `-s`) rather than a framework test target, because only [`Makefile`](Makefile) build/clean targets exist.
