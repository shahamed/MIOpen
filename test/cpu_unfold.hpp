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
#pragma once

#include <miopen/tensor.hpp>
#include "tensor_holder.hpp"
#include <miopen/tensor_view_utils.hpp>

template <class T>
void cpu_unfold_fwd_4d(tensor<T> input_tensor,
                       tensor<T>& ref_output_tensor,
                       const std::vector<uint64_t> kernel_size,
                       const std::vector<uint64_t> stride,
                       const std::vector<uint64_t> padding,
                       const std::vector<uint64_t> dilation)
{
    auto input_tv   = miopen::get_inner_expanded_tv<4>(input_tensor.desc);
    auto output_tv  = miopen::get_inner_expanded_tv<3>(ref_output_tensor.desc);
    auto input_size = input_tensor.desc.GetNumDims();
    auto input_dims = input_tensor.desc.GetLengths();

    auto input  = input_tensor.data.data();
    auto output = ref_output_tensor.data.data();

    const uint64_t LOCAL_SIZE = 256;
    uint64_t spatial_dim_size = input_size - 2;

    const uint64_t N = static_cast<uint64_t>(input_dims[0]);
    const uint64_t C = static_cast<uint64_t>(input_dims[1]);

    uint64_t P = 1, L = 1;
    std::vector<uint64_t> ls;
    for(uint64_t i = 0; i < spatial_dim_size; ++i)
    {
        P *= kernel_size[i];
        uint64_t l = (static_cast<uint64_t>(input_dims[i + 2]) + 2 * padding[i] -
                      dilation[i] * (kernel_size[i] - 1) - 1) /
                         stride[i] +
                     1;
        L *= l;
        ls.push_back(l);
    }

    uint64_t kernel_size_w = kernel_size[1];
    uint64_t stride_h      = stride[0];
    uint64_t stride_w      = stride[1];
    uint64_t padding_h     = padding[0];
    uint64_t padding_w     = padding[1];
    uint64_t dilation_h    = dilation[0];
    uint64_t dilation_w    = dilation[1];
    uint64_t LW            = ls[1];
    uint64_t H             = static_cast<uint64_t>(input_dims[2]);
    uint64_t W             = static_cast<uint64_t>(input_dims[3]);
    uint64_t work_size     = (((N * C * P * L) + LOCAL_SIZE - 1) / LOCAL_SIZE) * LOCAL_SIZE;
    par_ford(work_size)([&](uint64_t gid) {
        uint64_t ncp = gid / L, l = gid % L;
        uint64_t nc = ncp / P, p = ncp % P;
        uint64_t n = nc / C, c = nc % C;
        if(n >= N)
            return;

        int64_t lh = l / LW, lw = l % LW;                       // sliding window position
        int64_t ph = p / kernel_size_w, pw = p % kernel_size_w; // position inside kernel
        int64_t h = lh * stride_h - padding_h + ph * dilation_h;
        int64_t w = lw * stride_w - padding_w + pw * dilation_w;

        T x = static_cast<T>(0.0f);
        if(0 <= h && h < H && 0 <= w && w < W)
        {
            long input_idx = input_tv.stride[3] * w + input_tv.stride[2] * h +
                             input_tv.stride[1] * c + input_tv.stride[0] * n;
            x = input[input_idx];
        }

        long output_idx =
            output_tv.stride[2] * l + output_tv.stride[1] * (c * P + p) + output_tv.stride[0] * n;
        output[output_idx] = x;
    });
}

template <class T>
void cpu_unfold_bwd_4d(tensor<T>& ref_dinput_tensor,
                       tensor<T> doutput_tensor,
                       const std::vector<uint64_t> kernel_size,
                       const std::vector<uint64_t> stride,
                       const std::vector<uint64_t> padding,
                       const std::vector<uint64_t> dilation)
{
    auto input_grad_tv   = miopen::get_inner_expanded_tv<4>(ref_dinput_tensor.desc);
    auto output_grad_tv  = miopen::get_inner_expanded_tv<3>(doutput_tensor.desc);
    auto input_size      = ref_dinput_tensor.desc.GetNumDims();
    auto input_grad_dims = ref_dinput_tensor.desc.GetLengths();

    auto input_grad  = ref_dinput_tensor.data.data();
    auto output_grad = doutput_tensor.data.data();

    const uint64_t LOCAL_SIZE = 256;
    uint64_t spatial_dim_size = input_size - 2;

    const uint64_t N = static_cast<uint64_t>(input_grad_dims[0]);
    const uint64_t C = static_cast<uint64_t>(input_grad_dims[1]);

    uint64_t P = 1;
    std::vector<uint64_t> ls;
    for(uint64_t i = 0; i < spatial_dim_size; ++i)
    {
        P *= kernel_size[i];
        uint64_t l = (static_cast<uint64_t>(input_grad_dims[i + 2]) + 2 * padding[i] -
                      dilation[i] * (kernel_size[i] - 1) - 1) /
                         stride[i] +
                     1;
        ls.push_back(l);
    }

    uint64_t kernel_size_h = kernel_size[0];
    uint64_t kernel_size_w = kernel_size[1];
    uint64_t stride_h      = stride[0];
    uint64_t stride_w      = stride[1];
    uint64_t padding_h     = padding[0];
    uint64_t padding_w     = padding[1];
    uint64_t dilation_h    = dilation[0];
    uint64_t dilation_w    = dilation[1];
    uint64_t LH            = ls[0];
    uint64_t LW            = ls[1];
    uint64_t H             = static_cast<uint64_t>(input_grad_dims[2]);
    uint64_t W             = static_cast<uint64_t>(input_grad_dims[3]);
    uint64_t work_size     = (((N * C * H * W) + LOCAL_SIZE - 1) / LOCAL_SIZE) * LOCAL_SIZE;
    par_ford(work_size)([&](uint64_t gid) {
        uint64_t nch = gid / W, w = gid % W;
        uint64_t nc = nch / H, h = nch % H;
        uint64_t n = nc / C, c = nc % C;
        if(n >= N)
            return;

        float sum = 0.0f;

        for(uint64_t ph = 0; ph < kernel_size_h; ++ph)
        {
            for(uint64_t pw = 0; pw < kernel_size_w; ++pw)
            {
                int64_t lhsh = h - ph * dilation_h + padding_h;
                int64_t lwsw = w - pw * dilation_w + padding_w;
                if(lhsh % stride_h != 0)
                    continue;
                if(lwsw % stride_w != 0)
                    continue;
                int64_t lh = lhsh / stride_h;
                int64_t lw = lwsw / stride_w;
                if(lh < 0 || LH <= lh)
                    continue;
                if(lw < 0 || LW <= lw)
                    continue;
                long output_grad_idx =
                    output_grad_tv.stride[2] * (lh * LW + lw) +
                    output_grad_tv.stride[1] * (c * P + (ph * kernel_size_w + pw)) +
                    output_grad_tv.stride[0] * n;
                sum += static_cast<float>(output_grad[output_grad_idx]);
            }
        }

        long input_grad_idx = input_grad_tv.stride[3] * w + input_grad_tv.stride[2] * h +
                              input_grad_tv.stride[1] * c + input_grad_tv.stride[0] * n;
        input_grad[input_grad_idx] = static_cast<T>(sum);
    });
}