/*====================================================================*/
/*  FILE: src/cli_parser.c                                          */
/*====================================================================*/
/**
 * @brief  Command‑line parser for the *video_metric* utility.
 *
 * This file implements a small, self‑contained parser that translates
 * the raw `argc/argv` array into a populated `struct cli_options`
 * instance.  The implementation follows the specification given in the
 * project documentation.
 *
 * The parser is intentionally simple – it does not attempt to support
 * arbitrary POSIX option syntax or long‑dash options that have arguments
 * after a space or an `=` sign.  The only supported forms are:
 *
 *   - Short flags:   `-s`, `-v`, `-o <file>`, `-t <file>`,
 *                     `-r hd|4k`, `-n <int>`
 *   - Combined flags: `-sv` or `-vs` are treated as `-s -v`
 *
 * Any unknown option causes the parser to emit an error message
 * to `stderr` and return `-1`.  The caller must free any strings
 * that were allocated before the error occurred.
 */

#include "cli_options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
/* ------------------------------------------------------------------ */
/*  Helper: duplicate a string, abort on failure                     */
/* ------------------------------------------------------------------ */
static char *dup_or_die(const char *s)
{
    char *p = strdup(s);
    if (!p) {
        fprintf(stderr, "Error: out of memory\n");
        exit(EXIT_FAILURE);
    }
    return p;
}

