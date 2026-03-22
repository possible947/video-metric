## Plan for Adding Windows Support

The current codebase is designed for Linux (POSIX). To make it compile and run on Windows, we need to:

1. **Conditional compilation** – Wrap OS‑specific includes/logic with `#ifdef _WIN32`.
2. **File path handling** – Replace `/` separators with `\\` or use `PATH_SEPARATOR` macro.
3. **Stat / file existence** – Use `_stat()` on Windows instead of `stat()`.
4. **Executable directory** – Use `GetModuleHandle(NULL)` + `GetModuleFileNameA()` and `PathRemoveFileSpecW()` (requires `<windows.h>` and `<shlwapi.h>`).
5. **Threading model** – Ensure any thread‑related code uses POSIX or Windows threads consistently.
6. **Makefile / build system** – Add a separate `CMakeLists.txt` or a Visual Studio project file, or extend the existing Makefile to support `mingw32-gcc`.
7. **Environment check** – Replace shell commands with equivalent PowerShell/command‑prompt checks (e.g., use `Get-Command ffmpeg`).
8. **Error handling** – Use `errno.h` on Windows; some error codes differ.
9. **Testing** – Write a simple test harness that compiles and runs the binary on a Windows machine or in a Docker container with Wine.
10. **Documentation** – Update README.md to include Windows build instructions.

The final patch will introduce new macros and helper functions (`is_windows()`, `get_executable_dir_win()`), update `file_exists()` for Windows, and adjust string literals accordingly.
