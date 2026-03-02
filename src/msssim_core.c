/*====================================================================*/
/*  FILE: src/msssim_core.c                                           */
/*====================================================================*/
/**
 * @file   msssim_core.c
 * @brief  MS-SSIM (Multi-Scale SSIM) core algorithm implementation
 *
 * Based on: "Multi-scale structural similarity for image quality assessment"
 * Z. Wang, E. P. Simoncelli and A. C. Bovik, 2003
 *
 * Threading:
 *   - Build with -DMSSSIM_SINGLE_THREAD to disable all parallelism.
 *   - Build with -fopenmp to use OpenMP (Linux: gcc/clang; macOS: Homebrew
 *     llvm or gcc).  The compiler sets _OPENMP automatically.
 *   - Otherwise POSIX pthreads are used (works with system clang on macOS
 *     and with gcc/clang on Linux).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#include "msssim_core.h"

/* ------------------------------------------------------------------ */
/*  Threading configuration                                           */
/* ------------------------------------------------------------------ */

#ifndef MSSSIM_SINGLE_THREAD
#  ifdef _OPENMP
#    include <omp.h>
#  else  /* pthreads */
#    include <pthread.h>
#    include <unistd.h>
#  endif
#endif /* !MSSSIM_SINGLE_THREAD */

/* Maximum number of threads supported in pthreads mode */
#define MSSSIM_MAX_THREADS 64

/* Global thread count (0 = auto-detect) */
static int g_num_threads = 0;

void msssim_set_num_threads(int n)
{
    g_num_threads = (n > 0) ? n : 0;
}

#ifndef MSSSIM_SINGLE_THREAD
static int msssim_get_num_threads(void)
{
#if defined(_OPENMP)
    if (g_num_threads > 0)
        return g_num_threads;
    return omp_get_max_threads();
#else  /* pthreads */
    if (g_num_threads > 0)
        return g_num_threads;
    /* Check environment variable */
    const char *env = getenv("MSSSIM_THREADS");
    if (env) {
        int n = atoi(env);
        if (n > 0)
            return n;
    }
    /* Auto-detect from CPU count */
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return (n > 1) ? (int)n : 1;
#endif
}
#endif /* !MSSSIM_SINGLE_THREAD */

/* ------------------------------------------------------------------ */
/*  Constants                                                         */
/* ------------------------------------------------------------------ */

/* Number of pyramid levels */
#define NUM_LEVELS 5

/* MS-SSIM weights (from Wang et al. 2003) */
static const double MSSSIM_WEIGHTS[NUM_LEVELS] = {
    0.0448, 0.2856, 0.3001, 0.2363, 0.1333
};

/* SSIM constants */
#define C1 (0.01 * 0.01)  /* (K1*L)^2, L=1 for [0..1] range */
#define C2 (0.03 * 0.03)  /* (K2*L)^2 */

/* Gaussian kernel size */
#define KERNEL_SIZE 11
#define KERNEL_RADIUS (KERNEL_SIZE / 2)

/* Gaussian kernel sigma=1.5 (normalized) */
static const float GAUSSIAN_KERNEL[KERNEL_SIZE] = {
    0.0102f, 0.0278f, 0.0656f, 0.1353f, 0.2421f,
    0.3679f,
    0.2421f, 0.1353f, 0.0656f, 0.0278f, 0.0102f
};

/* ------------------------------------------------------------------ */
/*  Row-range blur helpers (used by both serial and parallel paths)   */
/* ------------------------------------------------------------------ */

/* Process rows [y0, y1) of a horizontal Gaussian blur: src -> dst */
static void blur_h_rows(
    const float *src, float *dst,
    int width, int height,
    int y0, int y1)
{
    (void)height;  /* height not used in horizontal pass; kept for API symmetry */
    for (int y = y0; y < y1; y++) {
        for (int x = 0; x < width; x++) {
            float sum = 0.0f, norm = 0.0f;
            for (int k = 0; k < KERNEL_SIZE; k++) {
                int kx = x + k - KERNEL_RADIUS;
                if (kx < 0) kx = 0;
                if (kx >= width) kx = width - 1;
                sum += src[y * width + kx] * GAUSSIAN_KERNEL[k];
                norm += GAUSSIAN_KERNEL[k];
            }
            dst[y * width + x] = sum / norm;
        }
    }
}

