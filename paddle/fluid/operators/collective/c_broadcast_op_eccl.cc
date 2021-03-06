/* Copyright (c) 2021 PaddlePaddle Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. */

#include "paddle/fluid/operators/collective/c_broadcast_op.h"

#if defined(PADDLE_WITH_ECCL)
#include "paddle/fluid/platform/collective_helper.h"
#include "paddle/fluid/platform/eccl_helper.h"
#endif

namespace paddle {
namespace operators {

template <typename T>
class CBroadcastOpECCLKernel : public framework::OpKernel<T> {
 public:
  void Compute(const framework::ExecutionContext& ctx) const override {
#if defined(PADDLE_WITH_ECCL)
    auto x = ctx.Input<framework::LoDTensor>("X");
    void* ptr = reinterpret_cast<void*>(const_cast<T*>(x->data<T>()));
    int numel = x->numel();
    EcclDataType dtype = platform::ToECCLDataType(x->type());

    auto out = ctx.Output<framework::LoDTensor>("Out");
    void* out_ptr = reinterpret_cast<void*>(const_cast<T*>(out->data<T>()));

    int ring_id = ctx.Attr<int>("ring_id");
    auto place = ctx.GetPlace();
    auto comm =
        paddle::platform::ECCLCommContext::Instance().Get(ring_id, place);

    aclrtStream stream = nullptr;
    auto dev_ctx = platform::DeviceContextPool::Instance().Get(place);
    if (ctx.Attr<bool>("use_calc_stream")) {
      stream = static_cast<platform::NPUDeviceContext*>(dev_ctx)->stream();
    } else {
      stream = comm->stream();
    }

    int root = ctx.Attr<int>("root");

    VLOG(3) << "begin eccl broadcast, parameter is: "
            << "root " << root << ", ring_id is " << ring_id
            << ", comm: " << comm->comm() << ", stream: " << stream;

    PADDLE_ENFORCE_ECCL_SUCCESS(platform::dynload::eccl_broadcast(
        ptr, out_ptr, numel, dtype, root, comm->comm().c_str(), stream, AUTO));

    VLOG(3) << "rank " << comm->rank() << " invoke Bcast. recieved "
            << framework::product(out->dims());

#else
    PADDLE_THROW(platform::errors::PreconditionNotMet(
        "PaddlePaddle should compile with NPU."));
#endif
  }
};

}  // namespace operators
}  // namespace paddle

namespace ops = paddle::operators;
namespace plat = paddle::platform;

REGISTER_OP_NPU_KERNEL(c_broadcast, ops::CBroadcastOpECCLKernel<int>,
                       ops::CBroadcastOpECCLKernel<int8_t>,
                       ops::CBroadcastOpECCLKernel<float>,
                       ops::CBroadcastOpECCLKernel<plat::float16>);
