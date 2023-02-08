/*******************************************************************************
 *
 * MIT License
 *
 * Copyright (c) 2022 Advanced Micro Devices, Inc.
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

#include <vector>
#include <cstdint>

#include <miopen/solver.hpp>
#include <miopen/generic_search.hpp>
#include <miopen/conv/data_invoke_params.hpp>
#include <miopen/solver/problem_description_interpreter.hpp>
#if MIOPEN_BACKEND_HIP && MIOPEN_USE_COMPOSABLEKERNEL
#include <ck/library/tensor_operation_instance/gpu/grouped_convolution_forward.hpp>
#endif
MIOPEN_DECLARE_ENV_VAR(MIOPEN_DEBUG_GROUP_CONV_IMPLICIT_GEMM_HIP_FWD_XDLOPS)

namespace miopen {
namespace solver {

#if MIOPEN_BACKEND_HIP && MIOPEN_USE_COMPOSABLEKERNEL
template <typename DataType>
using DeviceOpGFwd = ck::tensor_operation::device::DeviceGroupedConvFwdMultipleD<
    2,
    ck::tensor_layout::convolution::GNHWC,
    ck::tensor_layout::convolution::GKYXC,
    ck::Tuple<>,
    ck::tensor_layout::convolution::GNHWK,
    DataType,
    DataType,
    ck::Tuple<>,
    DataType,
    ck::tensor_operation::element_wise::PassThrough,
    ck::tensor_operation::element_wise::PassThrough,
    ck::tensor_operation::element_wise::PassThrough>;

template <typename DataType>
using DeviceOpGFwdPtrs =
    ck::tensor_operation::device::instance::DeviceOperationInstanceFactory<DeviceOpGFwd<DataType>>;

struct CKArgsGFwd
{
    CKArgsGFwd(const ProblemDescription& problem)
    {
        G        = ProblemInterpreter::GetGroupCountG(problem);
        N        = ProblemInterpreter::GetBatchN(problem);
        K        = ProblemInterpreter::GetOutputChannelK(problem);
        C        = ProblemInterpreter::GetInputChannelC(problem);
        input    = {G, N, ProblemInterpreter::GetInputHeightHi(problem),
                 ProblemInterpreter::GetInputWidthWi(problem), C};
        output   = {G, N, ProblemInterpreter::GetOutputHeightHo(problem),
                  ProblemInterpreter::GetOutputWidthWo(problem), K};
        weight   = {G, K, ProblemInterpreter::GetFilterHeightY(problem),
                  ProblemInterpreter::GetFilterWidthX(problem), C};
        in_strides = {0, 0, 0, 0, 1};
        out_strides = {0, 0, 0, 0, 1};
        wei_strides = {0, 0, 0, 0, 1};
        strides  = {ProblemInterpreter::GetAdjustedConvolutionStrideH(problem),
                   ProblemInterpreter::GetAdjustedConvolutionStrideW(problem)};
        dilation = {ProblemInterpreter::GetAdjustedConvolutionDilationH(problem),
                    ProblemInterpreter::GetAdjustedConvolutionDilationW(problem)};
        lPadding = {ProblemInterpreter::GetInputLeftPadH(problem),
                    ProblemInterpreter::GetInputLeftPadW(problem)};
        rPadding = {ProblemInterpreter::GetAdjustedInputRightPadH(problem),
                    ProblemInterpreter::GetAdjustedInputRightPadW(problem)};
        std::partial_sum(rbegin(input),
                        std::prev(rend(input)),
                        std::next(rbegin(in_strides)),
                        std::multiplies<>{});
        std::partial_sum(rbegin(weight),
                        std::prev(rend(weight)),
                        std::next(rbegin(wei_strides)),
                        std::multiplies<>{});
        std::partial_sum(rbegin(output),
                        std::prev(rend(output)),
                        std::next(rbegin(out_strides)),
                        std::multiplies<>{});

        std::rotate(
            rbegin(input), std::next(rbegin(input)), std::next(rbegin(input), 3));
        std::rotate(
            rbegin(in_strides), std::next(rbegin(in_strides)), std::next(rbegin(in_strides), 3));
        std::rotate(
            rbegin(weight), std::next(rbegin(weight)), std::next(rbegin(weight), 3));
        std::rotate(
            rbegin(wei_strides), std::next(rbegin(wei_strides)), std::next(rbegin(wei_strides), 3));
        std::rotate(
            rbegin(output), std::next(rbegin(output)), std::next(rbegin(output), 3));
        std::rotate(
            rbegin(out_strides), std::next(rbegin(out_strides)), std::next(rbegin(out_strides), 3));
   
    }
    int G;
    int N;
    int K;
    int C;
    std::array<ck::index_t, 5> input;
    std::array<ck::index_t, 5> in_strides;
    std::array<ck::index_t, 5> output;
    std::array<ck::index_t, 5> out_strides;
    std::array<ck::index_t, 5> weight;
    std::array<ck::index_t, 5> wei_strides;
    std::array<ck::index_t, 2> strides;
    std::array<ck::index_t, 2> dilation;
    std::array<ck::index_t, 2> lPadding;
    std::array<ck::index_t, 2> rPadding;
};

template <typename DataType>
void PerformanceConfigHipImplicitGemmGroupFwdXdlops::Init(const ProblemDescription& problem)
{
    const auto args      = CKArgsGFwd{problem};
    const auto conv_ptrs = DeviceOpGFwdPtrs<DataType>::GetInstances();
    assert(!conv_ptrs.empty());
    for(int i = 0; i < conv_ptrs.size(); i++)
    {
        auto argument_ptr = conv_ptrs[i]->MakeArgumentPointer(nullptr,
                                                              nullptr,
                                                              {},
                                                              nullptr,
                                                              args.input,
                                                              args.in_strides,
                                                              args.weight,
                                                              args.wei_strides,
                                                              {},
                                                              {},
                                                              args.output,
                                                              args.out_strides,
                                                              args.strides,
                                                              args.dilation,
                                                              args.lPadding,
                                                              args.rPadding,
                                                              {},
                                                              {},
                                                              {});
        if(conv_ptrs[i]->IsSupportedArgument(argument_ptr.get()))
        {
            valid_kernels.push_back(conv_ptrs[i]->GetTypeString());
        }
    }
    assert(!valid_kernels.empty());
    this->index     = 0;
    this->kernel_id = valid_kernels[0];
}

template <typename DataType>
bool PerformanceConfigHipImplicitGemmGroupFwdXdlops::CheckIsSupportCKArgs(
    const ProblemDescription& problem) const
{
    const auto args      = CKArgsGFwd{problem};
    const auto conv_ptrs = DeviceOpGFwdPtrs<DataType>::GetInstances();
    int i                = 0;
    for(; i < conv_ptrs.size(); i++)
    {
        if(conv_ptrs[i]->GetTypeString() == this->kernel_id)
        {
            break;
        }
    }
    if(i == valid_kernels.size())
    {
        return false;
    }
    auto argument_ptr = conv_ptrs[i]->MakeArgumentPointer(nullptr,
                                                          nullptr,
                                                          {},
                                                          nullptr,
                                                          args.input,
                                                          args.in_strides,
                                                          args.weight,
                                                          args.wei_strides,
                                                          {},
                                                          {},
                                                          args.output,
                                                          args.out_strides,
                                                          args.strides,
                                                          args.dilation,
                                                          args.lPadding,
                                                          args.rPadding,
                                                          {},
                                                          {},
                                                          {});
    return conv_ptrs[i]->IsSupportedArgument(argument_ptr.get());
}

template <typename DataType>
bool ConvHipImplicitGemmGroupFwdXdlops::CheckCKApplicability(const ProblemDescription& problem) const
{
    const auto conv_ptrs = DeviceOpGFwdPtrs<DataType>::GetInstances();
    assert(!conv_ptrs.empty());
    const auto args = CKArgsGFwd{problem};
    if(!std::all_of(args.strides.begin(), args.strides.end(), [&](auto x) { return x == 1; }))
        return false;
    for(int i = 0; i < conv_ptrs.size(); i++)
    {
        auto argument_ptr = conv_ptrs[i]->MakeArgumentPointer(nullptr,
                                                              nullptr,
                                                              {},
                                                              nullptr,
                                                              args.input,
                                                              args.in_strides,
                                                              args.weight,
                                                              args.wei_strides,
                                                              {},
                                                              {},
                                                              args.output,
                                                              args.out_strides,
                                                              args.strides,
                                                              args.dilation,
                                                              args.lPadding,
                                                              args.rPadding,
                                                              {},
                                                              {},
                                                              {});
        if(conv_ptrs[i]->IsSupportedArgument(argument_ptr.get()))
            return true;
    }
    return false;
}

template <typename DataType>
void ConvHipImplicitGemmGroupFwdXdlops::RunCKSolution(
    const Handle& handle,
    const AnyInvokeParams& primitive_parameters,
    const ProblemDescription& problem,
    const PerformanceConfigHipImplicitGemmGroupFwdXdlops& config) const
{
    const auto args      = CKArgsGFwd{problem};
    const auto conv_ptrs = DeviceOpGFwdPtrs<DataType>::GetInstances();
    int i                = 0;
    for(; i < conv_ptrs.size(); i++)
    {
        if(conv_ptrs[i]->GetTypeString() == config.kernel_id)
        {
            break;
        }
    }
    assert(i != conv_ptrs.size());
    auto& conv_ptr      = conv_ptrs.at(i);
    auto& data_ctx      = primitive_parameters.CastTo<conv::DataInvokeParams>();
    const auto& tensors = data_ctx.tensors;
    auto argument_ptr   = conv_ptr->MakeArgumentPointer(
        const_cast<void*>( // NOLINT (cppcoreguidelines-pro-type-const-cast)
            static_cast<const void*>(tensors.in)),
        const_cast<void*>( // NOLINT (cppcoreguidelines-pro-type-const-cast)
            static_cast<const void*>(tensors.w)),
        {},
        static_cast<void*>(tensors.out),
        args.input,
        args.in_strides,
        args.weight,
        args.wei_strides,
        {},
        {},
        args.output,
        args.out_strides,
        args.strides,
        args.dilation,
        args.lPadding,
        args.rPadding,
        {},
        {},
        {});
    auto invoker_ptr            = conv_ptr->MakeInvokerPointer();
    const auto enable_profiling = handle.IsProfilingEnabled();
    
    std::cout<<"*********run ck solution**********"<<std::endl;
    auto& xDesc = tensors.inDesc;
    /*auto& wDesc = tensors.wDesc;
    auto& yDesc = tensors.outDesc;
    std::cout<<"**********************************************G: "<<args.G<<std::endl;
    std::cout<<"**********************************************N: "<<args.N<<std::endl;
    std::cout<<"**********************************************K: "<<args.K<<std::endl;
    std::cout<<"**********************************************C: "<<args.C<<std::endl;*/   
    std::cout<<"****************************problem.conv_problem.GetInLayout(): "<<problem.conv_problem.GetInLayout()<<std::endl;
    std::cout<<"****************************xDesc.GetLayout_str(): "<<xDesc.GetLayout_str()<<std::endl;
    /*std::cout<<"****************************yDesc.GetLayout_t(): "<<yDesc.GetLayout_t()<<std::endl;
    std::cout<<"****************************strides[0]: "<<args.in_strides[0]<<std::endl;
    std::cout<<"****************************strides[1]: "<<args.in_strides[1]<<std::endl;
    std::cout<<"****************************strides[2]: "<<args.in_strides[2]<<std::endl;
    std::cout<<"****************************strides[3]: "<<args.in_strides[3]<<std::endl;
    std::cout<<"****************************strides[4]: "<<args.in_strides[4]<<std::endl;
    std::cout<<"****************************xDesc.GetLengths()[0]: "<<xDesc.GetLengths()[0]<<std::endl;
    std::cout<<"****************************xDesc.GetLengths()[1]: "<<xDesc.GetLengths()[1]<<std::endl;
    std::cout<<"****************************xDesc.GetLengths()[2]: "<<xDesc.GetLengths()[2]<<std::endl;
    std::cout<<"****************************xDesc.GetLengths()[3]: "<<xDesc.GetLengths()[3]<<std::endl;
    std::cout<<"****************************wDesc.GetLengths()[0]: "<<wDesc.GetLengths()[0]<<std::endl;
    std::cout<<"****************************wDesc.GetLengths()[1]: "<<wDesc.GetLengths()[1]<<std::endl;
    std::cout<<"****************************wDesc.GetLengths()[2]: "<<wDesc.GetLengths()[2]<<std::endl;
    std::cout<<"****************************wDesc.GetLengths()[3]: "<<wDesc.GetLengths()[3]<<std::endl;*/

    float elapsed_time =
        invoker_ptr->Run(argument_ptr.get(), {handle.GetStream(), enable_profiling});
    if(enable_profiling)
    {
        handle.ResetKernelTime();
        handle.AccumKernelTime(elapsed_time);
    }
}
#endif

