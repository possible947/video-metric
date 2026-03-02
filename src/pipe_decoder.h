#ifndef PIPE_DECODER_H
#define PIPE_DECODER_H

#include <stdint.h>

/**
 * Video metadata structure
 */
typedef struct {
    int width;
    int height;
    int num_frames;
    double fps;
    char pix_fmt[32];
} VideoInfo;

/**
 * FFmpeg pipe decoder structure
 */
typedef struct {
    FILE *pipe;
    int width;
    int height;
    int frame_size_y;      /* width * height */
    int frame_size_uv;     /* (width/2) * (height/2) for 420 */
    int frame_size_total;  /* Y + U + V */
    uint8_t *frame_buffer;
    int is_open;
} PipeDecoder;

/**
 * Get video metadata using ffprobe
 *
 * @param filepath  Path to video file
 * @param info      Output structure for video metadata
 * @return 0 on success, -1 on failure
 */
int get_video_info(const char *filepath, VideoInfo *info);

/**
 * Open video file for decoding through FFmpeg pipe
 *
 * @param dec       Decoder structure to initialize
 * @param filepath  Path to video file
 * @param width     Video width
 * @param height    Video height
 * @return 0 on success, -1 on failure
 */
int pipe_decoder_open(PipeDecoder *dec, const char *filepath, int width, int height);

/**
 * Read next frame's Y-plane (luminance only) from pipe
 *
 * @param dec          Decoder structure
 * @param y_plane_out  Output buffer for Y-plane in float [0..1]
 *                     Must be allocated by caller (width * height floats)
 * @return 0 on success, -1 on EOF or error
 */
int pipe_decoder_read_y_plane(PipeDecoder *dec, float *y_plane_out);

/**
 * Close pipe decoder and free resources
 *
 * @param dec  Decoder structure to close
 */
void pipe_decoder_close(PipeDecoder *dec);

#endif /* PIPE_DECODER_H */
