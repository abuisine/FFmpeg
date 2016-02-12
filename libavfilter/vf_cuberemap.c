/*
 * Copyright (c) 2016 Alexandre Buisine <alexandrejabuisine@gmail.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Cubemap remapping filter
 */

#include "libavutil/avassert.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "internal.h"
#include "video.h"

#define RIGHT   0
#define LEFT    1
#define TOP     2
#define BOTTOM  3
#define FRONT   4
#define BACK    5

typedef enum InputLayout {
    INPUT_LAYOUT_CUBEMAP,
    INPUT_LAYOUT_CUBEMAP_32,
    INPUT_LAYOUT_CUBEMAP_180,
    INPUT_LAYOUT_PLANE_POLES,
    INPUT_LAYOUT_PLANE_POLES_6,
    INPUT_LAYOUT_PLANE_POLES_CUBEMAP,
    INPUT_LAYOUT_PLANE_CUBEMAP,
    INPUT_LAYOUT_PLANE_CUBEMAP_32,

    INPUT_LAYOUT_N
} InputLayout;

typedef enum OutputLayout {
    OUTPUT_LAYOUT_CUBEMAP,
    OUTPUT_LAYOUT_CUBEMAP_32,
    OUTPUT_LAYOUT_CUBEMAP_180,
    OUTPUT_LAYOUT_PLANE_POLES,
    OUTPUT_LAYOUT_PLANE_POLES_6,
    OUTPUT_LAYOUT_PLANE_POLES_CUBEMAP,
    OUTPUT_LAYOUT_PLANE_CUBEMAP,
    OUTPUT_LAYOUT_PLANE_CUBEMAP_32,

    OUTPUT_LAYOUT_N
} OutputLayour;

typedef struct TransformPixelWeights {
    uint32_t *pairs;
    uint8_t n;
} TransformPixelWeights;

typedef struct TransformPlaneMap {
    int w, h;
    TransformPixelWeights *weights;
} TransformPlaneMap;

typedef struct TransformContext {
    const AVClass *class;
    TransformPlaneMap *out_map;

    AVDictionary *opts;
} TransformContext;

