//   Copyright (c) 2021 PaddlePaddle Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#if defined(PADDLE_WITH_ECCL)
#include "paddle/fluid/platform/collective_helper.h"
#include <utility>

namespace paddle {
namespace platform {

class ECCLCommImpl : public ECCLComm {
 public:
  void set_ring_id(int ring_id) { ring_id_ = ring_id; }
  int ring_id() const override { return ring_id_; }

  void set_nranks(int nranks) { nranks_ = nranks; }
  int nranks() const override { return nranks_; }

  void set_rank(int rank) { rank_ = rank; }
  int rank() const override { return rank_; }

  int device_id() const override {
    return BOOST_GET_CONST(NPUPlace, dev_ctx_->GetPlace()).device;
  }

  ~ECCLCommImpl() {
    PADDLE_ENFORCE_ECCL_SUCCESS(platform::dynload::eccl_destroy_comm_global(comm_.c_str()));
  }

  void set_comm(PaddleEcclCommGroupIdType comm) { comm_ = comm; }
  PaddleEcclCommGroupIdType comm() const override { return comm_; }

  aclrtStream stream() const override { return dev_ctx_->stream(); }

  void set_dev_ctx(std::unique_ptr<NPUDeviceContext>&& dev_ctx) {
    dev_ctx_ = std::move(dev_ctx);
  }
  NPUDeviceContext* dev_context() const override { return dev_ctx_.get(); }

 private:
  int ring_id_;
  int nranks_;
  int rank_;
  PaddleEcclCommGroupIdType comm_;
  std::unique_ptr<NPUDeviceContext> dev_ctx_;
};

ECCLComm* ECCLCommContext::CreateECCLComm(PaddleEcclCommGroupIdType group_name, int nranks,
                                          int rank, int dev_id, int ring_id) {
  PADDLE_ENFORCE_GT(
      nranks, 1,
      platform::errors::InvalidArgument(
          "Expected nranks > 1. But received nranks is %d.", nranks));
  PADDLE_ENFORCE_GE(rank, 0,
                    platform::errors::InvalidArgument(
                        "Expected rank >= 0. But received rank is %d.", rank));
  PADDLE_ENFORCE_LT(
      rank, nranks,
      platform::errors::InvalidArgument(
          "Expected rank < nranks. But received rank is %d, nranks is %d.",
          rank, nranks));
  PADDLE_ENFORCE_GE(
      dev_id, 0,
      platform::errors::InvalidArgument(
          "Expected dev_id >= 0. But received dev_id is %d.", dev_id));

  PADDLE_ENFORCE_NPU_SUCCESS(aclrtSetDevice(dev_id));
  VLOG(1) << "initialized comm: " << group_name << ", nranks: " << nranks
          << ", rank: " << rank;
  PADDLE_ENFORCE_NPU_SUCCESS(
      platform::dynload::eccl_init_comm_global(nranks, rank, "CPU", dev_id, group_name.c_str()));

  VLOG(1) << "initialized comm: " << group_name << ", nranks: " << nranks
          << ", rank: " << rank;

  auto* comm_wrapper = AssignECCLComm(group_name, nranks, rank, dev_id, ring_id);

  VLOG(1) << "eccl communicator of rank " << rank << " in ring " << ring_id
          << " has been created on device " << dev_id
          << ", with comm: " << comm_wrapper->comm();

  std::call_once(once_flag_, []() {
    std::atexit([]() { ECCLCommContext::Instance().ReleaseECCLComms(); });
  });

  return comm_wrapper;
}

ECCLComm* ECCLCommContext::AssignECCLComm(PaddleEcclCommGroupIdType group_name, int nranks, int rank,
                                          int dev_id, int ring_id) {
  std::unique_ptr<NPUDeviceContext> dev_ctx(
      new NPUDeviceContext(NPUPlace(dev_id)));

  ECCLCommImpl* c = new ECCLCommImpl;
  c->set_ring_id(ring_id);
  c->set_nranks(nranks);
  c->set_rank(rank);
  c->set_comm(group_name);
  c->set_dev_ctx(std::move(dev_ctx));

  comm_map_mutex_.lock();
  if (comm_map_.count(ring_id) == 0) {
    comm_map_.emplace(ring_id, std::map<int, std::unique_ptr<ECCLComm>>());
  }
  auto& dev2comm = comm_map_[ring_id];

  dev2comm.emplace(dev_id, std::unique_ptr<ECCLComm>(c));
  comm_map_mutex_.unlock();

  if (ring_id == 0) {
    auto* dev_ctx = static_cast<platform::NPUDeviceContext*>(
        platform::DeviceContextPool::Instance().Get(
            platform::NPUPlace(dev_id)));
    dev_ctx->set_eccl_comm(group_name);
  }

  return comm_map_[ring_id][dev_id].get();
}

void ECCLCommContext::ReleaseECCLComms() {
  for (auto& p : comm_map_) {
    for (auto& q : p.second) {
      q.second.reset();
    }
  }
}

}  // namespace platform
}  // namespace paddle
#endif