void PerformanceConfigHipImplicitGemmGroupFwdXdlops::HeuristicInit(const ProblemDescription& problem)
{
#if !MIOPEN_BACKEND_HIP || !MIOPEN_USE_COMPOSABLEKERNEL
    std::ignore = problem;
#else
    switch(problem.conv_problem.GetInDataType())
    {
    case miopenHalf: Init<ck::half_t>(problem); break;
    case miopenFloat: Init<float>(problem); break;
    case miopenInt8:
    case miopenInt32:
    case miopenInt8x4:
    case miopenBFloat16:
    case miopenDouble: break;
    }
#endif
}

bool PerformanceConfigHipImplicitGemmGroupFwdXdlops::SetNextValue(const ProblemDescription& problem)
{
    if(valid_kernels.empty())
    {
        this->HeuristicInit(problem);
        assert(!valid_kernels.empty());
        return true;
    }
    if((index + 1) < valid_kernels.size())
    {
        ++index;
        this->kernel_id = this->valid_kernels[index];
        return true;
    }
    else
        return false;
}

bool PerformanceConfigHipImplicitGemmGroupFwdXdlops::IsValidValue() const
{
    return index < valid_kernels.size();
}

bool PerformanceConfigHipImplicitGemmGroupFwdXdlops::IsValid(const ProblemDescription& problem) const
{
#if !MIOPEN_BACKEND_HIP || !MIOPEN_USE_COMPOSABLEKERNEL
    std::ignore = problem;
    return false;
#else
    switch(problem.conv_problem.GetInDataType())
    {
    case miopenHalf: return CheckIsSupportCKArgs<ck::half_t>(problem);
    case miopenFloat: return CheckIsSupportCKArgs<float>(problem);
    case miopenInt8:
    case miopenInt32:
    case miopenInt8x4:
    case miopenBFloat16:
    case miopenDouble: break;
    }
    return false;
#endif
}

