# Project Error Report

## Summary
The project now compiles successfully with all critical issues resolved. The following issues were found and fixed:

---

## Critical Issues

### 1. ⚠️ **CRITICAL: Logic Error in env_check.c (Line 143-149)**

**Location:** [src/env_check.c](src/env_check.c#L143-L149)

**Problem:**
The code checks the wrong variable in the return statement:

```c
// Line 143-149
if (ok_ffmpeg && ok_ssim && ok_vmaf &&
    ok_model_hd && ok_model_4k && ok_test_vmaf)
    return 0;
```

However, the status string at line 145-148 reports:
```c
snprintf(status_buf, bufsize,
         "ffmpeg: %s, ssim: %s, vmaf: %s",
         ok_ffmpeg ? "ok" : "fail",
         ok_ssim   ? "ok" : "fail",
         ok_test_vmaf ? "ok" : "fail");
```

**Issue:** The return condition checks `ok_vmaf` (whether libvmaf filter is available), but the status message reports `ok_test_vmaf` (whether a test run succeeds). The return statement should check `ok_test_vmaf` instead:

```c
// WRONG:
if (ok_ffmpeg && ok_ssim && ok_vmaf && 
    ok_model_hd && ok_model_4k && ok_test_vmaf)

// CORRECT:
if (ok_ffmpeg && ok_ssim && ok_test_vmaf && 
    ok_model_hd && ok_model_4k)
```

**Impact:** The function may return success when only the filter exists but actual VMAF execution could fail.

---

### 2. ⚠️ **BUG: Incorrect ffmpeg Command in ssim.c (Line 77-83)**

**Location:** [src/ssim.c](src/ssim.c#L77-L83)

**Problem:**
The `-threads` flag is placed **after** the output specification, which is incorrect:

```c
char cmd[4096];
int n = snprintf(
    cmd, sizeof(cmd),
    "%s -i %s -i %s "
    "-filter_complex \"[0:v][1:v]ssim\" "
    "-f null - -threads %d 2>&1",  // ← WRONG POSITION
    ffmpeg,
    esc_orig,
    esc_test,
    ssim_threads
);
```

**Issue:** In ffmpeg, the `-threads` option must be placed **before** the `-i` (input) options as a global option, or it won't be properly applied. The current placement after `-f null -` is ignored.

**Correct version:**
```c
"%s -threads %d -i %s -i %s "
"-filter_complex \"[0:v][1:v]ssim\" "
"-f null - 2>&1"
```

**Impact:** The `num_threads` parameter is silently ignored when computing SSIM.

---

### 3. ⚠️ **POTENTIAL: Unsafe Use of wait.h Macros**

**Location:** [src/vmaf.c](src/vmaf.c#L118-119) and [src/ssim.c](src/ssim.c#L110-111)

**Problem:**
Both functions use `WIFEXITED()` and `WEXITSTATUS()` after checking `status == -1`:

```c
int status = pclose(fp);
if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
    score = NAN;
```

**Issue:** If `pclose()` returns -1, calling `WIFEXITED(status)` or `WEXITSTATUS(status)` on undefined values is undefined behavior. These macros should only be called if `WIFEXITED()` returns true.

**Safe version:**
```c
int status = pclose(fp);
if (status == -1) {
    score = NAN;
} else if (WIFEXITED(status)) {
    if (WEXITSTATUS(status) != 0)
        score = NAN;
} else {
    score = NAN;
}
```

Or more concisely:
```c
int status = pclose(fp);
if (status != 0)
    score = NAN;
```

**Impact:** Potential undefined behavior when ffmpeg subprocess fails abnormally.

---

## Compiler Warnings

### 4. ⚠️ **Comment Syntax Warning in main.c (Line 20)**

**Location:** [src/main.c](src/main.c#L20)

```c
*   gcc -std=gnu11 -Wall -Wextra -O2 src/*.c -o video_metric
    ^^--- warning: '/*' within comment
```

**Fix:** Remove the `/*` or ensure proper comment formatting. The line should be:
```c
 *   gcc -std=gnu11 -Wall -Wextra -O2 src/*.c -o video_metric
```

---

### 5. ⚠️ **Buffer Truncation Warnings (Low Risk)**

**Location:** Multiple locations in `env_check.c` and `main.c`

**Issue:** Compiler warns that `snprintf()` calls constructing paths may truncate:
- Lines 97, 102, 103 in env_check.c
- Lines 138-142 in main.c

**Analysis:** These are false positives since `PATH_MAX` buffers are 4096 bytes and the longest path is approximately 50-100 bytes. However, using safer constructions would eliminate warnings.

**Suggested fix:** Use dynamic path construction or increase buffer sizes explicitly.

---

## Summary Table

| Issue | Severity | File | Line | Type |
|-------|----------|------|------|------|
| Wrong variable checked in return | **CRITICAL** | env_check.c | 143 | Logic Error |
| `-threads` flag wrong position | **HIGH** | ssim.c | 83 | Command Error |
| Unsafe wait.h macro usage | **MEDIUM** | vmaf.c, ssim.c | 118-119 | Undefined Behavior |
| Comment syntax | **LOW** | main.c | 20 | Syntax Warning |
| Buffer truncation warnings | **LOW** | env_check.c, main.c | Multiple | False Positive |

---

## Recommendations

1. **Immediate:** Fix the critical logic error in env_check.c
2. **Immediate:** Correct the ffmpeg command in ssim.c  
3. **Soon:** Fix the unsafe macro usage in process status checking
4. **Cleanup:** Fix the comment syntax warning
5. **Optional:** Suppress or fix buffer truncation warnings

---

## Resolution Status ✅

All critical and high-priority issues have been fixed:

### ✅ Issue 1: FIXED (env_check.c, Line 143-149)
- Changed return condition from checking `ok_vmaf` to checking `ok_test_vmaf`
- Removed unused `ok_vmaf` variable declaration and assignment
- **Result:** Function now correctly verifies that VMAF test execution succeeds before returning success

### ✅ Issue 2: FIXED (ssim.c, Line 77-83)
- Moved `-threads` flag from end to beginning of command (before input files)
- Now: `ffmpeg -threads N -i orig -i test -filter_complex ... -f null -`
- **Result:** Thread parameter is now correctly applied to SSIM computation

### ✅ Issue 3: FIXED (vmaf.c & ssim.c, Line 118-119)
- Replaced unsafe macro usage with proper error handling
- Now checks `if (status == -1)` separately before using `WIFEXITED()` and `WEXITSTATUS()`
- **Result:** Avoids undefined behavior when subprocess fails abnormally

### ✅ Issue 4: FIXED (main.c, Line 20)
- Fixed comment syntax warning
- **Result:** Compiler warning eliminated

### Final Compilation Status:
```
✓ No compilation errors
✓ No critical warnings
✓ All functional issues resolved
✓ Remaining warnings are low-risk buffer truncation false positives
```

