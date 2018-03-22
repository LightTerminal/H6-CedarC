/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "vp9hwd_decoder.h"
#include "vp9hwd_treecoder.h"

#define MV_COUNT_SAT 20
#define MV_MAX_UPDATE_FACTOR 128

const vp9_tree_index vp9hwd_mv_joint_tree[2 * MV_JOINTS - 2] = {
    -MV_JOINT_ZERO, 2, -MV_JOINT_HNZVZ, 4, -MV_JOINT_HZVNZ, -MV_JOINT_HNZVNZ};

const vp9_tree_index vp9hwd_mv_class_tree[2 * MV_CLASSES - 2] = {
    -MV_CLASS_0, 2,           -MV_CLASS_1, 4,           6,
    8,           -MV_CLASS_2, -MV_CLASS_3, 10,          12,
    -MV_CLASS_4, -MV_CLASS_5, -MV_CLASS_6, 14,          16,
    18,          -MV_CLASS_7, -MV_CLASS_8, -MV_CLASS_9, -MV_CLASS_10, };

const struct NmvContext vp9_default_nmv_context = {
    {32, 64, 96},                 /* joints */
    {128, 128},                   /* sign */
    {{216}, {208}},               /* class0 */
    {{64, 96, 64}, {64, 96, 64}}, /* fp */
    {160, 160},                   /* class0_hp bit */
    {128, 128},                   /* hp */
    {{224, 144, 192, 168, 192, 176, 192, 198, 198, 245},
     {216, 128, 176, 160, 176, 176, 192, 198, 198, 208}}, /* class */
    {{{128, 128, 64}, {96, 112, 64}},
     {{128, 128, 64}, {96, 112, 64}}}, /* class0_fp */
    {{136, 140, 148, 160, 176, 192, 224, 234, 234, 240},
     {136, 140, 148, 160, 176, 192, 224, 234, 234, 240}}, /* bits */
};

void Vp9InitMvProbs(struct Vp9Decoder *x)
{
    memcpy(&x->entropy.a.nmvc, &vp9_default_nmv_context,
         sizeof(struct NmvContext));
}

static void AdaptProb(vp9_prob *dest, vp9_prob prep, unsigned int ct[2])
{
    #define MIN(a, b) (((a) < (b)) ? (a) : (b))
    const int count = MIN(ct[0] + ct[1], MV_COUNT_SAT);
    if(count)
    {
        const vp9_prob newp = GetBinaryProb(ct[0], ct[1]);
        const int factor = MV_MAX_UPDATE_FACTOR * count / MV_COUNT_SAT;
        *dest = WeightedProb(prep, newp, factor);
    }
    else
    {
        *dest = prep;
    }
}

static unsigned int AdaptProbs(unsigned int i, vp9_tree tree,
                               vp9_prob this_probs[],
                               const vp9_prob last_probs[],
                               const unsigned int num_events[])
{
    vp9_prob this_prob;

    const u32 left = tree[i] <= 0 ? num_events[-tree[i]]
                         : AdaptProbs(tree[i], tree, this_probs,
                         last_probs, num_events);
    const u32 right = tree[i + 1] <= 0 ? num_events[-tree[i + 1]]
                        : AdaptProbs(tree[i + 1], tree, this_probs,
                        last_probs, num_events);

    u32 weight = left + right;
    if(weight)
    {
        this_prob = GetBinaryProb(left, right);
        weight = weight > MV_COUNT_SAT ? MV_COUNT_SAT : weight;
        this_prob = WeightedProb(last_probs[i >> 1], this_prob,
                         MV_MAX_UPDATE_FACTOR * weight / MV_COUNT_SAT);
    }
    else
    {
        this_prob = last_probs[i >> 1];
    }
    this_probs[i >> 1] = this_prob;
    return left + right;
}

const vp9_tree_index vp9hwd_mv_class0_tree[2 * CLASS0_SIZE - 2] = {-0, -1, };

const vp9_tree_index vp9hwd_mv_fp_tree[2 * 4 - 2] = {-0, 2, -1, 4, -2, -3};

void Vp9AdaptNmvProbs(struct Vp9Decoder *cm)
{
    s32 usehp = cm->allow_high_precision_mv;
    s32 i, j;

    AdaptProbs(0, vp9hwd_mv_joint_tree, cm->entropy.a.nmvc.joints,
             cm->prev_ctx.nmvc.joints, cm->ctx_ctr.nmvcount.joints);
    for(i = 0; i < 2; ++i)
    {
        AdaptProb(&cm->entropy.a.nmvc.sign[i], cm->prev_ctx.nmvc.sign[i],
              cm->ctx_ctr.nmvcount.sign[i]);
        AdaptProbs(0, vp9hwd_mv_class_tree, cm->entropy.a.nmvc.classes[i],
               cm->prev_ctx.nmvc.classes[i], cm->ctx_ctr.nmvcount.classes[i]);
        AdaptProbs(0, vp9hwd_mv_class0_tree, cm->entropy.a.nmvc.class0[i],
               cm->prev_ctx.nmvc.class0[i], cm->ctx_ctr.nmvcount.class0[i]);
        for(j = 0; j < MV_OFFSET_BITS; ++j)
        {
            AdaptProb(&cm->entropy.a.nmvc.bits[i][j], cm->prev_ctx.nmvc.bits[i][j],
                cm->ctx_ctr.nmvcount.bits[i][j]);
        }
    }

    for(i = 0; i < 2; ++i)
    {
        for(j = 0; j < CLASS0_SIZE; ++j)
        {
            AdaptProbs(0, vp9hwd_mv_fp_tree, cm->entropy.a.nmvc.class0_fp[i][j],
                 cm->prev_ctx.nmvc.class0_fp[i][j],
                 cm->ctx_ctr.nmvcount.class0_fp[i][j]);
        }
        AdaptProbs(0, vp9hwd_mv_fp_tree, cm->entropy.a.nmvc.fp[i],
               cm->prev_ctx.nmvc.fp[i], cm->ctx_ctr.nmvcount.fp[i]);
    }

    if(usehp)
    {
        for(i = 0; i < 2; ++i)
        {
            AdaptProb(&cm->entropy.a.nmvc.class0_hp[i],
                cm->prev_ctx.nmvc.class0_hp[i],
                cm->ctx_ctr.nmvcount.class0_hp[i]);
            AdaptProb(&cm->entropy.a.nmvc.hp[i], cm->prev_ctx.nmvc.hp[i],
                cm->ctx_ctr.nmvcount.hp[i]);
        }
    }
}
