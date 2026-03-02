/*====================================================================*/
/*  FILE: src/env_check.c                                            */
/*====================================================================*/
/**
 * @brief  Runtime environment verification for *video_metric*.
 *
 * The routine checks that the local FFmpeg binary exists, that it
 * supports the required filters, that the VMAF model files are
 * present, and that a minimal VMAF run succeeds.  On success it
 * writes a short status string into the caller supplied buffer
 * and returns 0; otherwise it returns -1.
 *
 * All ffmpeg output is suppressed to avoid polluting the console.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>     /* access, readlink, fork, dup2 */
#include <sys/stat.h>   /* stat */
#include <limits.h>     /* PATH_MAX */
#include <errno.h>
#include <sys/wait.h>   /* waitpid, WIFEXITED, WEXITSTATUS */
#include <fcntl.h>      /* open, O_WRONLY */
#include "path_util.h"   /* get_executable_dir */

/* ------------------------------------------------------------------ */
/*  Helper: check if a file exists and is executable                  */
/* ------------------------------------------------------------------ */
static int is_executable(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return 0;
    return (S_ISREG(st.st_mode) && (st.st_mode & S_IXUSR));
}

/* ------------------------------------------------------------------ */
/*  Helper: check if a regular file exists                            */
/* ------------------------------------------------------------------ */
static int file_exists(const char *path)
{
    return (access(path, F_OK) == 0);
}

/* ------------------------------------------------------------------ */
/*  Helper: run a shell command quietly and return exit status        */
/*  Uses fork/dup2 to suppress output at file descriptor level.       */
/* ------------------------------------------------------------------ */
static int run_cmd_quiet(const char *cmd)
{
    pid_t pid = fork();
    if (pid == -1)
        return -1;

    if (pid == 0) {
        /* Child process: suppress stdout and stderr */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull == -1)
            exit(127);

        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        close(devnull);

        /* Execute the command via shell */
        execl("/bin/sh", "sh", "-c", cmd, NULL);
        exit(127); /* if execl fails */
    } else {
        /* Parent process: wait for child to complete */
        int status;
        if (waitpid(pid, &status, 0) == -1)
            return -1;

        if (WIFEXITED(status))
            return WEXITSTATUS(status);

        return -1;
    }
}

/* ------------------------------------------------------------------ */
/*  Main API: check_environment                                       */
/* ------------------------------------------------------------------ */
int check_environment(char *status_buf, size_t bufsize)
{
    char exec_dir[PATH_MAX];
    if (get_executable_dir(exec_dir, sizeof(exec_dir)) != 0) {
        snprintf(status_buf, bufsize,
                 "ffmpeg: fail, ssim: fail, vmaf: fail");
        return -1;
    }

    /* ffmpeg binary is expected to live in the same directory */
    char ffmpeg_path[PATH_MAX];
    snprintf(ffmpeg_path, sizeof(ffmpeg_path), "%s/ffmpeg", exec_dir);

    /* model files */
    char model_hd[PATH_MAX];
    char model_4k[PATH_MAX];
    snprintf(model_hd, sizeof(model_hd), "%s/model/vmaf_v0.6.1.json", exec_dir);
    snprintf(model_4k, sizeof(model_4k), "%s/model/vmaf_4k_v0.6.1.json", exec_dir);

    /* -------------------------------------------------------------- */
    /*  Perform checks                                                */
    /* -------------------------------------------------------------- */
    int ok_ffmpeg     = is_executable(ffmpeg_path);
    int ok_ssim       = 0;
    int ok_model_hd   = file_exists(model_hd);
    int ok_model_4k   = file_exists(model_4k);
    int ok_test_vmaf  = 0;

    if (ok_ffmpeg) {
        /* Check SSIM filter */
        char cmd_ssim[4096];
        snprintf(cmd_ssim, sizeof(cmd_ssim),
                 "\"%s\" -filters | grep -q ssim", ffmpeg_path);
        ok_ssim = (run_cmd_quiet(cmd_ssim) == 0);

        /* Minimal VMAF test run */
        char cmd_test[4096];
        snprintf(cmd_test, sizeof(cmd_test),
                 "\"%s\" -f lavfi -i testsrc=duration=1:size=128x128:rate=30 "
                 "-f lavfi -i testsrc=duration=1:size=128x128:rate=30 "
                 "-filter_complex \"[0:v][1:v]libvmaf=log_path=/dev/null\" "
                 "-f null -",
                 ffmpeg_path);

        ok_test_vmaf = (run_cmd_quiet(cmd_test) == 0);
    }

    /* -------------------------------------------------------------- */
    /*  Build status string                                           */
    /* -------------------------------------------------------------- */
    snprintf(status_buf, bufsize,
             "ffmpeg: %s, ssim: %s, vmaf: %s",
             ok_ffmpeg ? "ok" : "fail",
             ok_ssim   ? "ok" : "fail",
             ok_test_vmaf ? "ok" : "fail");

    /* -------------------------------------------------------------- */
    /*  Return overall success                                        */
    /* -------------------------------------------------------------- */
    if (ok_ffmpeg && ok_ssim && ok_test_vmaf &&
        ok_model_hd && ok_model_4k)
        return 0;

    return -1;
}