/* ------------------------------------------------------------------ */
/*  Helper: parse integer from a string; return -1 on failure        */
/* ------------------------------------------------------------------ */
static int parse_int(const char *s, int *out)
{
    char *end = NULL;
    long val = strtol(s, &end, 10);
    if (!s || *s == '\0' || *end != '\0' || val < 0 || val > INT_MAX) {
        return -1;
    }
    *out = (int)val;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Main API: parse_cli                                             */
/* ------------------------------------------------------------------ */
int parse_cli(int argc, char *argv[], struct cli_options *opts)
{
    /* Initialise defaults */
    opts->orig           = NULL;
    opts->test           = NULL;
    opts->compute_ssim   = 0;
    opts->compute_vmaf   = 0;
    opts->resolution     = dup_or_die("hd");   /* default */
    opts->num_threads    = 0;                  /* default */

    /* Helper flags to check required options */
    int got_orig = 0;
    int got_test = 0;

    for (int i = 1; i < argc; ++i) {
        char *arg = argv[i];

        /* Long option (starts with "--") */
        if (strncmp(arg, "--", 2) == 0) {
            if (strcmp(arg, "--output") == 0 ||
                strcmp(arg, "--orig")   == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "Error: missing value for %s\n", arg);
                    return -1;
                }
                if (opts->orig) free(opts->orig);
                opts->orig = dup_or_die(argv[++i]);
                got_orig = 1;
            }
            else if (strcmp(arg, "--test") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "Error: missing value for %s\n", arg);
                    return -1;
                }
                if (opts->test) free(opts->test);
                opts->test = dup_or_die(argv[++i]);
                got_test = 1;
            }
            else if (strcmp(arg, "--ssim") == 0) {
                opts->compute_ssim = 1;
            }
            else if (strcmp(arg, "--vmaf") == 0) {
                opts->compute_vmaf = 1;
            }
            else if (strcmp(arg, "--resolution") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "Error: missing value for %s\n", arg);
                    return -1;
                }
                const char *val = argv[++i];
                if (strcmp(val, "hd") != 0 && strcmp(val, "4k") != 0) {
                    fprintf(stderr, "Error: invalid resolution '%s'\n", val);
                    return -1;
                }
                free(opts->resolution);
                opts->resolution = dup_or_die(val);
            }
            else if (strcmp(arg, "--num_threads") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "Error: missing value for %s\n", arg);
                    return -1;
                }
                int n;
                if (parse_int(argv[++i], &n) != 0) {
                    fprintf(stderr, "Error: invalid number of threads '%s'\n",
                            argv[i]);
                    return -1;
                }
                opts->num_threads = n;
            }
            else {
                fprintf(stderr, "Error: unknown option '%s'\n", arg);
                return -1;
            }
        }
        /* Short option(s) (starts with single '-') */
        else if (arg[0] == '-' && arg[1] != '\0') {
            /* Handle combined flags: e.g. -sv, -vs */
            for (size_t j = 1; j < strlen(arg); ++j) {
                char opt = arg[j];
                switch (opt) {
                case 'o':   /* -o <file> */
                case 'O':
                    if (j + 1 < strlen(arg)) {
                        /* Treat '-ofoo' as '-o foo' */
                        if (opts->orig) free(opts->orig);
                        opts->orig = dup_or_die(arg + j + 1);
                        got_orig = 1;
                        j = strlen(arg); /* end loop */
                    }
                    else {
                        if (i + 1 >= argc) {
                            fprintf(stderr, "Error: missing value for -%c\n",
                                    opt);
                            return -1;
                        }
                        if (opts->orig) free(opts->orig);
                        opts->orig = dup_or_die(argv[++i]);
                        got_orig = 1;
                    }
                    break;

                case 't':   /* -t <file> */
                case 'T':
                    if (j + 1 < strlen(arg)) {
                        if (opts->test) free(opts->test);
                        opts->test = dup_or_die(arg + j + 1);
                        got_test = 1;
                        j = strlen(arg);
                    }
                    else {
                        if (i + 1 >= argc) {
                            fprintf(stderr, "Error: missing value for -%c\n",
                                    opt);
                            return -1;
                        }
                        if (opts->test) free(opts->test);
                        opts->test = dup_or_die(argv[++i]);
                        got_test = 1;
                    }
                    break;

                case 's':
                    opts->compute_ssim = 1;
                    break;

                case 'v':
                    opts->compute_vmaf = 1;
                    break;

                case 'r':   /* -r hd|4k */
                    if (j + 1 < strlen(arg)) {
                        /* e.g., -rhd */
                        const char *val = arg + j + 1;
                        if (strcmp(val, "hd") != 0 && strcmp(val, "4k") != 0) {
                            fprintf(stderr, "Error: invalid resolution '%s'\n",
                                    val);
                            return -1;
                        }
                        free(opts->resolution);
                        opts->resolution = dup_or_die(val);
                        j = strlen(arg);
                    }
                    else {
                        if (i + 1 >= argc) {
                            fprintf(stderr, "Error: missing value for -%c\n",
                                    opt);
                            return -1;
                        }
                        const char *val = argv[++i];
                        if (strcmp(val, "hd") != 0 && strcmp(val, "4k") != 0) {
                            fprintf(stderr, "Error: invalid resolution '%s'\n",
                                    val);
                            return -1;
                        }
                        free(opts->resolution);
                        opts->resolution = dup_or_die(val);
                    }
                    break;

                case 'n':   /* -n <int> */
                    if (j + 1 < strlen(arg)) {
                        /* e.g., -n4 */
                        int n;
                        if (parse_int(arg + j + 1, &n) != 0) {
                            fprintf(stderr,
                                    "Error: invalid number of threads '%s'\n",
                                    arg + j + 1);
                            return -1;
                        }
                        opts->num_threads = n;
                        j = strlen(arg);
                    }
                    else {
                        if (i + 1 >= argc) {
                            fprintf(stderr, "Error: missing value for -%c\n",
                                    opt);
                            return -1;
                        }
                        int n;
                        if (parse_int(argv[++i], &n) != 0) {
                            fprintf(stderr,
                                    "Error: invalid number of threads '%s'\n",
                                    argv[i]);
                            return -1;
                        }
                        opts->num_threads = n;
                    }
                    break;

                default:
                    fprintf(stderr, "Error: unknown option '-%c'\n", opt);
                    return -1;
                }
            }
        }
        else {
            /* Positional arguments are not allowed in this tool */
            fprintf(stderr, "Error: unexpected positional argument '%s'\n",
                    arg);
            return -1;
        }
    }

    /* Verify required arguments */
    if (!got_orig) {
        fprintf(stderr, "Error: missing required option --output/-o\n");
        return -1;
    }
    if (!got_test) {
        fprintf(stderr, "Error: missing required option --test/-t\n");
        return -1;
    }

    return 0;
}

