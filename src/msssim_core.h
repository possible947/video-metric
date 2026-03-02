#ifndef MSSSIM_CORE_H
#define MSSSIM_CORE_H

#include <stdint.h>

/**
 * MS-SSIM (Multi-Scale SSIM) Core Algorithm
 * Based on Wang et al. (2003)
 *
 * Threading model:
 *   - Compile with -DMSSSIM_SINGLE_THREAD to disable all parallelism.
 *   - When built with -fopenmp (_OPENMP defined), OpenMP is used.
 *   - Otherwise, POSIX pthreads are used (works on Linux and macOS).
 *   - Number of threads: set via msssim_set_num_threads(), or the
 *     MSSSIM_THREADS environment variable, or auto-detected from CPU count.
 */

/**
 * Pyramid level structure
 */
typedef struct {
    float *data;
    int width;
    int height;
} PyramidLevel;

/**
 * Set the number of worker threads used by MS-SSIM computation.
 *
 * @param n  Number of threads (0 = auto-detect from MSSSIM_THREADS env
 *           variable or CPU count; 1 = single-threaded).
 *           Has no effect when built with -DMSSSIM_SINGLE_THREAD.
 */
void msssim_set_num_threads(int n);

/**
 * Compute MS-SSIM between two Y-plane images (float input, range [0..1]).
 *
 * This implements the Multi-Scale SSIM algorithm as described in:
 * "Multi-scale structural similarity for image quality assessment"
 * by Z. Wang, E. P. Simoncelli and A. C. Bovik, 2003
 *
 * Algorithm:
 * 1. Build 5-level Gaussian pyramids (downsample by 2x each level)
 * 2. Compute SSIM at each scale with 11x11 Gaussian window
 * 3. Combine using weights: {0.0448, 0.2856, 0.3001, 0.2363, 0.1333}
 *
 * @param ref_y    Reference Y-plane in float [0..1]
 * @param dist_y   Distorted Y-plane in float [0..1]
 * @param width    Image width (must be >= 176 for 5 levels)
 * @param height   Image height (must be >= 144 for 5 levels)
 * @return         MS-SSIM score [0..1], or -1.0 on error
 */
double compute_msssim_float(
    const float *ref_y,
    const float *dist_y,
    int width,
    int height
);

/**
 * Compute MS-SSIM between two Y-plane images (16-bit integer input).
 *
 * Values are normalised to [0..1] using max_value before computation.
 *
 * @param ref_y      Reference Y-plane in uint16_t
 * @param dist_y     Distorted Y-plane in uint16_t
 * @param width      Image width (must be >= 176 for 5 levels)
 * @param height     Image height (must be >= 144 for 5 levels)
 * @param max_value  Maximum pixel value (255 for 8-bit, 1023 for 10-bit, etc.)
 * @return           MS-SSIM score [0..1], or -1.0 on error
 */
double compute_msssim_uint16(
    const uint16_t *ref_y,
    const uint16_t *dist_y,
    int width,
    int height,
    int max_value
);

/**
 * Compute MS-SSIM between two Y-plane images (float input).
 * Alias for compute_msssim_float(); kept for backward compatibility.
 */
double compute_msssim_frame(
    const float *ref_y,
    const float *dist_y,
    int width,
    int height
);

#endif /* MSSSIM_CORE_H */
