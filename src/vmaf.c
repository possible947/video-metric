/*====================================================================*/
/*  FILE: src/vmaf.c                                                  */
/*====================================================================*/
/**
 * @file   vmaf.c
 * @brief  Compute the VMAF metric between two videos using ffmpeg.
 *
 * Requirements:
 *  - Use ONLY local ffmpeg (./ffmpeg), no system ffmpeg from $PATH.
 *  - Use filter_complex "[0:v][1:v]libvmaf=..." or CUDA libvmaf_cuda when available.
 *  - Read VMAF output from stderr (redirected to stdout via 2>&1).
 *  - Extract the numeric value after "VMAF score:".
 *  - Display progress bar during computation
 *  - Use JSON log only as a success indicator (no parsing).
 *  - Return NAN on any failure.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <ctype.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifdef _WIN32
#   include <windows.h>
#else
#   include <unistd.h>
#endif

#include "path_util.h"
#include "vmaf.h"

/* ------------------------------------------------------------------ */
/*  Always use local ffmpeg                                           */
/* ------------------------------------------------------------------ */
static const char *get_ffmpeg_path(void)
{
    if (access("./ffmpeg", X_OK) == 0)
        return "./ffmpeg";

    /* GUI may run from a different cwd; check the executable's directory */
    static char exe_ffmpeg[PATH_MAX];
    char exe_dir[PATH_MAX];
    if (get_executable_dir(exe_dir, sizeof(exe_dir)) == 0) {
        int n = snprintf(exe_ffmpeg, sizeof(exe_ffmpeg), "%s/ffmpeg", exe_dir);
        if (n > 0 && (size_t)n < sizeof(exe_ffmpeg) && access(exe_ffmpeg, X_OK) == 0)
            return exe_ffmpeg;
    }

    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Helper: parse time string "HH:MM:SS.xx" and return seconds       */
/* ------------------------------------------------------------------ */
static double parse_time_string(const char *timestr)
{
    if (!timestr)
        return -1.0;

    int hours = 0, minutes = 0;
    double seconds_frac = 0.0;

    /* Try to parse HH:MM:SS.xx format */
    int parsed = sscanf(timestr, "%d:%d:%lf", &hours, &minutes, &seconds_frac);
    if (parsed != 3)
        return -1.0;

    return hours * 3600 + minutes * 60 + seconds_frac;
}

/* ------------------------------------------------------------------ */
/*  Helper: print progress bar                                        */
/* ------------------------------------------------------------------ */
static void print_progress_bar(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    int filled = percent / 5;      /* 20 characters = 5% each */
    int empty = 20 - filled;

    printf("\r<%3d%%>", percent);
    for (int i = 0; i < filled; i++) printf("#");
    for (int i = 0; i < empty; i++) printf("-");
    printf(" ");
    fflush(stdout);
}

static int get_auto_thread_count(void)
{
#ifdef _WIN32
    SYSTEM_INFO system_info;
    GetSystemInfo(&system_info);
    return system_info.dwNumberOfProcessors > 0
        ? (int)system_info.dwNumberOfProcessors : 1;
#else
    long count = sysconf(_SC_NPROCESSORS_ONLN);
    return (count > 0 && count <= INT_MAX) ? (int)count : 1;
#endif
}

typedef enum {
    VMAF_BACKEND_AUTO = 0,
    VMAF_BACKEND_CPU,
    VMAF_BACKEND_CUDA
} vmaf_backend_mode;

static vmaf_backend_mode get_vmaf_backend_mode(void)
{
    const char *value = getenv("VIDEO_METRIC_VMAF_BACKEND");
    if (!value || *value == '\0' || strcmp(value, "auto") == 0)
        return VMAF_BACKEND_AUTO;
    if (strcmp(value, "cpu") == 0)
        return VMAF_BACKEND_CPU;
    if (strcmp(value, "cuda") == 0)
        return VMAF_BACKEND_CUDA;

    fprintf(stderr,
            "compute_vmaf: unknown VIDEO_METRIC_VMAF_BACKEND='%s', using auto\n",
            value);
    return VMAF_BACKEND_AUTO;
}

static int command_succeeded(int status)
{
    return status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int cuda_vmaf_available(const char *ffmpeg, const char *esc_model)
{
    static int cached_available = -1;
    if (cached_available != -1)
        return cached_available;

    char esc_ffmpeg[PATH_MAX];
    escape_path(ffmpeg, esc_ffmpeg, sizeof(esc_ffmpeg));

    char cmd[8192];
    int n = snprintf(
        cmd, sizeof(cmd),
        "%s -hide_banner -loglevel error "
        "-f lavfi -i testsrc2=s=176x144:d=0.04 "
        "-f lavfi -i testsrc2=s=176x144:d=0.04 "
        "-filter_complex \"[0:v]format=yuv420p,hwupload_cuda[dist];"
        "[1:v]format=yuv420p,hwupload_cuda[ref];"
        "[dist][ref]libvmaf_cuda=model=path=%s:log_path=vmaf_cuda_probe.xml\" "
        "-frames:v 1 -f null - >/dev/null 2>&1",
        esc_ffmpeg,
        esc_model
    );
    if (n < 0 || (size_t)n >= sizeof(cmd)) {
        cached_available = 0;
        return cached_available;
    }

    remove("vmaf_cuda_probe.xml");
    cached_available = command_succeeded(system(cmd));
    remove("vmaf_cuda_probe.xml");
    return cached_available;
}

/* ------------------------------------------------------------------ */
/*  Helper: parse VMAF JSON log file and extract statistics          */
/* ------------------------------------------------------------------ */
static int parse_vmaf_json(const char *json_path, metric_stats *stats)
{
    FILE *fp = fopen(json_path, "r");
    if (!fp) {
        fprintf(stderr, "parse_vmaf_json: cannot open %s\n", json_path);
        return -1;
    }

    /* Dynamic array to store per-frame VMAF values */
    size_t capacity = 1000;
    size_t count = 0;
    double *values = malloc(capacity * sizeof(double));
    if (!values) {
        perror("malloc");
        fclose(fp);
        return -1;
    }

    /* VMAF JSON is actually XML format. Parse frame vmaf="value" attributes */
    char line[4096];
    
    while (fgets(line, sizeof(line), fp)) {
        /* Look for <frame ... vmaf="value" ... /> or vmaf= attribute */
        char *p = strstr(line, "vmaf=");
        if (!p)
            continue;
        
        /* Move past vmaf= and potential quote */
        p += 5;
        if (*p == '"' || *p == '\'') p++;
        
        /* Parse the number */
        errno = 0;
        char *endptr;
        double v = strtod(p, &endptr);
        
        /* Accept reasonable VMAF values (0-100) */
        if (errno == 0 && endptr != p && !isnan(v) && v >= 0.0 && v <= 100.0) {
            /* Store the value */
            if (count >= capacity) {
                capacity *= 2;
                double *new_values = realloc(values, capacity * sizeof(double));
                if (!new_values) {
                    perror("realloc");
                    free(values);
                    fclose(fp);
                    return -1;
                }
                values = new_values;
            }
            values[count++] = v;
        }
    }

    fclose(fp);

    /* Calculate statistics */
    if (count == 0) {
        free(values);
        fprintf(stderr, "parse_vmaf_json: no VMAF values found in JSON at %s\n", json_path);
        
        /* Print file size for debugging */
        struct stat st;
        if (stat(json_path, &st) == 0) {
            fprintf(stderr, "[DEBUG] JSON file size: %lld bytes\n", (long long)st.st_size);
        }
        return -1;
    }

    double sum = 0.0;
    stats->min = values[0];
    stats->max = values[0];

    for (size_t i = 0; i < count; i++) {
        sum += values[i];
        if (values[i] < stats->min) stats->min = values[i];
        if (values[i] > stats->max) stats->max = values[i];
    }

    stats->mean = sum / count;

    free(values);
    return 0;
}

static int run_vmaf_command(const char *cmd,
                            const char *json_path,
                            double total_duration,
                            metric_stats *stats,
                            metric_progress_cb progress_cb,
                            void *cb_userdata)
{
    /* Fork and execute ffmpeg with pipe to read output */
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return -1;
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        /* Child process: execute ffmpeg */
        close(pipefd[0]); /* Close read end */
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        execl("/bin/sh", "sh", "-c", cmd, NULL);
        exit(127); /* execl failed */
    }

    /* Parent process: read output and track progress */
    close(pipefd[1]); /* Close write end */

    char line[2048];
    FILE *fp = fdopen(pipefd[0], "r");
    if (!fp) {
        perror("fdopen");
        close(pipefd[0]);
        waitpid(pid, NULL, 0);
        return -1;
    }

    while (fgets(line, sizeof(line), fp)) {
        /* Look for progress indicator: time=HH:MM:SS.xx */
        if (total_duration > 0.0) {
            char *timestr = strstr(line, "time=");
            if (timestr) {
                timestr += 5; /* skip "time=" */
                double current_time = parse_time_string(timestr);
                if (current_time >= 0.0) {
                    int percent = (int)(100.0 * current_time / total_duration);
                    if (percent > 100) percent = 100;
                    if (progress_cb)
                        progress_cb(percent, cb_userdata);
                    else
                        print_progress_bar(percent);
                }
            }
        }
    }

    fclose(fp);

    int status;
    waitpid(pid, &status, 0);

    /* Clear progress bar and print newline */
    if (total_duration > 0.0) {
        if (progress_cb)
            progress_cb(100, cb_userdata);
        else
            printf("\n");
    }

    /* Check process exit status */
    if (!command_succeeded(status)) {
        fprintf(stderr, "compute_vmaf: ffmpeg process failed\n");
        return -1;
    }

    /* Parse the JSON log file to extract statistics */
    int rc = parse_vmaf_json(json_path, stats);
    if (rc != 0) {
        fprintf(stderr, "compute_vmaf: failed to parse VMAF scores from JSON\n");
        return -1;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Compute VMAF                                                      */
/* ------------------------------------------------------------------ */
int compute_vmaf(const char *orig,
                 const char *test,
                 const char *model_path,
                 int         threads,
                 char       *json_path,
                 size_t      json_bufsize,
                 metric_stats *stats,
                 metric_progress_cb progress_cb,
                 void *cb_userdata)
{
    if (!orig || !test || !model_path || !json_path || json_bufsize == 0 || !stats) {
        fprintf(stderr, "compute_vmaf: invalid argument(s)\n");
        return -1;
    }

    /* Initialize stats */
    stats->min = INFINITY;
    stats->max = -INFINITY;
    stats->mean = NAN;

    const char *ffmpeg = get_ffmpeg_path();
    if (!ffmpeg) {
        fprintf(stderr, "compute_vmaf: local ffmpeg not found\n");
        return -1;
    }

    /* Get video duration for progress calculation */
    double total_duration = get_video_duration(orig);
    if (total_duration <= 0.0) {
        total_duration = 0.0; /* If we can't get duration, just skip progress */
    }

    /* Expose fixed JSON log path to caller (per spec: used only as success check) */
    const char *log_file = "vmaf.json";
    if (strlen(log_file) >= json_bufsize) {
        fprintf(stderr, "compute_vmaf: json_path buffer too small\n");
        return -1;
    }
    strncpy(json_path, log_file, json_bufsize);
    json_path[json_bufsize - 1] = '\0';

    /* Escape paths */
    char esc_ffmpeg[PATH_MAX];
    char esc_orig[PATH_MAX];
    char esc_test[PATH_MAX];
    char esc_model[PATH_MAX];

    escape_path(ffmpeg,      esc_ffmpeg, sizeof(esc_ffmpeg));
    escape_path(orig,       esc_orig,  sizeof(esc_orig));
    escape_path(test,       esc_test,  sizeof(esc_test));
    escape_path(model_path, esc_model, sizeof(esc_model));

    /* libvmaf treats n_threads=0 as serial, so choose available CPUs. */
    int vmaf_threads = (threads > 0) ? threads : get_auto_thread_count();

    vmaf_backend_mode backend_mode = get_vmaf_backend_mode();

    /* Build ffmpeg command */
    char cmd[8192];
    int n = snprintf(
        cmd, sizeof(cmd),
        "%s -i %s -i %s "
        "-filter_complex \"[0:v][1:v]libvmaf=model=path=%s:n_threads=%d:log_path=%s\" "
        "-f null - 2>&1",
        esc_ffmpeg,
        esc_orig,
        esc_test,
        esc_model,
        vmaf_threads,
        json_path
    );

    if (n < 0 || (size_t)n >= sizeof(cmd)) {
        fprintf(stderr, "compute_vmaf: command buffer overflow\n");
        return -1;
    }

    if (backend_mode != VMAF_BACKEND_CPU && cuda_vmaf_available(ffmpeg, esc_model)) {
        char cuda_cmd[8192];
        int cuda_n = snprintf(
            cuda_cmd, sizeof(cuda_cmd),
            "%s -i %s -i %s "
            "-filter_complex \"[0:v]format=yuv420p,hwupload_cuda[dist];"
            "[1:v]format=yuv420p,hwupload_cuda[ref];"
            "[dist][ref]libvmaf_cuda=model=path=%s:n_threads=0:log_path=%s\" "
            "-f null - 2>&1",
            esc_ffmpeg,
            esc_orig,
            esc_test,
            esc_model,
            json_path
        );
        if (cuda_n < 0 || (size_t)cuda_n >= sizeof(cuda_cmd)) {
            fprintf(stderr, "compute_vmaf: CUDA command buffer overflow\n");
            if (backend_mode == VMAF_BACKEND_CUDA)
                return -1;
        } else {
            remove(json_path);
            if (run_vmaf_command(cuda_cmd, json_path, total_duration, stats,
                                 progress_cb, cb_userdata) == 0) {
                fprintf(stderr, "compute_vmaf: used CUDA VMAF backend\n");
                return 0;
            }

            if (backend_mode == VMAF_BACKEND_CUDA) {
                fprintf(stderr, "compute_vmaf: CUDA VMAF backend failed\n");
                return -1;
            }
            fprintf(stderr,
                    "compute_vmaf: CUDA VMAF failed, falling back to CPU VMAF\n");
        }
    } else if (backend_mode == VMAF_BACKEND_CUDA) {
        fprintf(stderr, "compute_vmaf: CUDA VMAF backend is not available\n");
        return -1;
    }

    remove(json_path);
    return run_vmaf_command(cmd, json_path, total_duration, stats,
                            progress_cb, cb_userdata);
}

