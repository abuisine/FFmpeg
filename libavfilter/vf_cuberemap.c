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
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

#define RIGHT   0
#define LEFT    1
#define TOP     2
#define BOTTOM  3
#define FRONT   4
#define BACK    5

typedef enum Layout {
    LAYOUT_CUBEMAP,
    LAYOUT_CUBEMAP_32,
    LAYOUT_CUBEMAP_180,
    LAYOUT_PLANE_POLES,
    LAYOUT_PLANE_POLES_6,
    LAYOUT_PLANE_POLES_CUBEMAP,
    LAYOUT_PLANE_CUBEMAP,
    LAYOUT_PLANE_CUBEMAP_32,

    LAYOUT_N
} Layout;

typedef struct CubeFace {
    int i_x;
    int i_y;
    int o_x;
    int o_y;
    int w;
    int h;
} CubeFace;

typedef struct ChromaDisplacement {
    int x;
    int y;
} ChromaDisplacement;

typedef struct CuberemapContext {
    const AVClass *class;
    CubeFace sprites[42]; //FIXME
    int nb_sprites;
    int nb_planes;
    ChromaDisplacement chroma[4];
    int pixstep[4];
    int linesize[4];
    int input_layout;
    int output_layout;
} CuberemapContext;

#define OFFSET(x) offsetof(CuberemapContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
static const AVOption cuberemap_options[] = {
    { "input_layout", "Input video layout format",         OFFSET(input_layout),    AV_OPT_TYPE_INT,  {.i64 = LAYOUT_CUBEMAP }, 0, LAYOUT_N - 1,  .flags = FLAGS, "input_format" },
    { "cubemap",             NULL, 0, AV_OPT_TYPE_CONST, {.i64 = LAYOUT_CUBEMAP },             0, 0, FLAGS, "input_layout" },
    { "cubemap_32",          NULL, 0, AV_OPT_TYPE_CONST, {.i64 = LAYOUT_CUBEMAP_32 },          0, 0, FLAGS, "input_layout" },
    { "cubemap_180",         NULL, 0, AV_OPT_TYPE_CONST, {.i64 = LAYOUT_CUBEMAP_180 },         0, 0, FLAGS, "input_layout" },
    { "plane_poles",         NULL, 0, AV_OPT_TYPE_CONST, {.i64 = LAYOUT_PLANE_POLES },         0, 0, FLAGS, "input_layout" },
    { "plane_poles_6",       NULL, 0, AV_OPT_TYPE_CONST, {.i64 = LAYOUT_PLANE_POLES_6 },       0, 0, FLAGS, "input_layout" },
    { "plane_poles_cubemap", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = LAYOUT_PLANE_POLES_CUBEMAP }, 0, 0, FLAGS, "input_layout" },
    { "plane_cubemap",       NULL, 0, AV_OPT_TYPE_CONST, {.i64 = LAYOUT_PLANE_CUBEMAP },       0, 0, FLAGS, "input_layout" },
    { "plane_cubemap_32",    NULL, 0, AV_OPT_TYPE_CONST, {.i64 = LAYOUT_PLANE_CUBEMAP_32 },    0, 0, FLAGS, "input_layout" },
    { "output_layout", "Output video layout format",         OFFSET(output_layout),    AV_OPT_TYPE_INT,  {.i64 = LAYOUT_CUBEMAP_32 }, 0, LAYOUT_N - 1,  .flags = FLAGS, "output_layout" },
    { "cubemap",             NULL, 0, AV_OPT_TYPE_CONST, {.i64 = LAYOUT_CUBEMAP },             0, 0, FLAGS, "output_layout" },
    { "cubemap_32",          NULL, 0, AV_OPT_TYPE_CONST, {.i64 = LAYOUT_CUBEMAP_32 },          0, 0, FLAGS, "output_layout" },
    { "cubemap_180",         NULL, 0, AV_OPT_TYPE_CONST, {.i64 = LAYOUT_CUBEMAP_180 },         0, 0, FLAGS, "output_layout" },
    { "plane_poles",         NULL, 0, AV_OPT_TYPE_CONST, {.i64 = LAYOUT_PLANE_POLES },         0, 0, FLAGS, "output_layout" },
    { "plane_poles_6",       NULL, 0, AV_OPT_TYPE_CONST, {.i64 = LAYOUT_PLANE_POLES_6 },       0, 0, FLAGS, "output_layout" },
    { "plane_poles_cubemap", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = LAYOUT_PLANE_POLES_CUBEMAP }, 0, 0, FLAGS, "output_layout" },
    { "plane_cubemap",       NULL, 0, AV_OPT_TYPE_CONST, {.i64 = LAYOUT_PLANE_CUBEMAP },       0, 0, FLAGS, "output_layout" },
    { "plane_cubemap_32",    NULL, 0, AV_OPT_TYPE_CONST, {.i64 = LAYOUT_PLANE_CUBEMAP_32 },    0, 0, FLAGS, "output_layout" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(cuberemap);

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_RGB24, AV_PIX_FMT_BGR24,
    AV_PIX_FMT_RGB48BE, AV_PIX_FMT_BGR48BE,
    AV_PIX_FMT_RGB48LE, AV_PIX_FMT_BGR48LE,
    AV_PIX_FMT_RGBA64BE, AV_PIX_FMT_BGRA64BE,
    AV_PIX_FMT_RGBA64LE, AV_PIX_FMT_BGRA64LE,
    AV_PIX_FMT_RGBA,  AV_PIX_FMT_BGRA,
    AV_PIX_FMT_ARGB,  AV_PIX_FMT_ABGR,
    AV_PIX_FMT_RGB0,  AV_PIX_FMT_BGR0,
    AV_PIX_FMT_0RGB,  AV_PIX_FMT_0BGR,
    AV_PIX_FMT_GBRP,
    AV_PIX_FMT_GBRP9BE,  AV_PIX_FMT_GBRP9LE,
    AV_PIX_FMT_GBRP10BE, AV_PIX_FMT_GBRP10LE,
    AV_PIX_FMT_GBRP12BE, AV_PIX_FMT_GBRP12LE,
    AV_PIX_FMT_GBRP14BE, AV_PIX_FMT_GBRP14LE,
    AV_PIX_FMT_GBRP16BE, AV_PIX_FMT_GBRP16LE,
    AV_PIX_FMT_YUV410P,
    AV_PIX_FMT_YUV411P,
    AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUVA420P,
    AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUVA422P,
    AV_PIX_FMT_YUV440P,
    AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUVA444P,
    AV_PIX_FMT_YUVJ411P,
    AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUVJ422P,
    AV_PIX_FMT_YUVJ440P,
    AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_YUV420P9LE,  AV_PIX_FMT_YUVA420P9LE,
    AV_PIX_FMT_YUV420P9BE,  AV_PIX_FMT_YUVA420P9BE,
    AV_PIX_FMT_YUV422P9LE,  AV_PIX_FMT_YUVA422P9LE,
    AV_PIX_FMT_YUV422P9BE,  AV_PIX_FMT_YUVA422P9BE,
    AV_PIX_FMT_YUV444P9LE,  AV_PIX_FMT_YUVA444P9LE,
    AV_PIX_FMT_YUV444P9BE,  AV_PIX_FMT_YUVA444P9BE,
    AV_PIX_FMT_YUV420P10LE, AV_PIX_FMT_YUVA420P10LE,
    AV_PIX_FMT_YUV420P10BE, AV_PIX_FMT_YUVA420P10BE,
    AV_PIX_FMT_YUV422P10LE, AV_PIX_FMT_YUVA422P10LE,
    AV_PIX_FMT_YUV422P10BE, AV_PIX_FMT_YUVA422P10BE,
    AV_PIX_FMT_YUV444P10LE, AV_PIX_FMT_YUVA444P10LE,
    AV_PIX_FMT_YUV444P10BE, AV_PIX_FMT_YUVA444P10BE,
    AV_PIX_FMT_YUV420P12BE,  AV_PIX_FMT_YUV420P12LE,
    AV_PIX_FMT_YUV422P12BE,  AV_PIX_FMT_YUV422P12LE,
    AV_PIX_FMT_YUV444P12BE,  AV_PIX_FMT_YUV444P12LE,
    AV_PIX_FMT_YUV420P14BE,  AV_PIX_FMT_YUV420P14LE,
    AV_PIX_FMT_YUV422P14BE,  AV_PIX_FMT_YUV422P14LE,
    AV_PIX_FMT_YUV444P14BE,  AV_PIX_FMT_YUV444P14LE,
    AV_PIX_FMT_YUV420P16LE, AV_PIX_FMT_YUVA420P16LE,
    AV_PIX_FMT_YUV420P16BE, AV_PIX_FMT_YUVA420P16BE,
    AV_PIX_FMT_YUV422P16LE, AV_PIX_FMT_YUVA422P16LE,
    AV_PIX_FMT_YUV422P16BE, AV_PIX_FMT_YUVA422P16BE,
    AV_PIX_FMT_YUV444P16LE, AV_PIX_FMT_YUVA444P16LE,
    AV_PIX_FMT_YUV444P16BE, AV_PIX_FMT_YUVA444P16BE,
    AV_PIX_FMT_NONE
    };
    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

