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
#include "paddle/fluid/operators/collective/c_eccl_collective_test_help.h"

namespace f = paddle::framework;
namespace p = paddle::platform;
namespace m = paddle::operators::math;

USE_OP(c_broadcast);
USE_OP_DEVICE_KERNEL(c_broadcast, NPU);

DECLARE_string(selected_npus);

void TestHCCLBroadcastOp(f::Scope* scope, const p::DeviceContext& ctx) {
  int root = atoi(getenv("ROOT_ID"));

  // init
  auto x = scope->Var("Data");
  auto tensor_x = x->GetMutable<f::LoDTensor>();
  int num = 2;
  std::vector<float> init;
  int rank_id = atoi(getenv("RANK_ID"));

  for (int64_t i = 0; i < num * num; ++i) {
    init.push_back(rank_id);
  }
  PrintDebugInfo("input data", init);

  TensorFromVector(init, ctx, tensor_x);
  tensor_x->Resize({num, num});
  ctx.Wait();

  auto place = ctx.GetPlace();
  auto out = scope->Var("OutData");
  auto tensor_out = out->GetMutable<f::LoDTensor>();
  tensor_out->Resize({num, num});
  tensor_out->mutable_data<float>(place);  // allocate
  ctx.Wait();

  // run
  f::AttributeMap attrs;
  attrs["tag"] = std::string("tagx");
  attrs["root"] = root;
  attrs["ring_id"] = 0;

  auto op = f::OpRegistry::CreateOp("c_broadcast", {{"X", {"Data"}}},
                                    {{"Out", {"OutData"}}}, attrs);

  for (int i = 0; i < 10; i++) {
    op->Run(*scope, place);
  }
  ctx.Wait();

  std::vector<float> out_vec;
  TensorToVector(*tensor_out, ctx, &out_vec);
  ctx.Wait();

  PrintDebugInfo("output data", out_vec);
  EXPECT_EQ(out_vec.size(), init.size());
  for (uint32_t i = 0; i < out_vec.size(); i++) {
    EXPECT_EQ(out_vec[i], root);
  }
}

TEST(c_broadcast, NPU) {
  f::Scope scope;
  PaddleEcclCommGroupIdType group_name = "test_group_1";
  // only support one device, if more than one device, use first default
  p::NPUDeviceContext ctx(p::NPUPlace(atoi(FLAGS_selected_npus.c_str())));

  PrepareUniqueId(&scope, ctx, group_name);
  Prepare(&scope, ctx, group_name);
  TestHCCLBroadcastOp(&scope, ctx);
}