/* Process rows [y0, y1) of a vertical Gaussian blur: src -> dst */
static void blur_v_rows(
    const float *src, float *dst,
    int width, int height,
    int y0, int y1)
{
    for (int y = y0; y < y1; y++) {
        for (int x = 0; x < width; x++) {
            float sum = 0.0f, norm = 0.0f;
            for (int k = 0; k < KERNEL_SIZE; k++) {
                int ky = y + k - KERNEL_RADIUS;
                if (ky < 0) ky = 0;
                if (ky >= height) ky = height - 1;
                sum += src[ky * width + x] * GAUSSIAN_KERNEL[k];
                norm += GAUSSIAN_KERNEL[k];
            }
            dst[y * width + x] = sum / norm;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  pthreads worker and dispatcher                                    */
/* ------------------------------------------------------------------ */

#if !defined(MSSSIM_SINGLE_THREAD) && !defined(_OPENMP)

typedef struct {
    const float *src;
    float *dst;
    int width, height;
    int horizontal;  /* 1 = horizontal pass, 0 = vertical pass */
    int y0, y1;
} BlurTask;

static void *blur_pthread_worker(void *arg)
{
    BlurTask *t = (BlurTask *)arg;
    if (t->horizontal)
        blur_h_rows(t->src, t->dst, t->width, t->height, t->y0, t->y1);
    else
        blur_v_rows(t->src, t->dst, t->width, t->height, t->y0, t->y1);
    return NULL;
}

/*
 * Run blur_h_rows / blur_v_rows in parallel over 'height' rows.
 * Falls back to serial on thread-creation failure.
 */
static void blur_1d_pthreads(
    const float *src, float *dst,
    int width, int height, int horizontal)
{
    int nt = msssim_get_num_threads();
    if (nt <= 1 || height < 2) {
        if (horizontal)
            blur_h_rows(src, dst, width, height, 0, height);
        else
            blur_v_rows(src, dst, width, height, 0, height);
        return;
    }

    int actual = nt;
    if (actual > MSSSIM_MAX_THREADS) actual = MSSSIM_MAX_THREADS;
    if (actual > height)             actual = height;

    /* Pre-initialise all task descriptors */
    BlurTask   args[MSSSIM_MAX_THREADS];
    pthread_t  threads[MSSSIM_MAX_THREADS];

    for (int t = 0; t < actual; t++) {
        args[t].src        = src;
        args[t].dst        = dst;
        args[t].width      = width;
        args[t].height     = height;
        args[t].horizontal = horizontal;
        args[t].y0         = t       * height / actual;
        args[t].y1         = (t + 1) * height / actual;
    }

    int created = 0;
    for (int t = 0; t < actual; t++) {
        if (pthread_create(&threads[created], NULL,
                           blur_pthread_worker, &args[t]) == 0) {
            created++;
        } else {
            /* Join already-created threads, then finish serially */
            for (int j = 0; j < created; j++)
                pthread_join(threads[j], NULL);
            for (int j = t; j < actual; j++)
                blur_pthread_worker(&args[j]);
            return;
        }
    }
    for (int t = 0; t < created; t++)
        pthread_join(threads[t], NULL);
}

#endif /* !MSSSIM_SINGLE_THREAD && !_OPENMP */

/* ------------------------------------------------------------------ */
/*  Helper: Apply 1D Gaussian blur (horizontal or vertical)           */
/* ------------------------------------------------------------------ */
static void gaussian_blur_1d(
    const float *src,
    float *dst,
    int width,
    int height,
    int horizontal
)
{
#if defined(MSSSIM_SINGLE_THREAD)
    if (horizontal)
        blur_h_rows(src, dst, width, height, 0, height);
    else
        blur_v_rows(src, dst, width, height, 0, height);

#elif defined(_OPENMP)
    int nt = msssim_get_num_threads();
    if (horizontal) {
#pragma omp parallel for schedule(static) num_threads(nt)
        for (int y = 0; y < height; y++)
            blur_h_rows(src, dst, width, height, y, y + 1);
    } else {
#pragma omp parallel for schedule(static) num_threads(nt)
        for (int y = 0; y < height; y++)
            blur_v_rows(src, dst, width, height, y, y + 1);
    }

#else  /* pthreads */
    blur_1d_pthreads(src, dst, width, height, horizontal);
#endif
}

/* ------------------------------------------------------------------ */
/*  Helper: Apply separable Gaussian blur (11x11)                     */
/* ------------------------------------------------------------------ */
static void gaussian_blur_separable(
    const float *src,
    float *dst,
    int width,
    int height
)
{
    /* Allocate temporary buffer */
    float *temp = malloc(width * height * sizeof(float));
    if (!temp) {
        /* Fallback: copy source to destination */
        memcpy(dst, src, width * height * sizeof(float));
        return;
    }

    /* Horizontal pass: src -> temp */
    gaussian_blur_1d(src, temp, width, height, 1);

    /* Vertical pass: temp -> dst */
    gaussian_blur_1d(temp, dst, width, height, 0);

    free(temp);
}

/* ------------------------------------------------------------------ */
/*  Helper: Downsample by 2x (average pooling)                        */
/* ------------------------------------------------------------------ */
static void downsample_2x(
    const float *src,
    float *dst,
    int src_width,
    int src_height
)
{
    int dst_width  = src_width  / 2;
    int dst_height = src_height / 2;

    for (int y = 0; y < dst_height; y++) {
        for (int x = 0; x < dst_width; x++) {
            int sx = x * 2;
            int sy = y * 2;

            /* Average 2x2 block */
            float sum = src[sy       * src_width + sx]     +
                        src[sy       * src_width + (sx + 1)] +
                        src[(sy + 1) * src_width + sx]     +
                        src[(sy + 1) * src_width + (sx + 1)];

            dst[y * dst_width + x] = sum * 0.25f;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Helper: Compute local statistics with Gaussian window             */
/* ------------------------------------------------------------------ */
static void compute_local_stats(
    const float *img,
    int width,
    int height,
    float *mean,
    float *variance
)
{
    /* Apply Gaussian blur to get local mean */
    gaussian_blur_separable(img, mean, width, height);

    /* Compute img^2 and apply Gaussian blur */
    int n = width * height;
    float *img_sq  = NULL;
    float *mean_sq = NULL;
    img_sq  = (float *)malloc((size_t)n * sizeof(float));
    mean_sq = (float *)malloc((size_t)n * sizeof(float));
    if (!img_sq || !mean_sq) {
        free(img_sq);
        free(mean_sq);
        return;
    }

    for (int i = 0; i < n; i++) {
        img_sq[i] = img[i] * img[i];
    }

    /* Suppress GCC false-positive "may be used uninitialized" for img_sq.
     * After the malloc+NULL guard above, img_sq is guaranteed non-NULL. */
#if defined(__GNUC__) && !defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
    gaussian_blur_separable(img_sq, mean_sq, width, height);
#if defined(__GNUC__) && !defined(__clang__)
#  pragma GCC diagnostic pop
#endif

    /* Variance = E[X^2] - E[X]^2 */
    for (int i = 0; i < width * height; i++) {
        variance[i] = mean_sq[i] - mean[i] * mean[i];
        /* Ensure non-negative */
        if (variance[i] < 0.0f) variance[i] = 0.0f;
    }

    free(img_sq);
    free(mean_sq);
}

/* ------------------------------------------------------------------ */
/*  Compute SSIM at single scale                                      */
/* ------------------------------------------------------------------ */
static double ssim_single_scale(
    const float *ref,
    const float *dist,
    int width,
    int height
)
{
    int size = width * height;

    /* Allocate buffers for statistics */
    float *mean_ref  = malloc(size * sizeof(float));
    float *mean_dist = malloc(size * sizeof(float));
    float *var_ref   = malloc(size * sizeof(float));
    float *var_dist  = malloc(size * sizeof(float));

    if (!mean_ref || !mean_dist || !var_ref || !var_dist) {
        free(mean_ref);
        free(mean_dist);
        free(var_ref);
        free(var_dist);
        return 0.0;
    }

    /* Compute local statistics */
    compute_local_stats(ref,  width, height, mean_ref,  var_ref);
    compute_local_stats(dist, width, height, mean_dist, var_dist);

    /* Compute covariance: E[XY] - E[X]E[Y] */
    float *ref_dist     = malloc(size * sizeof(float));
    float *cov          = malloc(size * sizeof(float));
    float *mean_ref_dist = malloc(size * sizeof(float));

    if (!ref_dist || !cov || !mean_ref_dist) {
        free(mean_ref);
        free(mean_dist);
        free(var_ref);
        free(var_dist);
        free(ref_dist);
        free(cov);
        free(mean_ref_dist);
        return 0.0;
    }

    for (int i = 0; i < size; i++) {
        ref_dist[i] = ref[i] * dist[i];
    }

    gaussian_blur_separable(ref_dist, mean_ref_dist, width, height);

    for (int i = 0; i < size; i++) {
        cov[i] = mean_ref_dist[i] - mean_ref[i] * mean_dist[i];
    }

    /* Compute SSIM map and accumulate mean */
    double ssim_sum = 0.0;

    for (int i = 0; i < size; i++) {
        float luminance = (2.0f * mean_ref[i] * mean_dist[i] + C1) /
                          (mean_ref[i] * mean_ref[i] + mean_dist[i] * mean_dist[i] + C1);

        float contrast_structure = (2.0f * cov[i] + C2) /
                                   (var_ref[i] + var_dist[i] + C2);

        ssim_sum += luminance * contrast_structure;
    }

    /* Cleanup */
    free(mean_ref);
    free(mean_dist);
    free(var_ref);
    free(var_dist);
    free(ref_dist);
    free(cov);
    free(mean_ref_dist);

    return (size > 0) ? (ssim_sum / size) : 0.0;
}

/* ------------------------------------------------------------------ */
/*  Build Gaussian pyramid                                            */
/* ------------------------------------------------------------------ */
static int build_pyramid(
    const float *base,
    int width,
    int height,
    PyramidLevel levels[NUM_LEVELS]
)
{
    /* Level 0: original image (blurred) */
    levels[0].width  = width;
    levels[0].height = height;
    levels[0].data   = malloc(width * height * sizeof(float));
    if (!levels[0].data) return -1;

    gaussian_blur_separable(base, levels[0].data, width, height);

    /* Subsequent levels: downsample */
    for (int L = 1; L < NUM_LEVELS; L++) {
        int prev_w = levels[L - 1].width;
        int prev_h = levels[L - 1].height;

        levels[L].width  = prev_w / 2;
        levels[L].height = prev_h / 2;

        /* Check minimum size */
        if (levels[L].width < KERNEL_SIZE || levels[L].height < KERNEL_SIZE) {
            for (int k = 0; k < L; k++) {
                free(levels[k].data);
            }
            return -1;
        }

        levels[L].data = malloc(levels[L].width * levels[L].height * sizeof(float));
        if (!levels[L].data) {
            for (int k = 0; k < L; k++) {
                free(levels[k].data);
            }
            return -1;
        }

        downsample_2x(levels[L - 1].data, levels[L].data, prev_w, prev_h);
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Free pyramid                                                      */
/* ------------------------------------------------------------------ */
static void free_pyramid(PyramidLevel levels[NUM_LEVELS])
{
    for (int i = 0; i < NUM_LEVELS; i++) {
        if (levels[i].data) {
            free(levels[i].data);
            levels[i].data = NULL;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Compute MS-SSIM (float input, range [0..1])                       */
/* ------------------------------------------------------------------ */
double compute_msssim_float(
    const float *ref_y,
    const float *dist_y,
    int width,
    int height
)
{
    if (!ref_y || !dist_y) {
        fprintf(stderr, "compute_msssim_float: NULL input\n");
        return -1.0;
    }

    /* Check minimum dimensions for 5-level pyramid */
    int min_dim = KERNEL_SIZE * (1 << (NUM_LEVELS - 1));
    if (width < min_dim || height < min_dim) {
        fprintf(stderr, "compute_msssim_float: image too small (%dx%d, min %dx%d)\n",
                width, height, min_dim, min_dim);
        return -1.0;
    }

    /* Build pyramids */
    PyramidLevel ref_pyramid[NUM_LEVELS];
    PyramidLevel dist_pyramid[NUM_LEVELS];

    memset(ref_pyramid,  0, sizeof(ref_pyramid));
    memset(dist_pyramid, 0, sizeof(dist_pyramid));

    if (build_pyramid(ref_y, width, height, ref_pyramid) != 0) {
        fprintf(stderr, "compute_msssim_float: failed to build reference pyramid\n");
        return -1.0;
    }

    if (build_pyramid(dist_y, width, height, dist_pyramid) != 0) {
        fprintf(stderr, "compute_msssim_float: failed to build distorted pyramid\n");
        free_pyramid(ref_pyramid);
        return -1.0;
    }

    /* Compute SSIM at each scale and combine with weights */
    double msssim = 1.0;

    for (int L = 0; L < NUM_LEVELS; L++) {
        double ssim_l = ssim_single_scale(
            ref_pyramid[L].data,
            dist_pyramid[L].data,
            ref_pyramid[L].width,
            ref_pyramid[L].height
        );

        msssim *= pow(ssim_l, MSSSIM_WEIGHTS[L]);
    }

    /* Cleanup */
    free_pyramid(ref_pyramid);
    free_pyramid(dist_pyramid);

    return msssim;
}

/* ------------------------------------------------------------------ */
/*  Compute MS-SSIM (uint16 input)                                    */
/* ------------------------------------------------------------------ */
double compute_msssim_uint16(
    const uint16_t *ref_y,
    const uint16_t *dist_y,
    int width,
    int height,
    int max_value
)
{
    if (!ref_y || !dist_y) {
        fprintf(stderr, "compute_msssim_uint16: NULL input\n");
        return -1.0;
    }
    if (max_value <= 0) {
        fprintf(stderr, "compute_msssim_uint16: invalid max_value %d\n", max_value);
        return -1.0;
    }

    int size = width * height;
    float scale = 1.0f / (float)max_value;

    float *ref_f  = malloc(size * sizeof(float));
    float *dist_f = malloc(size * sizeof(float));

    if (!ref_f || !dist_f) {
        free(ref_f);
        free(dist_f);
        fprintf(stderr, "compute_msssim_uint16: allocation failed\n");
        return -1.0;
    }

    for (int i = 0; i < size; i++) {
        ref_f[i]  = (float)ref_y[i]  * scale;
        dist_f[i] = (float)dist_y[i] * scale;
    }

    double result = compute_msssim_float(ref_f, dist_f, width, height);

    free(ref_f);
    free(dist_f);
    return result;
}

/* ------------------------------------------------------------------ */
/*  Backward-compatible alias                                         */
/* ------------------------------------------------------------------ */
double compute_msssim_frame(
    const float *ref_y,
    const float *dist_y,
    int width,
    int height
)
{
    return compute_msssim_float(ref_y, dist_y, width, height);
}
