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

#include "../driver/tensor_driver.hpp"
#include "cpu_where.hpp"
#include "get_handle.hpp"
#include "miopen/allocator.hpp"
#include "random.hpp"
#include "tensor_holder.hpp"
#include "verify.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <gtest/gtest.h>
#include <miopen/miopen.h>
#include <miopen/where.hpp>
#include <numeric>
#include <random>
#include <vector>

struct WhereTestCase
{
    std::vector<size_t> inDims;
    std::vector<size_t> otherDims;
    std::vector<size_t> condDims;
    friend std::ostream& operator<<(std::ostream& os, const WhereTestCase& tc)
    {
        os << "Input dim: ";
        for (auto inDim : tc.inDims) {
            os << inDim << " ";
        }
        os << std::endl;
        os << "Other dim: ";
        for (auto otherDim : tc.otherDims) {
            os << otherDim << " ";
        }
        os << std::endl;
        os << "Cond dim: ";
        for (auto condDim : tc.condDims) {
            os << condDim << " ";
        }
        os << std::endl;
        return os;
    }

    std::vector<size_t> GetInputDim()
    {
        std::vector<size_t> res;
        if (inDims.empty()) {
            return std::vector<size_t>{0};
        }
        for (auto inDim : inDims) {
            if (inDim != 0) {
                res.push_back(inDim);
            }
        }
        return res;
    }

    std::vector<size_t> GetOtherDim()
    {
        std::vector<size_t> res;
        if (otherDims.empty()) {
            return std::vector<size_t>{0};
        }
        for (auto otherDim : otherDims) {
            if (otherDim != 0) {
                res.push_back(otherDim);
            }
        }
        return res;
    }

    std::vector<size_t> GetCondDim()
    {
        std::vector<size_t> res;
        if (condDims.empty()) {
            return std::vector<size_t>{0};
        }
        for (auto condDim : condDims) {
            if (condDim != 0) {
                res.push_back(condDim);
            }
        }
        return res;
    }    
};

std::vector<WhereTestCase> WhereTestConfigs()
{ // n c d h w dim
    // clang-format off
    return {
        {std::vector<size_t>{1, 2, 8, 2, 2}, std::vector<size_t>{1, 2, 8, 2, 2}, std::vector<size_t>{1, 2, 8, 2, 2}},
        {std::vector<size_t>{6, 2, 2, 2, 2}, std::vector<size_t>{1, 2, 2, 2, 2}, std::vector<size_t>{1, 2, 2, 2, 2}},
        {std::vector<size_t>{1, 2, 2, 2, 2}, std::vector<size_t>{1, 2, 2, 2, 2}, std::vector<size_t>{4, 2, 2, 2, 2}},
        {std::vector<size_t>{ 2, 2, 2, 2}, std::vector<size_t>{1, 2, 2, 2}, std::vector<size_t>{1, 2, 2, 2, 1}},
        {std::vector<size_t>{1, 2, 1, 1, 1}, std::vector<size_t>{6, 2, 1, 1, 1}, std::vector<size_t>{6, 2}}
    };
    // clang-format on
}

