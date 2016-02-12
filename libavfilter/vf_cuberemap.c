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

typedef struct TransformContext {
    const AVClass *class;
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
    AVFilterContext *ctx = inlink->dst;
    CuberemapContext *cuberemap = ctx->priv;

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    CuberemapContext *cuberemap = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    int direct = 0;

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

    if (!direct)
        av_frame_free(&in);
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
