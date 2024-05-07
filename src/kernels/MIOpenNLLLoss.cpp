/*******************************************************************************
 *
 * MIT License
 *
 * Copyright (c) 2024 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *******************************************************************************/
#ifndef MIOPEN_DONT_USE_HIP_RUNTIME_HEADERS
#include <hip/hip_fp16.h>
#include <hip/hip_runtime.h>
#endif

#include "float_types.h"
#include "tensor_view.hpp"

template <typename TI, typename TO>
__device__ void nlllossUnreducedForward4dContiguous(const TI* __restrict__ input,
                                                    const int32_t* __restrict__ target,
                                                    const TI* weight,
                                                    TO* __restrict__ output,
                                                    int32_t ignore_index,
                                                    size_t N_total,
                                                    size_t C,
                                                    size_t D1,
                                                    size_t D2)
{
    uint64_t gid = threadIdx.x + blockIdx.x * blockDim.x;

    if(gid >= N_total)
        return;

    size_t NWH[3];
    NWH[2]    = (gid) % D2;
    size_t nc = (gid) / D2;
    NWH[1]    = nc % D1;
    NWH[0]    = nc / D1;

    int32_t t = target[gid];
    if(t < 0 || t == ignore_index || t >= C)
    {
        output[gid] = static_cast<TO>(0);
        return;
    }

    FLOAT_ACCUM w = weight != nullptr ? CVT_FLOAT2ACCUM(weight[t]) : CVT_FP32_2ACCUM(1.0f);

    uint32_t input_offset   = (NWH[0] * C + t) * D1 * D2 + NWH[1] * D2 + NWH[2];
    FLOAT_ACCUM input_value = CVT_FLOAT2ACCUM(input[input_offset]);

    FLOAT_ACCUM val = CVT_FP32_2ACCUM(-1.0f) * w * input_value;
    output[gid]     = CVT_ACCUM2FLOAT(val);
}

extern "C" __global__ void NLLLossUnreducedForward4dContiguous(const INPUT_TYPE* __restrict__ input,
                                                               const int32_t* __restrict__ target,
                                                               const INPUT_TYPE* weight,
                                                               OUTPUT_TYPE* __restrict__ output,
                                                               int32_t ignore_index,
                                                               size_t N_total,
                                                               size_t C,
                                                               size_t D1,
                                                               size_t D2)
{
    nlllossUnreducedForward4dContiguous<INPUT_TYPE, OUTPUT_TYPE>(
        input, target, weight, output, ignore_index, N_total, C, D1, D2);
}

template <typename TI, typename TO>
__device__ void nlllossUnreducedForward4d(const TI* __restrict__ input,
                                          const int32_t* __restrict__ target,
                                          const TI* weight,
                                          TO* __restrict__ output,
                                          int32_t ignore_index,
                                          tensor_view_4d_t input_tv,
                                          tensor_view_3d_t target_tv,
                                          tensor_view_1d_t weight_tv,
                                          tensor_view_3d_t output_tv)
{
    uint64_t gid = threadIdx.x + blockIdx.x * blockDim.x;

    size_t n[3];
    GET_NCD(n[0], n[1], n[2], gid, output_tv);

    // printf("gid: %d, n[0]: %d, n[1]: %d, n[2]: %d, output_tv.size[0]: %d, output_tv.size[1]: %d, output_tv.size[2]: %d\n", gid, n[0], n[1], n[2], output_tv.size[0], output_tv.size[1], output_tv.size[2]);

    size_t max_gid = output_tv.size[0] * output_tv.size[1] * output_tv.size[2];
    if(gid >= max_gid)
        return;

    size_t Tidx = TV3D_IDX(target_tv, n[0], n[1], n[2]);
    size_t Oidx = TV3D_IDX(output_tv, n[0], n[1], n[2]);

    int32_t C = weight_tv.size[0];
    int32_t t   = target[Tidx];
    size_t Iidx = TV4D_IDX(input_tv, n[0], t, n[1], n[2]);
    size_t Widx = TV1D_IDX(weight_tv, t);

    if(t < 0 || t == ignore_index || t >= C)
    {
        output[Oidx] = static_cast<TO>(0);
        return;
    }

    FLOAT_ACCUM w = weight != nullptr ? CVT_FLOAT2ACCUM(weight[Widx]) : CVT_FP32_2ACCUM(1.0f);

    FLOAT_ACCUM input_value = CVT_FLOAT2ACCUM(input[Iidx]);

    FLOAT_ACCUM val = CVT_FP32_2ACCUM(-1.0f) * w * input_value;
    output[Oidx]    = CVT_ACCUM2FLOAT(val);
}

extern "C" __global__ void NLLLossUnreducedForward4d(const INPUT_TYPE* __restrict__ input,
                                                     const int32_t* __restrict__ target,
                                                     const INPUT_TYPE* weight,
                                                     OUTPUT_TYPE* __restrict__ output,
                                                     int32_t ignore_index,
                                                     tensor_view_4d_t input_tv,
                                                     tensor_view_3d_t target_tv,
                                                     tensor_view_1d_t weight_tv,
                                                     tensor_view_3d_t output_tv)
{
    nlllossUnreducedForward4d<INPUT_TYPE, OUTPUT_TYPE>(
        input, target, weight, output, ignore_index, input_tv, target_tv, weight_tv, output_tv);
}