template <typename T>
struct WhereFwdTest : public ::testing::TestWithParam<WhereTestCase>
{
protected:
    void SetUp() override
    {

        auto&& handle  = get_handle();
        where_config     = GetParam();
        auto gen_value = [](auto...) { return prng::gen_descreet_uniform_sign<T>(1e-2, 100); };

        auto in_dims = where_config.GetInputDim();
        auto other_dims = where_config.GetOtherDim();
        auto cond_dims = where_config.GetCondDim();

        input = tensor<T>{in_dims}.generate(gen_value);
        other = tensor<T>{other_dims}.generate(gen_value);
        cond  = tensor<T>{cond_dims}.generate(gen_value);

        for (auto i = 0; i < cond.GetSize(); i++) {
            if (cond[i] > 0.5) {
                cond[i] = 1;
            } else {
                cond[i] = 0;
            }
        }

        size_t out_sz = std::max({in_dims.size(), other_dims.size(), cond_dims.size()});
        int in_sz = in_dims.size();
        int other_sz = other_dims.size();
        int cond_sz = cond_dims.size();
        std::vector<size_t> out_dims(out_sz);
        for (int i = 0; i < out_sz; i++) {
            size_t in_dim = (i < in_sz) ? in_dims[i] : 1;
            size_t other_dim = (i < other_sz) ? other_dims[i] : 1;
            size_t cond_dim = (i < cond_sz) ? cond_dims[i] : 1;
            out_dims[i] = std::max({in_dim, other_dim, cond_dim});
        }

        output = tensor<T>{out_dims};
        std::fill(output.begin(), output.end(), std::numeric_limits<T>::quiet_NaN());

        ref_output = tensor<T>{out_dims};
        std::fill(ref_output.begin(), ref_output.end(), std::numeric_limits<T>::quiet_NaN());

        input_dev  = handle.Write(input.data);
        other_dev = handle.Write(other.data);
        cond_dev   = handle.Write(cond.data);
        output_dev = handle.Write(output.data);
    }

    void RunTest()
    {
        auto&& handle = get_handle();

        cpu_where_forward<T>(input, other, cond, ref_output);
        miopenStatus_t status;

        status = miopen::WhereForward(
            handle, input.desc, input_dev.get(), other.desc, other_dev.get(), cond.desc, cond_dev.get(), output.desc, output_dev.get());

        EXPECT_EQ(status, miopenStatusSuccess);

        output.data = handle.Read<T>(output_dev, output.data.size());
    }

    double GetTolerance()
    {
        // Computation error of fp16 is ~2^13 (=8192) bigger than
        // the one of fp32 because mantissa is shorter by 13 bits.
        double tolerance = std::is_same<T, float>::value ? 1.5e-6 : 8.2e-3;

        // bf16 mantissa has 7 bits, by 3 bits shorter than fp16.
        if(std::is_same<T, bfloat16>::value)
            tolerance *= 8.0;
        return tolerance;
    }

    void Verify()
    {
        double threshold = GetTolerance();
        auto error       = miopen::rms_range(ref_output, output);

        EXPECT_TRUE(miopen::range_distance(ref_output) == miopen::range_distance(output));
        EXPECT_TRUE(error < threshold * 10) << "Error output beyond tolerance Error:" << error
                                            << ",  Thresholdx10: " << threshold * 10;
    }
    WhereTestCase where_config;

    tensor<T> input;
    tensor<T> other;
    tensor<T> cond;
    tensor<T> output;

    tensor<T> ref_output;

    miopen::Allocator::ManageDataPtr input_dev;
    miopen::Allocator::ManageDataPtr other_dev;
    miopen::Allocator::ManageDataPtr cond_dev;
    miopen::Allocator::ManageDataPtr output_dev;
};

