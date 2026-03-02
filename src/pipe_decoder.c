/*====================================================================*/
/*  FILE: src/pipe_decoder.c                                          */
/*====================================================================*/
/**
 * @file   pipe_decoder.c
 * @brief  FFmpeg pipe-based video decoder for extracting Y-plane
 *
 * This module decodes video through FFmpeg pipes, extracting only
 * the luminance (Y) channel for MS-SSIM computation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>

#include "pipe_decoder.h"
#include "path_util.h"

/* ------------------------------------------------------------------ */
/*  Get local ffmpeg path                                             */
/* ------------------------------------------------------------------ */
static const char *get_ffmpeg_path(void)
{
    if (access("./ffmpeg", X_OK) == 0)
        return "./ffmpeg";
    
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Get video metadata using ffmpeg -i                                */
/* ------------------------------------------------------------------ */
int get_video_info(const char *filepath, VideoInfo *info)
{
    if (!filepath || !info) {
        fprintf(stderr, "get_video_info: NULL argument\n");
        return -1;
    }

    const char *ffmpeg = get_ffmpeg_path();
    if (!ffmpeg) {
        fprintf(stderr, "get_video_info: local ffmpeg not found\n");
        return -1;
    }

    /* Use ffmpeg -i to get video metadata */
    char cmd[PATH_MAX * 2];
    snprintf(cmd, sizeof(cmd),
             "%s -i \"%s\" 2>&1",
             ffmpeg, filepath);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        perror("popen");
        return -1;
    }

    /* Parse output line by line looking for Stream line with Video */
    char line[512];
    info->width = 0;
    info->height = 0;
    info->num_frames = 0;
    info->fps = 0.0;
    strcpy(info->pix_fmt, "yuv420p");

    while (fgets(line, sizeof(line), fp)) {
        /* Look for: "Stream #0:0: Video: h264 (High), yuv420p(tv, bt709), 3840x2160 [SAR 1:1 DAR 16:9], 29.97 fps" */
        if (strstr(line, "Stream") && strstr(line, "Video")) {
            /* Try to extract resolution WIDTHxHEIGHT */
            char *p = line;
            while (*p) {
                if (isdigit(*p)) {
                    int w = 0, h = 0;
                    if (sscanf(p, "%dx%d", &w, &h) == 2) {
                        if (w > 0 && h > 0 && w <= 16384 && h <= 16384) {
                            info->width = w;
                            info->height = h;
                            break;
                        }
                    }
                }
                p++;
            }
            
            /* Extract fps */
            char *fps_str = strstr(line, " fps");
            if (fps_str) {
                /* Search backwards for the number */
                char *p = fps_str - 1;
                while (p > line && (*p == ' ' || *p == ',')) p--;
                char *end = p + 1;
                while (p > line && (isdigit(*p) || *p == '.')) p--;
                if (p < end) {
                    char fps_buf[32];
                    int len = end - p - 1;
                    if (len > 0 && len < 30) {
                        strncpy(fps_buf, p + 1, len);
                        fps_buf[len] = '\0';
                        info->fps = atof(fps_buf);
                    }
                }
            }
        }
        
        /* Look for Duration line to estimate frame count */
        if (strstr(line, "Duration:")) {
            /* Parse "Duration: HH:MM:SS.xx" */
            int h = 0, m = 0;
            float s = 0.0f;
            if (sscanf(line, " Duration: %d:%d:%f", &h, &m, &s) == 3) {
                double duration = h * 3600 + m * 60 + s;
                if (info->fps > 0.0) {
                    info->num_frames = (int)(duration * info->fps + 0.5);
                }
            }
        }
    }

    pclose(fp);

    /* Validate metadata */
    if (info->width <= 0 || info->height <= 0) {
        fprintf(stderr, "get_video_info: invalid dimensions (%dx%d)\n",
                info->width, info->height);
        return -1;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Open video file for pipe decoding                                 */
/* ------------------------------------------------------------------ */
int pipe_decoder_open(PipeDecoder *dec, const char *filepath, int width, int height)
{
    if (!dec || !filepath) {
        fprintf(stderr, "pipe_decoder_open: NULL argument\n");
        return -1;
    }

    if (width <= 0 || height <= 0) {
        fprintf(stderr, "pipe_decoder_open: invalid dimensions (%dx%d)\n", width, height);
        return -1;
    }

    const char *ffmpeg = get_ffmpeg_path();
    if (!ffmpeg) {
        fprintf(stderr, "pipe_decoder_open: local ffmpeg not found\n");
        return -1;
    }

    /* Initialize decoder structure */
    memset(dec, 0, sizeof(PipeDecoder));
    dec->width = width;
    dec->height = height;
    
    /* Calculate frame sizes for YUV420p format */
    dec->frame_size_y = width * height;
    dec->frame_size_uv = (width / 2) * (height / 2);
    dec->frame_size_total = dec->frame_size_y + 2 * dec->frame_size_uv;

    /* Allocate frame buffer */
    dec->frame_buffer = malloc(dec->frame_size_total);
    if (!dec->frame_buffer) {
        perror("malloc");
        return -1;
    }

    /* Build ffmpeg command to decode to raw YUV420p */
    char cmd[PATH_MAX * 2];
    snprintf(cmd, sizeof(cmd),
             "%s -i \"%s\" -f rawvideo -pix_fmt yuv420p pipe:1 2>/dev/null",
             ffmpeg, filepath);

    /* Open pipe for reading */
    dec->pipe = popen(cmd, "r");
    if (!dec->pipe) {
        perror("popen");
        free(dec->frame_buffer);
        dec->frame_buffer = NULL;
        return -1;
    }

    dec->is_open = 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Read Y-plane from next frame                                      */
/* ------------------------------------------------------------------ */
int pipe_decoder_read_y_plane(PipeDecoder *dec, float *y_plane_out)
{
    if (!dec || !dec->is_open || !y_plane_out) {
        fprintf(stderr, "pipe_decoder_read_y_plane: invalid decoder or output buffer\n");
        return -1;
    }

    /* Read one complete frame (Y + U + V) */
    size_t bytes_read = fread(dec->frame_buffer, 1, dec->frame_size_total, dec->pipe);
    
    if (bytes_read < (size_t)dec->frame_size_total) {
        /* EOF or error */
        if (feof(dec->pipe)) {
            return -1; /* Normal EOF */
        }
        if (ferror(dec->pipe)) {
            fprintf(stderr, "pipe_decoder_read_y_plane: read error\n");
            return -1;
        }
        /* Incomplete frame */
        return -1;
    }

    /* Extract Y-plane and convert uint8 [0..255] to float [0..1] */
    for (int i = 0; i < dec->frame_size_y; i++) {
        y_plane_out[i] = dec->frame_buffer[i] / 255.0f;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Close pipe decoder                                                */
/* ------------------------------------------------------------------ */
void pipe_decoder_close(PipeDecoder *dec)
{
    if (!dec)
        return;

    if (dec->pipe) {
        pclose(dec->pipe);
        dec->pipe = NULL;
    }

    if (dec->frame_buffer) {
        free(dec->frame_buffer);
        dec->frame_buffer = NULL;
    }

    dec->is_open = 0;
}
