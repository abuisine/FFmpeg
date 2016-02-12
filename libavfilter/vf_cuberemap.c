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
    int i_x;
    int i_y;
    int o_x;
    int o_y;
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
    int sampling[4];
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
    CuberemapContext *cr = ctx->priv;

    cr->nb_faces = 1;
    cr->faces.i_x = 0;
    cr->faces.i_y = 0;
    cr->faces.o_x = 200;
    cr->faces.o_y = 200;
    cr->faces.w = 400;
    cr->faces.h = 400;

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    CuberemapContext *cr = ctx->priv;

}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    CuberemapContext *cr = ctx->priv;;

    cr->width = inlink->w;
    cr->height = inlink->h / 2;

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    CuberemapContext *cr = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(outlink->format);
    int ret;

    outlink->w = cr->width;
    outlink->h = cr->height;

    if ((ret = av_image_fill_linesizes(cr->linesize, outlink->format, cr->width)) < 0)
        return ret;
    cr->nb_planes = av_pix_fmt_count_planes(outlink->format);
    av_image_fill_max_pixsteps(cr->pixstep, NULL, desc);
    cr->pheight[1] = cr->pheight[2] = FF_CEIL_RSHIFT(cr->height, desc->log2_chroma_h);
    cr->pheight[0] = cr->pheight[3] = cr->height;
    cr->sampling[1] = cr->sampling[2] = desc->log2_chroma_h;
    cr->sampling[0] = cr->sampling[3] = 1;
    cr->hsub = desc->log2_chroma_w;
    cr->vsub = desc->log2_chroma_h;

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    CuberemapContext *cr = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    int out_off_left[4], out_off_right[4];
    int i, direct = 0;
    int hsub, vsub = 0;

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




    // for (i = 0; i < cr->nb_planes; i++) {
        av_image_copy_plane(out->data[0] + cr->faces.o_y * out->linesize[0] + cr->faces.o_x, out->linesize[0],
                            in->data[0] + cr->faces.i_y * in->linesize[0], in->linesize[0],
                            cr->faces.w, cr->faces.h);

        // i = 2;
        // hsub = cr->hsub;
        // vsub = cr->vsub ;
        // cr->in_off_right[i]  = (FF_CEIL_RSHIFT(offset,  vsub) + 0)
        //     * in->linesize[i] + FF_CEIL_RSHIFT(0  * cr->pixstep[i], hsub);
        // out_off_right[i] = (FF_CEIL_RSHIFT(offset, vsub) + 0)
        //     * out->linesize[i] + FF_CEIL_RSHIFT(0 * cr->pixstep[i], hsub);
        // av_image_copy_plane(out->data[i] + out_off_right[i],
        //                     out->linesize[i],
        //                     in->data[i] + cr->in_off_right[i],
        //                     in->linesize[i],
        //                     cr->linesize[i], cr->pheight[i] - offset * 2);

        // av_image_copy_plane(out->data[3] + offset * out->linesize[3], out->linesize[3],
        //                     in->data[3] + offset * in->linesize[3], in->linesize[3],
        //                     cr->linesize[3], cr->height/2);
        // i = 1;
        // hsub = cr->hsub;
        // vsub = cr->vsub;
        // cr->in_off_right[i]  = (FF_CEIL_RSHIFT(offset,  vsub) + 0)
        //     * in->linesize[i] + FF_CEIL_RSHIFT(0  * cr->pixstep[i], hsub);
        // out_off_right[i] = (FF_CEIL_RSHIFT(offset, vsub) + 0)
        //     * out->linesize[i] + FF_CEIL_RSHIFT(0 * cr->pixstep[i], hsub);
        // av_image_copy_plane(out->data[i] + out_off_right[i],
        //                     out->linesize[i],
        //                     in->data[i] + cr->in_off_right[i],
        //                     in->linesize[i],
        //                     cr->linesize[i], cr->pheight[i] - offset * 2);



    if (!direct)
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