// static av_cold int init(AVFilterContext *ctx)
// {
//     CuberemapContext *cr = ctx->priv;

//     return 0;
// }

// static av_cold void uninit(AVFilterContext *ctx)
// {
//     CuberemapContext *cr = ctx->priv;
// }

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    CuberemapContext *cr = ctx->priv;;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int i;

    outlink->w = inlink->w;
    outlink->h = inlink->h / 2;

    av_log(ctx, AV_LOG_VERBOSE, "resize: %dx%d -> %dx%d.\n",
        inlink->w,
        inlink->h,
        outlink->w,
        outlink->h
       );


    cr->nb_planes = av_pix_fmt_count_planes(inlink->format);
    av_log(ctx, AV_LOG_VERBOSE, "planes count: %d.\n", cr->nb_planes);

    av_image_fill_max_pixsteps(cr->pixstep, NULL, desc);

    int ret;
    if ((ret = av_image_fill_linesizes(cr->linesize, outlink->format, outlink->w)) < 0)
        return ret;

    int p;
    for(p = 0; p < cr->nb_planes; p++) {
        av_log(ctx, AV_LOG_VERBOSE, "plane %d pixel step: %d.\n", p, cr->pixstep[p]);
        av_log(ctx, AV_LOG_VERBOSE, "plane %d linesize: %d.\n", p, cr->linesize[p]);
    }

    cr->chroma[0].x = 0;
    cr->chroma[0].y = 0;
    cr->chroma[1].x = desc->log2_chroma_w;
    cr->chroma[1].y = desc->log2_chroma_h;
    cr->chroma[3].x = 0;
    cr->chroma[3].y = 0;
    cr->chroma[2].x = desc->log2_chroma_w;
    cr->chroma[2].y = desc->log2_chroma_h;

    // LEFT RIGHT
    // left eye = right
    i = 0;
    cr->sprites[i].i_x = 0;
    cr->sprites[i].i_y = 0;
    cr->sprites[i].o_x = 0;
    cr->sprites[i].o_y = 0;
    cr->sprites[i].w = inlink->w / 6;
    cr->sprites[i].h = inlink->h / 4;

    // left eye = left
    i++;
    cr->sprites[i].i_x = inlink->w / 2;
    cr->sprites[i].i_y = 0;
    cr->sprites[i].o_x = outlink-> w / 6;
    cr->sprites[i].o_y = 0;
    cr->sprites[i].w = inlink->w / 6;
    cr->sprites[i].h = inlink->h / 4;

    // right eye = right
    i++;
    cr->sprites[i].i_x = 0;
    cr->sprites[i].i_y = inlink->h / 2;
    cr->sprites[i].o_x = 0;
    cr->sprites[i].o_y = outlink->h / 2;
    cr->sprites[i].w = inlink->w / 6;
    cr->sprites[i].h = inlink->h / 4;

    // right eye = left
    i++;
    cr->sprites[i].i_x = inlink->w / 2;
    cr->sprites[i].i_y = inlink->h / 2;
    cr->sprites[i].o_x = outlink-> w / 6;
    cr->sprites[i].o_y = outlink->h / 2;
    cr->sprites[i].w = inlink->w / 6;
    cr->sprites[i].h = inlink->h / 4;

    // TOP BOTTOM
    // left eye = bottom
    i++;
    cr->sprites[i].i_x = 0;
    cr->sprites[i].i_y = inlink->h / 4;
    cr->sprites[i].o_x = outlink->w / 3 * 2;
    cr->sprites[i].o_y = 0;
    cr->sprites[i].w = inlink->w / 3;
    cr->sprites[i].h = inlink->h / 8;

    // left eye = top
    i++;
    cr->sprites[i].i_x = inlink->w / 3 * 2;
    cr->sprites[i].i_y = inlink->h / 8;
    cr->sprites[i].o_x = outlink->w / 3 * 2;
    cr->sprites[i].o_y = outlink->h / 4;
    cr->sprites[i].w = inlink->w / 3;
    cr->sprites[i].h = inlink->h / 8;

    // right eye = bottom
    i++;
    cr->sprites[i].i_x = 0;
    cr->sprites[i].i_y = inlink->h / 4 * 3;
    cr->sprites[i].o_x = outlink->w / 3 * 2;
    cr->sprites[i].o_y = outlink->h / 2;
    cr->sprites[i].w = inlink->w / 3;
    cr->sprites[i].h = inlink->h / 8;

    // right eye = top
    i++;
    cr->sprites[i].i_x = inlink->w / 3 * 2;
    cr->sprites[i].i_y = inlink->h / 8 * 5;
    cr->sprites[i].o_x = outlink->w / 3 * 2;
    cr->sprites[i].o_y = outlink->h / 4 * 3;
    cr->sprites[i].w = inlink->w / 3;
    cr->sprites[i].h = inlink->h / 8;

    // FACE
    // right eye = face
    i++;
    cr->sprites[i].i_x = inlink->w / 3;
    cr->sprites[i].i_y = inlink->h / 4;
    cr->sprites[i].o_x = outlink->w / 3;
    cr->sprites[i].o_y = 0;
    cr->sprites[i].w = inlink->w / 3;
    cr->sprites[i].h = inlink->h / 4;

    // right eye = face
    i++;
    cr->sprites[i].i_x = inlink->w / 3;
    cr->sprites[i].i_y = inlink->h / 4 * 3;
    cr->sprites[i].o_x = outlink->w / 3;
    cr->sprites[i].o_y = outlink->h / 2;
    cr->sprites[i].w = inlink->w / 3;
    cr->sprites[i].h = inlink->h / 4;

    cr->nb_sprites = i + 1;
    av_log(ctx, AV_LOG_VERBOSE, "sprites count: %d.\n", cr->nb_sprites);

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    CuberemapContext *cr = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    int f, p = 0;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    for (f = 0; f < cr->nb_sprites; f++) {
        av_log(ctx, AV_LOG_DEBUG, "processing sprite %d.\n", f);
        for (p = 0; p < cr->nb_planes; p++) {
            av_log(ctx, AV_LOG_DEBUG, "processing plane %d.\n", p);
            av_image_copy_plane(out->data[p]
                    + cr->sprites[f].o_y * FF_CEIL_RSHIFT(out->linesize[p], cr->chroma[p].y)
                    + FF_CEIL_RSHIFT(cr->sprites[f].o_x * cr->pixstep[p], cr->chroma[p].x), out->linesize[p],
                in->data[p]
                    + cr->sprites[f].i_y * FF_CEIL_RSHIFT(in->linesize[p], cr->chroma[p].y)
                    + FF_CEIL_RSHIFT(cr->sprites[f].i_x * cr->pixstep[p], cr->chroma[p].x), in->linesize[p],

                FF_CEIL_RSHIFT(cr->sprites[f].w * cr->pixstep[p], cr->chroma[p].y), FF_CEIL_RSHIFT(cr->sprites[f].h, cr->chroma[p].y));
        }
    }

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static const AVFilterPad avfilter_vf_cuberemap_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
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
    // .init        = init,
    // .uninit      = uninit,
    .priv_size   = sizeof(CuberemapContext),
    .priv_class  = &cuberemap_class,
    .query_formats = query_formats,
    .inputs      = avfilter_vf_cuberemap_inputs,
    .outputs     = avfilter_vf_cuberemap_outputs,
};
