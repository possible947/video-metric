/*====================================================================*/
/*  FILE: src/ssim.c                                                  */
/*====================================================================*/
/**
 * @file   ssim.c
 * @brief  Compute SSIM between two videos using ffmpeg.
 *
 * Requirements:
 *  - Use ONLY local ffmpeg (./ffmpeg)
 *  - Use filter_complex "[0:v][1:v]ssim"
 *  - Read SSIM output from stderr (redirected to stdout via 2>&1)
 *  - Extract the numeric value after "All:"
 *  - Display progress bar during computation
 *  - Return NAN on any failure
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <math.h>
#include <ctype.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/select.h>

#include "path_util.h"
#include "ssim.h"

/* ------------------------------------------------------------------ */
/*  Always use local ffmpeg                                           */
/* ------------------------------------------------------------------ */
static const char *get_ffmpeg_path(void)
{
    if (access("./ffmpeg", X_OK) == 0)
        return "./ffmpeg";

    /* Per project spec: do NOT fall back to system ffmpeg */
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
/*  Helper: parse SSIM value from output line (handles multiple formats) */
/* ------------------------------------------------------------------ */
static int parse_ssim_value(const char *line, double *value)
{
    if (!line || !value)
        return 0;

    /* Strategy 1: Look for "All:" pattern (most common) */
    const char *p = strstr(line, "All:");
    if (p) {
        p += 4; /* skip "All:" */
        while (*p && isspace((unsigned char)*p))
            p++;
        
        char *endptr;
        double v = strtod(p, &endptr);
        if (endptr != p && !isnan(v) && v >= 0.0 && v <= 1.0) {
            *value = v;
            return 1;
        }
    }

    /* Strategy 2: Look for mean/average in output */
    p = strstr(line, "mean:");
    if (!p)
        p = strstr(line, "Mean:");
    
    if (p) {
        p += 5;
        while (*p && isspace((unsigned char)*p))
            p++;
        
        char *endptr;
        double v = strtod(p, &endptr);
        if (endptr != p && !isnan(v) && v >= 0.0 && v <= 1.0) {
            *value = v;
            return 1;
        }
    }

    /* Strategy 3: Look for numbers after SSIM keyword */
    p = strstr(line, "SSIM");
    if (p) {
        /* Skip ahead and look for numbers */
        char *endptr;
        while (*p) {
            double v = strtod(p, &endptr);
            if (endptr != p && !isnan(v) && v >= 0.0 && v <= 1.0) {
                *value = v;
                return 1;
            }
            if (*p == '\0')
                break;
            p = endptr;
            if (!*p || isspace((unsigned char)*p))
                p++;
        }
    }

    return 0;
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

/* ------------------------------------------------------------------ */
/*  Compute SSIM                                                      */
/* ------------------------------------------------------------------ */
int compute_ssim(const char *orig, const char *test, int threads, metric_stats *stats)
{
    if (!orig || !test || !stats) {
        fprintf(stderr, "compute_ssim: NULL input path or stats\n");
        return -1;
    }

    /* Initialize stats */
    stats->min = INFINITY;
    stats->max = -INFINITY;
    stats->mean = NAN;

    const char *ffmpeg = get_ffmpeg_path();
    if (!ffmpeg) {
        fprintf(stderr, "compute_ssim: local ffmpeg not found\n");
        return -1;
    }

    /* Get video duration for progress calculation */
    double total_duration = get_video_duration(orig);
    if (total_duration <= 0.0) {
        total_duration = 0.0; /* If we can't get duration, just skip progress */
    }

    /* Escape paths */
    char esc_orig[PATH_MAX];
    char esc_test[PATH_MAX];

    escape_path(orig, esc_orig, sizeof(esc_orig));
    escape_path(test, esc_test, sizeof(esc_test));

    /* Normalize threads */
    int ssim_threads = (threads > 0) ? threads : 1;

    /* Create temp stats file */
    const char *stats_file = "ssim_stats_temp.log";

    /* Build ffmpeg command with stats_file for per-frame data */
    char cmd[4096];
    int n = snprintf(
        cmd, sizeof(cmd),
        "%s -threads %d -i %s -i %s "
        "-filter_complex \"[0:v][1:v]ssim=stats_file=%s\" "
        "-f null - 2>&1",
        ffmpeg,
        ssim_threads,
        esc_orig,
        esc_test,
        stats_file
    );

    if (n < 0 || (size_t)n >= sizeof(cmd)) {
        fprintf(stderr, "compute_ssim: command buffer overflow\n");
        return -1;
    }

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

    /* Dynamic array to store per-frame SSIM values */
    size_t capacity = 1000;
    size_t count = 0;
    double *values = malloc(capacity * sizeof(double));
    if (!values) {
        perror("malloc");
        close(pipefd[0]);
        waitpid(pid, NULL, 0);
        return -1;
    }

    char line[2048];
    FILE *fp = fdopen(pipefd[0], "r");
    if (!fp) {
        perror("fdopen");
        close(pipefd[0]);
        free(values);
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
        printf("\n");
    }

    /* Check process exit status */
    if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        free(values);
        unlink(stats_file);
        fprintf(stderr, "compute_ssim: ffmpeg process failed\n");
        return -1;
    }

    /* Parse stats file for per-frame values */
    FILE *stats_fp = fopen(stats_file, "r");
    if (!stats_fp) {
        free(values);
        unlink(stats_file);
        fprintf(stderr, "compute_ssim: cannot open stats file\n");
        return -1;
    }

    char stats_line[1024];
    while (fgets(stats_line, sizeof(stats_line), stats_fp)) {
        /* Parse line: n:1 Y:0.992647 U:0.995634 V:0.996532 All:0.993792 (22.070712) */
        double all_value = 0.0;
        if (parse_ssim_value(stats_line, &all_value)) {
            if (count >= capacity) {
                capacity *= 2;
                double *new_values = realloc(values, capacity * sizeof(double));
                if (!new_values) {
                    perror("realloc");
                    free(values);
                    fclose(stats_fp);
                    unlink(stats_file);
                    return -1;
                }
                values = new_values;
            }
            values[count++] = all_value;
        }
    }

    fclose(stats_fp);
    unlink(stats_file);

    /* Calculate statistics */
    if (count == 0) {
        free(values);
        fprintf(stderr, "compute_ssim: no SSIM values collected from stats file\n");
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