bool PerformanceConfigHipImplicitGemmGroupFwdXdlops::operator==(
    const PerformanceConfigHipImplicitGemmGroupFwdXdlops& other) const
{
    return this->kernel_id == other.kernel_id;
}

PerformanceConfigHipImplicitGemmGroupFwdXdlops
ConvHipImplicitGemmGroupFwdXdlops::GetDefaultPerformanceConfig(const ProblemDescription& problem) const
{
    PerformanceConfigHipImplicitGemmGroupFwdXdlops pp;
    pp.HeuristicInit(problem);
    return pp;
}

bool ConvHipImplicitGemmGroupFwdXdlops::IsValidPerformanceConfig(
    const ProblemDescription& problem,
    const PerformanceConfigHipImplicitGemmGroupFwdXdlops& config) const
{
    return config.IsValid(problem);
}

PerformanceConfigHipImplicitGemmGroupFwdXdlops
ConvHipImplicitGemmGroupFwdXdlops::Search(const ConvolutionContext& ctx,
                                     const ProblemDescription& problem,
                                     const AnyInvokeParams& invoke_ctx) const
{
    return GenericSearch(*this, ctx, problem, invoke_ctx);
}

bool ConvHipImplicitGemmGroupFwdXdlops::IsApplicable(const ConvolutionContext& ctx,
                                                const ProblemDescription& problem) const
{
#if !MIOPEN_BACKEND_HIP || !MIOPEN_USE_COMPOSABLEKERNEL
    std::ignore = ctx;
    std::ignore = problem;
    return false;
#else
    if(miopen::IsDisabled(MIOPEN_DEBUG_GROUP_CONV_IMPLICIT_GEMM_HIP_FWD_XDLOPS{}))
        return false;
    if(miopen::IsEnabled(MIOPEN_DEBUG_CONVOLUTION_DETERMINISTIC{}))
        return false;
    if(problem.conv_problem.GetInDataType() != problem.conv_problem.GetWeightsDataType() ||
       problem.conv_problem.GetWeightsDataType() != problem.conv_problem.GetOutDataType() ||
       problem.conv_problem.GetInDataType() != problem.conv_problem.GetOutDataType())
        return false;
    if(!problem.direction.IsForward())
        return false;
    if(!problem.Is2d())
        return false;
    if(!problem.IsLayoutNHWC())
        return false;
    const std::string& arch = ctx.GetStream().GetDeviceName();
    if(!(arch == "gfx908" || arch == "gfx90a"))
        return false;
    switch(problem.conv_problem.GetInDataType())
    {
    case miopenHalf: return CheckCKApplicability<ck::half_t>(problem);
    case miopenFloat: return CheckCKApplicability<float>(problem);
    case miopenInt8:
    case miopenInt32:
    case miopenInt8x4:
    case miopenBFloat16:
    case miopenDouble: break;
    }
    return false;
#endif
}

ConvSolution ConvHipImplicitGemmGroupFwdXdlops::GetSolution(
    const ConvolutionContext& ctx,
    const ProblemDescription& problem,
    const PerformanceConfigHipImplicitGemmGroupFwdXdlops& config) const
{
    std::ignore = ctx;
#if !MIOPEN_BACKEND_HIP || !MIOPEN_USE_COMPOSABLEKERNEL
    std::ignore = problem;
    std::ignore = config;
    return {};
#else
    ConvSolution result;
    result.invoker_factory = [=](const std::vector<Kernel>& kernels) {
        std::ignore = kernels;
        return [=](const Handle& handle, const AnyInvokeParams& primitive_parameters) {
            switch(problem.conv_problem.GetInDataType())
            {
            case miopenHalf:
                RunCKSolution<ck::half_t>(handle, primitive_parameters, problem, config);
                break;
            case miopenFloat:
                RunCKSolution<float>(handle, primitive_parameters, problem, config);
                break;
            case miopenInt8:
            case miopenInt32:
            case miopenInt8x4:
            case miopenBFloat16:
            case miopenDouble: break;
            }
        };
    };
    return result;
#endif
}

} // namespace solver
} // namespace miopen
