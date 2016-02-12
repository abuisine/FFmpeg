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
} OutputLayout;

typedef struct CubeFace {
    int x;
    int y;
    int w;
    int h;
} CubeFace;

typedef struct CuberemapContext {
    const AVClass *class;
    CubeFace faces;
    int width, height;
    int nb_faces;
    int nb_planes;
    int linesize[4];
    int pixstep[4];
    int pheight[4];
    int hsub, vsub;
    int input_layout;
    int output_layout;
    int in_off_left[4], in_off_right[4];
} CuberemapContext;

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

AVFILTER_DEFINE_CLASS(cuberemap);

static av_cold int init(AVFilterContext *ctx)
{
    CuberemapContext *cuberemap = ctx->priv;

    cuberemap->nb_faces = 1;

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    CuberemapContext *cuberemap = ctx->priv;

}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    CuberemapContext *cuberemap = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(outlink->format);
    int ret;

    cuberemap->width = inlink->w;
    cuberemap->height = inlink->h / 2;
    outlink->w = cuberemap->width;
    outlink->h = cuberemap->height;

    if ((ret = av_image_fill_linesizes(cuberemap->linesize, outlink->format, cuberemap->width)) < 0)
        return ret;
    cuberemap->nb_planes = av_pix_fmt_count_planes(outlink->format);
    av_image_fill_max_pixsteps(cuberemap->pixstep, NULL, desc);
    cuberemap->pheight[1] = cuberemap->pheight[2] = FF_CEIL_RSHIFT(cuberemap->height, desc->log2_chroma_h);
    cuberemap->pheight[0] = cuberemap->pheight[3] = cuberemap->height;
    cuberemap->hsub = desc->log2_chroma_w;
    cuberemap->vsub = desc->log2_chroma_h;

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    CuberemapContext *cuberemap = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    int out_off_left[4], out_off_right[4];
    int i, direct = 0;

    if (av_frame_is_writable(in)) {
        direct = 1;
        out = in;
    } else {
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);
    }

    // do the stuff

    for (i = 0; i < 4; i++) {
        int hsub = i == 1 || i == 2 ? cuberemap->hsub : 0;
        int vsub = i == 1 || i == 2 ? cuberemap->vsub : 0;
        cuberemap->in_off_right[i]  =
            (FF_CEIL_RSHIFT(50,  vsub) + 0)
            * in->linesize[i] + FF_CEIL_RSHIFT(0  * cuberemap->pixstep[i], hsub);
        out_off_right[i] =
            (FF_CEIL_RSHIFT(50, vsub) + 0)
            * out->linesize[i] + FF_CEIL_RSHIFT(0 * cuberemap->pixstep[i], hsub);
    }

    for (i = 0; i < cuberemap->nb_planes; i++) {
        av_image_copy_plane(out->data[i] + out_off_right[i],
                            out->linesize[i],
                            in->data[i] + cuberemap->in_off_right[i],
                            in->linesize[i],
                            cuberemap->linesize[i], cuberemap->pheight[i] / 2);
        // av_image_copy_plane(out->data[i], out->linesize[i],
        //                     in->data[i], in->linesize[i],
        //                     cuberemap->linesize[i], cuberemap->pheight[i]);
    }

    if (!direct)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static const AVFilterPad avfilter_vf_cuberemap_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_cuberemap_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
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
