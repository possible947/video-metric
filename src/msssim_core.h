#ifndef MSSSIM_CORE_H
#define MSSSIM_CORE_H

/**
 * MS-SSIM (Multi-Scale SSIM) Core Algorithm
 * Based on Wang et al. (2003)
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
 * Compute MS-SSIM between two Y-plane images
 *
 * This implements the Multi-Scale SSIM algorithm as described in:
 * "Multi-scale structural similarity for image quality assessment"
 * by Z. Wang, E. P. Simoncelli and A. C. Bovik, 2003
 *
 * Algorithm:
 * 1. Build 5-level Gaussian pyramids (downsample by 2x each level)
 * 2. Compute SSIM at each scale with 11×11 Gaussian window
 * 3. Combine using weights: {0.0448, 0.2856, 0.3001, 0.2363, 0.1333}
 *
 * @param ref_y    Reference Y-plane in float [0..1]
 * @param dist_y   Distorted Y-plane in float [0..1]
 * @param width    Image width (must be >= 176 for 5 levels)
 * @param height   Image height (must be >= 144 for 5 levels)
 * @return         MS-SSIM score [0..1], or -1.0 on error
 */
double compute_msssim_frame(
    const float *ref_y,
    const float *dist_y,
    int width,
    int height
);

#endif /* MSSSIM_CORE_H */
