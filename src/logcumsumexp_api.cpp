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

#include <miopen/handle.hpp>
#include <miopen/tensor_ops.hpp>
#include <miopen/logcumsumexp.hpp>

static void LogCmdLogCumSumExp(const miopenTensorDescriptor_t inputDesc,
                               const miopenTensorDescriptor_t outputDesc,
                               const miopenTensorDescriptor_t doutputDesc,
                               const miopenTensorDescriptor_t dinputDesc,
                               const int dim,
                               const bool exclusive,
                               const bool reverse,
                               const bool is_fwd)
{
    if(miopen::IsLoggingCmd())
    {
        std::stringstream ss;
        auto dtype = miopen::deref(inputDesc).GetType();
        if(dtype == miopenHalf)
        {
            ss << "logcumsumexpfp16";
        }
        else if(dtype == miopenFloat)
        {
            ss << "logcumsumexpfp32";
        }
        else if(dtype == miopenBFloat16)
        {
            ss << "logcumsumexpbfp16";
        }

        if(is_fwd)
        {
            MIOPEN_LOG_FUNCTION(inputDesc, outputDesc);
        }
        else
        {
            MIOPEN_LOG_FUNCTION(inputDesc, outputDesc, doutputDesc, dinputDesc);
        }
        ss << " -d " << dim;
        ss << " --excl " << exclusive;
        ss << " --rev " << reverse;
        ss << " -F " << ((is_fwd) ? "1" : "2");

        MIOPEN_LOG_DRIVER_CMD(ss.str());
    }
}

extern "C" miopenStatus_t miopenLogCumSumExpForward(miopenHandle_t handle,
                                                    const miopenTensorDescriptor_t inputDesc,
                                                    const void* input,
                                                    const miopenTensorDescriptor_t outputDesc,
                                                    void* output,
                                                    const int dim,
                                                    const bool exclusive,
                                                    const bool reverse)
{
    MIOPEN_LOG_FUNCTION(handle, inputDesc, input, outputDesc, output, dim, exclusive, reverse);

    LogCmdLogCumSumExp(inputDesc, outputDesc, nullptr, nullptr, dim, exclusive, reverse, true);
    return miopen::try_([&] {
        miopen::logcumsumexp::LogCumSumExpForward(miopen::deref(handle),
                                                  miopen::deref(inputDesc),
                                                  DataCast(input),
                                                  miopen::deref(outputDesc),
                                                  DataCast(output),
                                                  dim,
                                                  exclusive,
                                                  reverse);
    });
}

extern "C" miopenStatus_t miopenLogCumSumExpBackward(miopenHandle_t handle,
                                                     miopenTensorDescriptor_t inputDesc,
                                                     const void* input,
                                                     miopenTensorDescriptor_t outputDesc,
                                                     const void* output,
                                                     miopenTensorDescriptor_t doutputDesc,
                                                     const void* doutput,
                                                     miopenTensorDescriptor_t dinputDesc,
                                                     void* dinput,
                                                     int dim,
                                                     bool exclusive,
                                                     bool reverse)
{
    MIOPEN_LOG_FUNCTION(handle,
                        inputDesc,
                        input,
                        outputDesc,
                        output,
                        doutputDesc,
                        doutput,
                        dinputDesc,
                        dinput,
                        dim,
                        exclusive,
                        reverse);

    LogCmdLogCumSumExp(
        inputDesc, outputDesc, doutputDesc, dinputDesc, dim, exclusive, reverse, false);
    return miopen::try_([&] {
        miopen::logcumsumexp::LogCumSumExpBackward(miopen::deref(handle),
                                                   miopen::deref(inputDesc),
                                                   DataCast(input),
                                                   miopen::deref(outputDesc),
                                                   DataCast(output),
                                                   miopen::deref(doutputDesc),
                                                   DataCast(doutput),
                                                   miopen::deref(dinputDesc),
                                                   DataCast(dinput),
                                                   dim,
                                                   exclusive,
                                                   reverse);
    });
}
