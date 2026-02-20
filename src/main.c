/*====================================================================*/
/*  FILE: src/main.c                                                 */
/*====================================================================*/
/**
 * @brief  Entry point for the *video_metric* utility.
 *
 * This file implements the orchestrator that:
 *   1. Parses the command‑line options into a `struct cli_options`.
 *   2. Checks that at least one metric flag is set.
 *   3. Performs a runtime environment check.
 *   4. Verifies that the original and test files exist.
 *   5. Calls the metric calculators (`compute_ssim` and/or
 *      `compute_vmaf`).
 *   6. Prints a status line followed by the requested metric results.
 *
 * The code is intentionally straightforward – it focuses on clarity
 * and error handling rather than on performance micro‑optimisations.
 *
 * Build (example):
 *   gcc -std=gnu11 -Wall -Wextra -O2 src/ *.c -o video_metric
 *
 * Author:  OpenAI ChatGPT
 * Date:    2026‑02‑13
 */

#include "cli_options.h"
#include "cli_parser.h"   /* parse_cli() prototype */
#include "env_check.h"    /* check_environment() */
#include "ssim.h"         /* compute_ssim() */
#include "vmaf.h"         /* compute_vmaf() */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/*  Helper: test if a file exists and is readable                      */
/* ------------------------------------------------------------------ */
static int file_exists(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

/* ------------------------------------------------------------------ */
/*  Helper: print a simple usage message                              */
/* ------------------------------------------------------------------ */
static void print_usage(const char *progname)
{
    fprintf(stderr,
            "Usage: %s -o <orig> -t <test> [options]\n"
            "Options:\n"
            "  -o, --output <file>      Path to the original video (required)\n"
            "  -t, --test <file>        Path to the test video (required)\n"
            "  -s, --ssim               Compute SSIM (optional)\n"
            "  -v, --vmaf               Compute VMAF (optional)\n"
            "  -r, --resolution <hd|4k> Target resolution (default: hd)\n"
            "  -n, --num_threads <int>  Number of worker threads (default: 0)\n",
            progname);
}

/* ------------------------------------------------------------------ */
/*  Main program                                                    */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[])
{
    struct cli_options opts;
    int rc = parse_cli(argc, argv, &opts);
    if (rc != 0) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (!opts.compute_ssim && !opts.compute_vmaf) {
        fprintf(stderr,
                "Error: at least one metric flag must be set "
                "(-s/--ssim or -v/--vmaf)\n");
        return EXIT_FAILURE;
    }

    /* ------------------------------------------------------------------ */
    /*  Environment check                                                */
    /* ------------------------------------------------------------------ */
    char status_buf[256];
    rc = check_environment(status_buf, sizeof(status_buf));
    if (rc != 0) {
        /* Environment is incomplete – report and exit */
        printf("%s\n", status_buf);
        return EXIT_FAILURE;
    }
    printf("%s\n", status_buf);

    /* ------------------------------------------------------------------ */
    /*  Validate input files                                            */
    /* ------------------------------------------------------------------ */
    if (!file_exists(opts.orig) || !file_exists(opts.test)) {
        fprintf(stderr,
                "Error: input file(s) not found or unreadable\n");
        return EXIT_FAILURE;
    }

    /* ------------------------------------------------------------------ */
    /*  Compute metrics                                                 */
    /* ------------------------------------------------------------------ */
    double ssim_val = NAN;
    if (opts.compute_ssim) {
        printf("Computing SSIM...\n");
        fflush(stdout);
        ssim_val = compute_ssim(opts.orig, opts.test, opts.num_threads);
        if (isnan(ssim_val)) {
            fprintf(stderr,
                    "Error: SSIM computation failed\n");
            return EXIT_FAILURE;
        }
        printf("SSIM: %.6f\n", ssim_val);
    }

    double vmaf_val = NAN;
    if (opts.compute_vmaf) {
        printf("Computing VMAF...\n");
        fflush(stdout);
        /* Resolve the executable directory to locate the model files */
        char exec_dir[PATH_MAX];
        ssize_t n = readlink("/proc/self/exe", exec_dir, sizeof(exec_dir) - 1);
        if (n == -1) {
            fprintf(stderr,
                    "Error: cannot determine executable path: %s\n",
                    strerror(errno));
            return EXIT_FAILURE;
        }
        exec_dir[n] = '\0';
        /* Strip executable name */
        char *slash = strrchr(exec_dir, '/');
        if (slash) *slash = '\0';

        char model_file[PATH_MAX];
        if (strcmp(opts.resolution, "hd") == 0) {
            snprintf(model_file, sizeof(model_file),
                     "%s/model/vmaf_v0.6.1.json", exec_dir);
        } else if (strcmp(opts.resolution, "4k") == 0) {
            snprintf(model_file, sizeof(model_file),
                     "%s/model/vmaf_4k_v0.6.1.json", exec_dir);
        } else {
            fprintf(stderr,
                    "Error: unsupported resolution '%s'\n",
                    opts.resolution);
            return EXIT_FAILURE;
        }

        /* Compute VMAF – let compute_vmaf create & delete its own log */
        char json_path[PATH_MAX];
        vmaf_val = compute_vmaf(opts.orig, opts.test, model_file,
                                opts.num_threads, json_path, sizeof(json_path));
        if (isnan(vmaf_val)) {
            fprintf(stderr,
                    "Error: VMAF computation failed\n");
            return EXIT_FAILURE;
        }
        printf("VMAF: %.6f\n", vmaf_val);
    }

    /* ------------------------------------------------------------------ */
    /*  Clean up allocated strings                                       */
    /* ------------------------------------------------------------------ */
    free(opts.orig);
    free(opts.test);
    free(opts.resolution);

    return EXIT_SUCCESS;
}