template <typename T>
struct WhereBwdTest : public ::testing::TestWithParam<WhereTestCase>
{
protected:
    void SetUp() override
    {
        auto&& handle  = get_handle();
        where_config     = GetParam();
        auto gen_value = [](auto...) { return prng::gen_descreet_uniform_sign<T>(1e-2, 100); };

        auto in_dims = where_config.GetInputDim();
        auto other_dims = where_config.GetOtherDim();
        auto cond_dims = where_config.GetCondDim();

        cond  = tensor<T>{cond_dims}.generate(gen_value);
        for (auto i = 0; i < cond.GetSize(); i++) {
            if (cond[i] > 0.5) {
                cond[i] = 1;
            } else {
                cond[i] = 0;
            }
        }

        size_t out_sz = std::max({in_dims.size(), other_dims.size(), cond_dims.size()});
        int in_sz = in_dims.size();
        int other_sz = other_dims.size();
        int cond_sz = cond_dims.size();
        std::vector<size_t> out_dims(out_sz);
        for (int i = 0; i < out_sz; i++) {
            size_t in_dim = (i < in_sz) ? in_dims[i] : 1;
            size_t other_dim = (i < other_sz) ? other_dims[i] : 1;
            size_t cond_dim = (i < cond_sz) ? cond_dims[i] : 1;
            out_dims[i] = std::max({in_dim, other_dim, cond_dim});
        }

        outputGrad = tensor<T>{out_dims}.generate(gen_value);

        inputGrad = tensor<T>{in_dims};
        std::fill(inputGrad.begin(), inputGrad.end(), std::numeric_limits<T>::quiet_NaN());

        ref_inputGrad = tensor<T>{in_dims};
        std::fill(ref_inputGrad.begin(), ref_inputGrad.end(), std::numeric_limits<T>::quiet_NaN());

        otherGrad = tensor<T>{other_dims};
        std::fill(otherGrad.begin(), otherGrad.end(), std::numeric_limits<T>::quiet_NaN());

        ref_otherGrad = tensor<T>{other_dims};
        std::fill(ref_otherGrad.begin(), ref_otherGrad.end(), std::numeric_limits<T>::quiet_NaN());

        inputGrad_dev  = handle.Write(inputGrad.data);
        otherGrad_dev  = handle.Write(otherGrad.data);
        cond_dev       = handle.Write(cond.data);
        outputGrad_dev = handle.Write(outputGrad.data);
    }

    void RunTest()
    {
        auto&& handle = get_handle();

        cpu_where_backward<T>(outputGrad, cond, ref_inputGrad, ref_otherGrad);
        miopenStatus_t status;

        status = miopen::WhereBackward(handle,
                                     outputGrad.desc,
                                     outputGrad_dev.get(),
                                     cond.desc,
                                     cond_dev.get(),
                                     inputGrad.desc,
                                     inputGrad_dev.get(),
                                     otherGrad.desc,
                                     otherGrad_dev.get());

        EXPECT_EQ(status, miopenStatusSuccess);

        inputGrad.data = handle.Read<T>(inputGrad_dev, inputGrad.data.size());
        otherGrad.data = handle.Read<T>(otherGrad_dev, otherGrad.data.size());
    }

    double GetTolerance()
    {
        // Computation error of fp16 is ~2^13 (=8192) bigger than
        // the one of fp32 because mantissa is shorter by 13 bits.
        double tolerance = std::is_same<T, float>::value ? 1.5e-6 : 8.2e-3;

        // bf16 mantissa has 7 bits, by 3 bits shorter than fp16.
        if(std::is_same<T, bfloat16>::value)
            tolerance *= 8.0;
        return tolerance;
    }

    void Verify()
    {
        double threshold = GetTolerance();
        auto error1       = miopen::rms_range(ref_inputGrad, inputGrad);
        auto error2       = miopen::rms_range(ref_otherGrad, otherGrad);

        EXPECT_TRUE(miopen::range_distance(ref_inputGrad) == miopen::range_distance(inputGrad));
        EXPECT_TRUE(miopen::range_distance(ref_otherGrad) == miopen::range_distance(otherGrad));
        EXPECT_TRUE(error1 < threshold * 10) << "Error output (input grad) beyond tolerance Error:" << error1
                                            << ",  Thresholdx10: " << threshold * 10 << std::endl;

        EXPECT_TRUE(error2 < threshold * 10) << "Error output (other grad) beyond tolerance Error:" << error2
                                            << ",  Thresholdx10: " << threshold * 10 << std::endl;
    }
    WhereTestCase where_config;

    tensor<T> inputGrad;
    tensor<T> otherGrad;
    tensor<T> cond;
    tensor<T> outputGrad;

    tensor<T> ref_inputGrad;
    tensor<T> ref_otherGrad;

    miopen::Allocator::ManageDataPtr inputGrad_dev;
    miopen::Allocator::ManageDataPtr otherGrad_dev;
    miopen::Allocator::ManageDataPtr cond_dev;
    miopen::Allocator::ManageDataPtr outputGrad_dev;
};