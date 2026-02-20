/*====================================================================*/
/*  FILE: src/vmaf.c                                                  */
/*====================================================================*/
/**
 * @file   vmaf.c
 * @brief  Compute the VMAF metric between two videos using ffmpeg.
 *
 * Requirements:
 *  - Use ONLY local ffmpeg (./ffmpeg), no system ffmpeg from $PATH.
 *  - Use filter_complex "[0:v][1:v]libvmaf=...".
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
#include <unistd.h>
#include <math.h>
#include <ctype.h>
#include <sys/wait.h>
#include <fcntl.h>

#include "path_util.h"
#include "vmaf.h"

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
/*  Compute VMAF                                                      */
/* ------------------------------------------------------------------ */
double compute_vmaf(const char *orig,
                    const char *test,
                    const char *model_path,
                    int         threads,
                    char       *json_path,
                    size_t      json_bufsize)
{
    if (!orig || !test || !model_path || !json_path || json_bufsize == 0) {
        fprintf(stderr, "compute_vmaf: invalid argument(s)\n");
        return NAN;
    }

    const char *ffmpeg = get_ffmpeg_path();
    if (!ffmpeg) {
        fprintf(stderr, "compute_vmaf: local ffmpeg not found\n");
        return NAN;
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
        return NAN;
    }
    strncpy(json_path, log_file, json_bufsize);
    json_path[json_bufsize - 1] = '\0';

    /* Escape paths */
    char esc_orig[PATH_MAX];
    char esc_test[PATH_MAX];
    char esc_model[PATH_MAX];

    escape_path(orig,       esc_orig,  sizeof(esc_orig));
    escape_path(test,       esc_test,  sizeof(esc_test));
    escape_path(model_path, esc_model, sizeof(esc_model));

    /* Normalize threads for libvmaf */
    int vmaf_threads = (threads > 0) ? threads : 1;

    /* Build ffmpeg command */
    char cmd[4096];
    int n = snprintf(
        cmd, sizeof(cmd),
        "%s -i %s -i %s "
        "-filter_complex \"[0:v][1:v]libvmaf=model=path=%s:n_threads=%d:log_path=%s\" "
        "-f null - 2>&1",
        ffmpeg,
        esc_orig,
        esc_test,
        esc_model,
        vmaf_threads,
        json_path
    );

    if (n < 0 || (size_t)n >= sizeof(cmd)) {
        fprintf(stderr, "compute_vmaf: command buffer overflow\n");
        return NAN;
    }

    /* Fork and execute ffmpeg with pipe to read output */
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return NAN;
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        close(pipefd[0]);
        close(pipefd[1]);
        return NAN;
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

    double score = NAN;
    char line[2048];
    FILE *fp = fdopen(pipefd[0], "r");
    if (!fp) {
        perror("fdopen");
        close(pipefd[0]);
        waitpid(pid, NULL, 0);
        return NAN;
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

        /* Look for VMAF result: "VMAF score: <value>" */
        char *p = strstr(line, "VMAF score:");
        if (!p)
            continue;

        p += strlen("VMAF score:");
        while (*p && isspace((unsigned char)*p))
            p++;

        errno = 0;
        double v = strtod(p, NULL);
        if (errno == 0)
            score = v;

        break; /* first occurrence is enough */
    }

    fclose(fp);

    int status;
    waitpid(pid, &status, 0);

    /* Clear progress bar and print newline */
    if (total_duration > 0.0) {
        printf("\n");
    }

    if (status == -1) {
        score = NAN;
    } else if (WIFEXITED(status)) {
        if (WEXITSTATUS(status) != 0)
            score = NAN;
    } else {
        score = NAN;
    }

    if (isnan(score))
        fprintf(stderr, "compute_vmaf: failed to obtain VMAF score\n");

    return score;
}