template <typename TI, typename TO>
__device__ void nlllossUnreducedBackward4dContiguous(TO* __restrict__ input_grad,
                                                     const int32_t* __restrict__ target,
                                                     const TI* weight,
                                                     TI* __restrict__ output_grad,
                                                     int32_t ignore_index,
                                                     size_t N_total,
                                                     size_t C,
                                                     size_t D1,
                                                     size_t D2)
{
    uint64_t gid = threadIdx.x + blockIdx.x * blockDim.x;

    if(gid >= N_total)
        return;

    size_t NWH[3];
    NWH[2]    = (gid) % D2;
    size_t nc = (gid) / D2;
    NWH[1]    = nc % D1;
    NWH[0]    = nc / D1;

    int32_t t             = target[gid];
    uint32_t input_offset = (NWH[0] * C + t) * D1 * D2 + NWH[1] * D2 + NWH[2];
    if(t < 0 || t == ignore_index || t >= C)
    {
        input_grad[input_offset] = static_cast<TO>(0);
        return;
    }

    FLOAT_ACCUM w        = weight != nullptr ? CVT_FLOAT2ACCUM(weight[t]) : CVT_FP32_2ACCUM(1.0f);
    FLOAT_ACCUM grad_val = CVT_FLOAT2ACCUM(output_grad[gid]);
    FLOAT_ACCUM input_grad_value = CVT_FP32_2ACCUM(-1.0f) * w * grad_val;

    input_grad[input_offset] = CVT_ACCUM2FLOAT(input_grad_value);
}

extern "C" __global__ void
NLLLossUnreducedBackward4dContiguous(INPUT_TYPE* __restrict__ input_grad,
                                     const int32_t* __restrict__ target,
                                     const INPUT_TYPE* weight,
                                     OUTPUT_TYPE* __restrict__ output_grad,
                                     int32_t ignore_index,
                                     size_t N_total,
                                     size_t C,
                                     size_t D1,
                                     size_t D2)
{
    nlllossUnreducedBackward4dContiguous<INPUT_TYPE, OUTPUT_TYPE>(
        input_grad, target, weight, output_grad, ignore_index, N_total, C, D1, D2);
}

template <typename TI, typename TO>
__device__ void nlllossUnreducedBackward4d(TO* __restrict__ input_grad,
                                           const int32_t* __restrict__ target,
                                           const TI* weight,
                                           TI* __restrict__ output_grad,
                                           int32_t ignore_index,
                                           tensor_view_4d_t input_grad_tv,
                                           tensor_view_3d_t target_tv,
                                           tensor_view_1d_t weight_tv,
                                           tensor_view_3d_t output_grad_tv)
{
    uint64_t gid = threadIdx.x + blockIdx.x * blockDim.x;

    size_t n[3];
    GET_NCD(n[0], n[1], n[2], gid, output_grad_tv);

    if(n[0] >= output_grad_tv.size[0])
        return;

    size_t Tidx = TV3D_IDX(target_tv, n[0], n[1], n[2]);
    size_t Oidx = TV3D_IDX(output_grad_tv, n[0], n[1], n[2]);

    int32_t C = weight_tv.size[0];
    int32_t t   = target[Tidx];
    size_t Iidx = TV4D_IDX(input_grad_tv, n[0], t, n[1], n[2]);
    size_t Widx = TV1D_IDX(weight_tv, t);

    if(t < 0 || t == ignore_index || t >= C)
    {
        input_grad[Iidx] = static_cast<TO>(0);
        return;
    }

    FLOAT_ACCUM w = weight != nullptr ? CVT_FLOAT2ACCUM(weight[Widx]) : CVT_FP32_2ACCUM(1.0f);
    FLOAT_ACCUM grad_val         = CVT_FLOAT2ACCUM(output_grad[Oidx]);
    FLOAT_ACCUM input_grad_value = CVT_FP32_2ACCUM(-1.0f) * w * grad_val;

    input_grad[Iidx] = CVT_ACCUM2FLOAT(input_grad_value);
}

extern "C" __global__ void NLLLossUnreducedBackward4d(INPUT_TYPE* __restrict__ input_grad,
                                                      const int32_t* __restrict__ target,
                                                      const INPUT_TYPE* weight,
                                                      OUTPUT_TYPE* __restrict__ output_grad,
                                                      int32_t ignore_index,
                                                      tensor_view_4d_t input_grad_tv,
                                                      tensor_view_3d_t target_tv,
                                                      tensor_view_1d_t weight_tv,
                                                      tensor_view_3d_t output_grad_tv)
{
    nlllossUnreducedBackward4d<INPUT_TYPE, OUTPUT_TYPE>(input_grad,
                                                        target,
                                                        weight,
                                                        output_grad,
                                                        ignore_index,
                                                        input_grad_tv,
                                                        target_tv,
                                                        weight_tv,
                                                        output_grad_tv);
}