#define OFFSET(x) offsetof(CuberemapContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
static const AVOption cuberemap_options[] = {
    { "input_layout", "Input video layout format",         OFFSET(input_layout),    AV_OPT_TYPE_INT,  {.i64 = INPUT_LAYOUT_CUBEMAP }, 0, INPUT_LAYOUT_N - 1,  .flags = FLAGS, "input_format" },
    { "cubemap",             NULL, 0, AV_OPT_TYPE_CONST, {.i64 = INPUT_LAYOUT_CUBEMAP },             0, 0, FLAGS, "input_layout" },
    { "cubemap_32",          NULL, 0, AV_OPT_TYPE_CONST, {.i64 = INPUT_LAYOUT_CUBEMAP_32 },          0, 0, FLAGS, "input_layout" },
    { "cubemap_180",         NULL, 0, AV_OPT_TYPE_CONST, {.i64 = INPUT_LAYOUT_CUBEMAP_180 },         0, 0, FLAGS, "input_layout" },
    { "plane_poles",         NULL, 0, AV_OPT_TYPE_CONST, {.i64 = INPUT_LAYOUT_PLANE_POLES },         0, 0, FLAGS, "input_layout" },
    { "plane_poles_6",       NULL, 0, AV_OPT_TYPE_CONST, {.i64 = INPUT_LAYOUT_PLANE_POLES_6 },       0, 0, FLAGS, "input_layout" },
    { "plane_poles_cubemap", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = INPUT_LAYOUT_PLANE_POLES_CUBEMAP }, 0, 0, FLAGS, "input_layout" },
    { "plane_cubemap",       NULL, 0, AV_OPT_TYPE_CONST, {.i64 = INPUT_LAYOUT_PLANE_CUBEMAP },       0, 0, FLAGS, "input_layout" },
    { "plane_cubemap_32",    NULL, 0, AV_OPT_TYPE_CONST, {.i64 = INPUT_LAYOUT_PLANE_CUBEMAP_32 },    0, 0, FLAGS, "input_layout" },
    { "output_layout", "Output video layout format",         OFFSET(output_layout),    AV_OPT_TYPE_INT,  {.i64 = OUTPUT_LAYOUT_CUBEMAP_32 }, 0, OUTPUT_LAYOUT_N - 1,  .flags = FLAGS, "output_layout" },
    { "cubemap",             NULL, 0, AV_OPT_TYPE_CONST, {.i64 = OUTPUT_LAYOUT_CUBEMAP },             0, 0, FLAGS, "output_layout" },
    { "cubemap_32",          NULL, 0, AV_OPT_TYPE_CONST, {.i64 = OUTPUT_LAYOUT_CUBEMAP_32 },          0, 0, FLAGS, "output_layout" },
    { "cubemap_180",         NULL, 0, AV_OPT_TYPE_CONST, {.i64 = OUTPUT_LAYOUT_CUBEMAP_180 },         0, 0, FLAGS, "output_layout" },
    { "plane_poles",         NULL, 0, AV_OPT_TYPE_CONST, {.i64 = OUTPUT_LAYOUT_PLANE_POLES },         0, 0, FLAGS, "output_layout" },
    { "plane_poles_6",       NULL, 0, AV_OPT_TYPE_CONST, {.i64 = OUTPUT_LAYOUT_PLANE_POLES_6 },       0, 0, FLAGS, "output_layout" },
    { "plane_poles_cubemap", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = OUTPUT_LAYOUT_PLANE_POLES_CUBEMAP }, 0, 0, FLAGS, "output_layout" },
    { "plane_cubemap",       NULL, 0, AV_OPT_TYPE_CONST, {.i64 = OUTPUT_LAYOUT_PLANE_CUBEMAP },       0, 0, FLAGS, "output_layout" },
    { "plane_cubemap_32",    NULL, 0, AV_OPT_TYPE_CONST, {.i64 = OUTPUT_LAYOUT_PLANE_CUBEMAP_32 },    0, 0, FLAGS, "output_layout" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(edgedetect);

static av_cold int init(AVFilterContext *ctx)
{
    CuberemapContext *cuberemap = ctx->priv;

    return 0;
}

static int config_props(AVFilterLink *inlink)
{
    int p;
    AVFilterContext *ctx = inlink->dst;
    EdgeDetectContext *edgedetect = ctx->priv;

    edgedetect->nb_planes = inlink->format == AV_PIX_FMT_GRAY8 ? 1 : 3;
    for (p = 0; p < edgedetect->nb_planes; p++) {
        struct plane_info *plane = &edgedetect->planes[p];

        plane->tmpbuf     = av_malloc(inlink->w * inlink->h);
        plane->gradients  = av_calloc(inlink->w * inlink->h, sizeof(*plane->gradients));
        plane->directions = av_malloc(inlink->w * inlink->h);
        if (!plane->tmpbuf || !plane->gradients || !plane->directions)
            return AVERROR(ENOMEM);
    }
    return 0;
}

// We need to end up with X and Y coordinates in the range [0..1).
// Horizontally wrapping is easy: 1.25 becomes 0.25, -0.25 becomes 0.75.
// Vertically, if we pass through the north pole, we start coming back 'down'
// in the Y direction (ie, a reflection from the boundary) but we also are
// on the opposite side of the sphere so the X value changes by 0.5.

static inline void transform_pos(TransformContext *ctx, float x, float y, float *outX, float *outY) {
    int is_right = 0;
    if (ctx->input_stereo_format != STEREO_FORMAT_MONO) {
        if (y > Y_HALF) {
            y = (y - Y_HALF) / Y_HALF;
            if (ctx->vflip) {
                y = 1.0f - y;
            }
            is_right = 1;
        } else {
            y = y / Y_HALF;
        }
    }

    if (ctx->output_layout == LAYOUT_PLANE_POLES) {
        if (x >= ctx->main_plane_ratio) {
            float dx = (x * 2 - 1 - ctx->main_plane_ratio) / (1 - ctx->main_plane_ratio);
            if (y < Y_HALF) {
                // Bottom
                float dy = (y - YC_BOTTOM) / PH;
                *outX = (atan2f(dy, dx)) / (M_PI * 2.0f) + 0.75f;
                *outY = sqrtf(dy * dy + dx * dx) * 0.25f;
            } else {
                // Top
                float dy = (y - YC_TOP) / PH;
                *outX = (atan2f(dy, dx)) / (M_PI * 2.0f) + 0.75f;
                *outY = 1.0f - sqrtf(dy * dy + dx * dx) * 0.25f;
            }
            if (*outX > 1.0f) {
                *outX -= 1.0f;
            }
        } else {
            // Main
            *outX = x / ctx->main_plane_ratio;
            *outY = y * 0.5f + 0.25f;
        }
    } else if (ctx->output_layout == LAYOUT_PLANE_POLES_6) {
        int face = (int) (x * 6);
        if (face < 4) {
            // Main
            *outX = x * 6.0f / 4.0f;
            *outY = y * 0.5f + 0.25f;
        } else {
            float dx, dy;
            x = x * 6.0f - face;
            dx = x * 2 - 1;
            dy = y * 2 - 1;
            if (face == 4) {
                // Top
                *outX = (atan2f(dy, dx)) / (M_PI * 2.0f) + 0.75f;
                *outY = 1.0f - sqrtf(dy * dy + dx * dx) * 0.25f;
            } else {
                // Bottom
                *outX = (atan2f(dy, dx)) / (M_PI * 2.0f) + 0.75f;
                *outY = sqrtf(dy * dy + dx * dx) * 0.25f;
            }
            if (*outX > 1.0f) {
                *outX -= 1.0f;
            }
        }
    } else if (ctx->output_layout == LAYOUT_FLAT_FIXED) {
        // Per the Metadata RFC for orienting the equirectangular coords:
        //                           Heading
        //         -180           0           180
        //       90 +-------------+-------------+   0.0
        //          |             |             |
        //    P     |             |      o      |
        //    i     |             ^             |
        //    t   0 +-------------X-------------+   0.5
        //    c     |             |             |
        //    h     |             |             |
        //          |             |             |
        //      -90 +-------------+-------------+   1.0
        //          0.0          0.5          1.0
        //    X  - the default camera center
        //    ^  - the default up vector
        //    o  - the image center for a pitch of 45 and a heading of 90
        //    Coords on left and top sides are degrees
        //    Coords on right and bottom axes are our X/Y in range [0..1)
        //  Note: Negative field of view can be supplied to flip the image.
        *outX = ((x - 0.5f) * ctx->fixed_hfov + ctx->fixed_yaw)   / 360.0f
            + 0.5f;
        *outY = ((y - 0.5f) * ctx->fixed_vfov - ctx->fixed_pitch) / 180.0f
            + 0.5f;

        normalize_equirectangular(*outX, *outY, outX, outY);
    } else if (ctx->output_layout == LAYOUT_CUBEMAP ||
            ctx->output_layout == LAYOUT_PLANE_POLES_CUBEMAP ||
            ctx->output_layout == LAYOUT_CUBEMAP_32 ||
            ctx->output_layout == LAYOUT_CUBEMAP_180 ||
            ctx->output_layout == LAYOUT_PLANE_CUBEMAP_32 ||
            ctx->output_layout == LAYOUT_PLANE_CUBEMAP) {
        float qx, qy, qz;
        float cos_y, cos_p, sin_y, sin_p;
        float tx, ty, tz;
        float d;
        y = 1.0f - y;

        const float *vx, *vy, *p;
        int face = 0;
        if (ctx->output_layout == LAYOUT_CUBEMAP) {
            face = (int) (x * 6);
            x = x * 6.0f - face;
        } else if (ctx->output_layout == LAYOUT_CUBEMAP_32) {
            int vface = (int) (y * 2);
            int hface = (int) (x * 3);
            x = x * 3.0f - hface;
            y = y * 2.0f - vface;
            face = hface + (1 - vface) * 3;
        } else if (ctx->output_layout == LAYOUT_CUBEMAP_180) {
            // LAYOUT_CUBEMAP_180: layout for spatial resolution downsampling with 180 degree viewport size
            //
            // - Given a view (yaw,pitch) we can create a customized cube mapping to make the view center at the front cube face.
            // - A 180 degree viewport cut the cube into 2 equal-sized halves: front half and back half.
            // - The front half contains these faces of the cube: front, half of right, half of left, half of top, half of bottom.
            //   The back half contains these faces of the cube: back, half of right, half of left, half of top, half of bottom.
            //   Illutrasion on LAYOUT_CUBEMAP_32 (mono):
            //
            //   +---+---+---+---+---+---+
            //   |   |   |   |   |   5   |
            //   + 1 | 2 + 3 | 4 +-------+     Area 1, 4, 6, 7, 9 are in the front half
            //   |   |   |   |   |   6   |
            //   +---+---+---+---+---+---+     Area 2, 3, 5, 8, 0 are in the back half
            //   |   7   |       |       |
            //   +-------+   9   +   0   +
            //   |   8   |       |       |
            //   +---+---+---+---+---+---+
            //
            // - LAYOUT_CUBEMAP_180 reduces the spatial resolution of the back half to 25% (1/2 height, 1/2 width makes 1/4 size)
            //   and then re-pack the cube map like this:
            //
            //   +---+---+---+---+---+      Front half   Back half (1/4 size)
            //   |       |   |   c   |      ----------   --------------------
            //   +   a   + b +---+----      Area a = 9   Area f = 0
            //   |       |   | f |   |      Area b = 4   Area g = 3
            //   +---+---+---+---+ d +      Area c = 6   Area h = 2
            //   |g|h|-i-|   e   |   |      Area d = 1   Area i1(top) = 5
            //   +---+---+---+---+---+      Area e = 7   Area i2(bottom) = 8
            //
            if (0.0f <= y && y < 1.0f/3 && 0.0f <= x && x < 0.8f) { // Area g, h, i1, i2, e
                if (0.0f <= x && x < 0.1f) { // g
                    face = LEFT;
                    x = x/0.2f;
                    y = y/(1.0f/3);
                }
                else if (0.1f <= x && x < 0.2f) { // h
                    face = RIGHT;
                    x = (x-0.1f)/0.2f + 0.5f;
                    y = y/(1.0f/3);
                }
                else if (0.2f <= x && x < 0.4f) {
                    if (y >= 1.0f/6){ //i1
                        face = TOP;
                        x = (x-0.2f)/0.2f;
                        y  =(y-1.0f/6)/(1.0f/3) + 0.5f;
                    }
                    else { // i2
                        face = BOTTOM;
                        x = (x-0.2f)/0.2f;
                        y = y/(1.0f/3);
                    }
                }
                else if (0.4f <= x && x < 0.8f){ // e
                    face = BOTTOM;
                    x = (x-0.4f)/0.4f;
                    y = y/(2.0f/3) + 0.5f;
                }
            }
            else if (2.0f/3 <= y && y < 1.0f && 0.6f <= x && x < 1.0f) { // Area c
                face = TOP;
                x = (x-0.6f)/0.4f;
                y = (y-2.0f/3)/(2.0f/3);
            }
            else { // Area a, b, f, d
                if (0.0f <= x && x < 0.4f) { // a
                    face = FRONT;
                    x = x/0.4f;
                    y = (y-1.0/3)/(2.0f/3);
                }
                else if (0.4f <= x && x < 0.6f) { // b
                    face = LEFT;
                    x = (x-0.4f)/0.4f + 0.5f;
                    y = (y-1.0f/3)/(2.0f/3);
                }
                else if (0.6f <= x && x < 0.8f) { // f
                    face = BACK;
                    x = (x-0.6f)/0.2f;
                    y = (y-1.0f/3)/(1.0f/3);
                }
                else if (0.8f <= x && x < 1.0f) { // d
                    face = RIGHT;
                    x = (x-0.8f)/0.4f;
                    y = y/(2.0f/3);
                }
            }
        } else if (ctx->output_layout == LAYOUT_PLANE_CUBEMAP_32) {
            int vface = (int) (y * 2);
            int hface = (int) (x * 3);
            x = x * 3.0f - hface;
            y = y * 2.0f - vface;
            face = hface + (1 - vface) * 3;
            face = PLANE_CUBEMAP_32_FACE_MAP[face];
        } else if (ctx->output_layout == LAYOUT_PLANE_POLES_CUBEMAP) {
            face = (int) (x * 4.5f);
            x = x * 4.5f - face;
            if (face == 4) {
                x *= 2.0f;
                y *= 2.0f;
                if (y >= 1.0f) {
                    y -= 1.0f;
                } else {
                    face = 5; // bottom
                }
            }
            face = PLANE_POLES_FACE_MAP[face];
        } else if (ctx->output_layout == LAYOUT_PLANE_CUBEMAP) {
            face = (int) (x * 6);
            x = x * 6.0f - face;
            face = PLANE_CUBEMAP_FACE_MAP[face];
        } else {
            av_assert0(0);
        }
        av_assert1(x >= 0 && x <= 1);
        av_assert1(y >= 0 && y <= 1);
        av_assert1(face >= 0 && face < 6);
        x = (x - 0.5f) * ctx->expand_coef + 0.5f;
        y = (y - 0.5f) * ctx->expand_coef + 0.5f;

        switch (face) {
            case RIGHT:   p = P5; vx = NZ; vy = PY; break;
            case LEFT:    p = P0; vx = PZ; vy = PY; break;
            case TOP:     p = P6; vx = PX; vy = NZ; break;
            case BOTTOM:  p = P0; vx = PX; vy = PZ; break;
            case FRONT:   p = P4; vx = PX; vy = PY; break;
            case BACK:    p = P1; vx = NX; vy = PY; break;
        }
        qx = p [0] + vx [0] * x + vy [0] * y;
        qy = p [1] + vx [1] * x + vy [1] * y;
        qz = p [2] + vx [2] * x + vy [2] * y;

        // rotation
        sin_y = sin(ctx->fixed_yaw*M_PI/180.0f);
        sin_p = sin(ctx->fixed_pitch*M_PI/180.0f);
        cos_y = cos(ctx->fixed_yaw*M_PI/180.0f);
        cos_p = cos(ctx->fixed_pitch*M_PI/180.0f);
        tx = qx * cos_y   - qy * sin_y*sin_p  + qz * sin_y*cos_p;
        ty =                qy * cos_p        + qz * sin_p;
        tz = qx* (-sin_y) - qy * cos_y*sin_p  + qz * cos_y*cos_p;

        d = sqrtf(tx * tx + ty * ty + tz * tz);
        *outX = -atan2f (-tx / d, tz / d) / (M_PI * 2.0f) + 0.5f;
        *outY = asinf (-ty / d) / M_PI + 0.5f;
    }

    if (ctx->input_stereo_format == STEREO_FORMAT_TB) {
        if (is_right) {
            *outY = *outY * Y_HALF + Y_HALF;
        } else {
            *outY = *outY * Y_HALF;
        }
    } else if (ctx->input_stereo_format == STEREO_FORMAT_LR) {
        if (is_right) {
            *outX = *outX * X_HALF + X_HALF;
        } else {
            *outX = *outX * X_HALF;
        }
    } else {
        // mono no steps needed.
    }
    av_assert1(*outX >= 0 && *outX <= 1);
    av_assert1(*outY >= 0 && *outY <= 1);
}


static inline int generate_map(TransformContext *s,
        AVFilterLink *inlink, AVFilterLink *outlink, AVFrame *in) {
    AVFilterContext *ctx = outlink->src;

    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(outlink->format);
    s->planes = av_pix_fmt_count_planes(outlink->format);
    s->out_map_planes = 2;
    s->out_map = av_malloc_array(s->out_map_planes, sizeof(*s->out_map));
    if (!s->out_map) {
        return AVERROR(ENOMEM);
    }

    for (int plane = 0; plane < s->out_map_planes; ++plane) {
        int out_w, out_h, in_w, in_h;
        TransformPlaneMap *p;
        av_log(ctx, AV_LOG_VERBOSE, "processing plane #%d\n",
                plane);
        out_w = outlink->w;
        out_h = outlink->h;
        in_w = inlink->w;
        in_h = inlink->h;

        if (plane == 1) {
            out_w = FF_CEIL_RSHIFT(out_w, desc->log2_chroma_w);
            out_h = FF_CEIL_RSHIFT(out_h, desc->log2_chroma_h);
            in_w = FF_CEIL_RSHIFT(in_w, desc->log2_chroma_w);
            in_h = FF_CEIL_RSHIFT(in_h, desc->log2_chroma_h);
        }
        p = &s->out_map[plane];
        p->w = out_w;
        p->h = out_h;
        p->weights = av_malloc_array(out_w * out_h, sizeof(*p->weights));
        if (!p->weights) {
            return AVERROR(ENOMEM);
        }
        for (int i = 0; i < out_h; ++i) {
            for (int j = 0; j < out_w; ++j) {
                int id = i * out_w + j;
                float out_x, out_y;
                TransformPixelWeights *ws = &p->weights[id];
                ws->n = 0;
                for (int suby = 0; suby < s->h_subdivisons; ++suby) {
                    for (int subx = 0; subx < s->w_subdivisons; ++subx) {
                        float y = (i + (suby + 0.5f) / s->h_subdivisons) / out_h;
                        float x = (j + (subx + 0.5f) / s->w_subdivisons) / out_w;
                        int in_x, in_y;
                        uint32_t in_id;
                        int result;
                        transform_pos(s, x, y, &out_x, &out_y);

                        in_y = (int) (out_y * in_h);
                        in_x = (int) (out_x * in_w);

                        in_id = in_y * in->linesize[plane] + in_x;
                        result = increase_pixel_weight(ws, in_id);
                        if (result != 0) {
                            return result;
                        }
                    }
                }
            }
        }
    }
    return 0;
}

static int filter_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    const ThreadData *td = arg;
    const TransformPlaneMap *p = td->p;
    const int linesize = td->linesize;
    const int subs = td->subs;
    const int num_tiles = td->num_tiles;
    const int num_tiles_col = td->num_tiles_col;
    const uint8_t *in_data = td->in_data;
    uint8_t *out_data = td->out_data;

    const int tile_start = (num_tiles * jobnr) / nb_jobs;
    const int tile_end = (num_tiles * (jobnr+1)) / nb_jobs;

    for (int tile = tile_start ; tile < tile_end ; ++tile) {
        int tile_i = (tile / num_tiles_col) * 16;
        int tile_j = (tile % num_tiles_col) * 16;

#ifdef SOFTWARE_PREFETCH_OPT
        TransformPixelWeights *ws_prefetch;
        const uint8_t prefetch_lookahead = 8;
        int id_prefetch = p->w * tile_i + tile_j;

        // The loop below prefetches all the weights from array "p" and
        // associated pairs array. The prefetch is only done for the initial
        // lookahead for the first iteration of tile processing loop below so
        // that they are ready to be consumed in the inner loop. In the tile
        // processing loop, we are prefetching addresses that are after the
        // lookahead (i.e in the same iteration and also the next
        // ietartion of the loop).
        for(int k = 0; k < prefetch_lookahead; ++k){
           ws_prefetch = &p->weights[id_prefetch+k];
           __builtin_prefetch (ws_prefetch, 0, 0);
           __builtin_prefetch (ws_prefetch->pairs, 0, 0);
        }
        // Prefetch the cacheline for out_data for writes
        int out_sample_prefetch = linesize * (tile_i + 2) + tile_j;
        __builtin_prefetch (&out_data[out_sample_prefetch], 1, 0);
#endif

        if ((tile_i + 15) >= p->h || (tile_j + 15) >= p->w) {
            filter_slice_boundcheck(tile_i, tile_j, linesize, subs, p, in_data, out_data);
            continue;
        }

        for (int i = 0; i < 16; ++i) {
            int out_line = linesize * (tile_i + i);
            int map_line = p->w * (tile_i + i);

#ifdef SOFTWARE_PREFETCH_OPT
            // Prefetch the cacheline for out_data for writes
            __builtin_prefetch (&out_data[out_line+tile_j], 1, 0);
#endif

            for (int j = 0; j < 16; ++j) {
                int out_sample = out_line + tile_j + j;
                int id = map_line + tile_j + j;
                TransformPixelWeights *ws = &p->weights[id];

#ifdef SOFTWARE_PREFETCH_OPT
                // In this inner loop, we prefech the weight from array "p" after the
                // prefetch_lookahead iteration. We also prefetch the weight pairs
                // along with weight address as we found that we were getting
                // datacache (L1 and LLC) and DTLB misses for both the address
                // and the pair.
                if (j <  prefetch_lookahead) {
                   ws_prefetch = &p->weights[id+prefetch_lookahead];
                   __builtin_prefetch (ws_prefetch->pairs, 0, 0);
                   __builtin_prefetch (ws_prefetch+prefetch_lookahead, 0, 0);
                }
                else if (i < 15) {
                   // Here we are prefetching the address for the next iteration of outer loop
                   // so that we have the data avaialble in the next loop when it starts.
                   id_prefetch = p->w + id - prefetch_lookahead;
                   ws_prefetch = &p->weights[id_prefetch];
                   __builtin_prefetch (ws_prefetch->pairs, 0, 0);
                   __builtin_prefetch (ws_prefetch+prefetch_lookahead, 0, 0);
                }
                // Prefetch the cacheline for out_data for writes
                __builtin_prefetch (&out_data[out_sample], 1, 0);
#endif

                if (ws->n == 1) {
                    out_data[out_sample] = in_data[UNPACK_ID(ws->pairs[0])];
                } else {
                    int color_sum = 0;
                    for (int k = 0; k < ws->n; ++k) {
                        color_sum += ((int) in_data[UNPACK_ID(ws->pairs[k])]) *
                            UNPACK_COUNT(ws->pairs[k]);
                    }
                    // Round to nearest
                    out_data[out_sample] = (uint8_t) ((color_sum + (subs >> 1)) / subs);
                }
            }
        }
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    TransformContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    int subs;
    av_log(ctx, AV_LOG_VERBOSE, "Frame\n");

    // map not yet set
    if (s->out_map_planes != 2) {
        int result = generate_map(s, inlink, outlink, in);
        if (result != 0) {
            av_frame_free(&in);
            return result;
        }
    }

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    av_log(ctx, AV_LOG_VERBOSE, "Got Frame %dx%d\n", outlink->w, outlink->h);

    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);
    av_log(ctx, AV_LOG_VERBOSE, "Copied props \n");
    subs = s->w_subdivisons * s->h_subdivisons;

    for (int plane = 0; plane < s->planes; ++plane) {
        uint8_t *in_data, *out_data;
        int out_map_plane;
        TransformPlaneMap *p;

        in_data = in->data[plane];
        av_assert1(in_data);
        out_map_plane = (plane == 1 || plane == 2) ? 1 : 0;
        p = &s->out_map[out_map_plane];
        out_data = out->data[plane];

        int num_tiles_row = 1 + ((p->h - 1) / 16); // ceiling operation
        int num_tiles_col = 1 + ((p->w - 1) / 16); // ceiling operation
        int num_tiles = num_tiles_row * num_tiles_col;

        ThreadData td;
        td.p = p;
        td.subs = subs;
        td.linesize = out->linesize[plane];
        td.num_tiles = num_tiles;
        td.num_tiles_col = num_tiles_col;
        td.in_data = in_data;
        td.out_data = out_data;
        ctx->internal->execute(ctx, filter_slice, &td, NULL, FFMIN(num_tiles, ctx->graph->nb_threads));
    }

    av_log(ctx, AV_LOG_VERBOSE, "Done with byte copy \n");

    av_frame_free(&in);
    av_log(ctx, AV_LOG_VERBOSE, "Done freeing in \n");
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    CuberemapContext *cuberemap = ctx->priv;
    
    return 0;
}

static const AVFilterPad avfilter_vf_cuberemap_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_cuberemap_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_cuberemap = {
    .name        = "cuberemap",
    .description = NULL_IF_CONFIG_SMALL("Remaps a cubemap."),
    .init        = init,
    .uninit      = uninit,
    .priv_size   = sizeof(CuberemapContext),
    .priv_class  = &cuberemap_class,
    .inputs      = avfilter_vf_cuberemap_inputs,
    .outputs     = avfilter_vf_cuberemap_outputs,
};
