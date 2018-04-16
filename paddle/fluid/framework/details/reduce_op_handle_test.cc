//   Copyright (c) 2018 PaddlePaddle Authors. All Rights Reserved.
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

#include "paddle/fluid/framework/details/reduce_op_handle.h"
#include "gtest/gtest.h"

#include "paddle/fluid/platform/device_context.h"

namespace paddle {
namespace framework {
namespace details {
namespace f = paddle::framework;
namespace p = paddle::platform;

// test data amount
const f::DDim kDims = {20, 20};

struct TestReduceOpHandle {
  bool use_gpu_;
  Scope g_scope_;
  std::vector<Scope *> local_scopes_;
  std::unique_ptr<OpHandleBase> op_handle_;
  std::vector<std::unique_ptr<VarHandleBase>> vars_;
  std::vector<p::Place> gpu_list_;
  std::vector<std::unique_ptr<p::DeviceContext>> ctxs_;

#ifdef PADDLE_WITH_CUDA
  std::unique_ptr<platform::NCCLContextMap> nccl_ctxs_;
#endif

  void WaitAll() {
    for (size_t j = 0; j < ctxs_.size(); ++j) {
      ctxs_[j]->Wait();
    }
#ifdef PADDLE_WITH_CUDA
    nccl_ctxs_->WaitAll();
#endif
  }

  void InitCtxOnGpu(bool use_gpu) {
    use_gpu_ = use_gpu;
    if (use_gpu) {
#ifdef PADDLE_WITH_CUDA
      int count = p::GetCUDADeviceCount();
      if (count <= 1) {
        LOG(WARNING) << "Cannot test multi-gpu Broadcast, because the CUDA "
                        "device count is "
                     << count;
        exit(0);
      }
      for (int i = 0; i < count; ++i) {
        auto p = p::CUDAPlace(i);
        gpu_list_.push_back(p);
        ctxs_.emplace_back(new p::CUDADeviceContext(p));
      }
#else
      PADDLE_THROW("CUDA is not support.");
#endif
    } else {
      int count = 8;
      for (int i = 0; i < count; ++i) {
        auto p = p::CPUPlace();
        gpu_list_.push_back(p);
        ctxs_.emplace_back(new p::CPUDeviceContext(p));
      }
    }
#ifdef PADDLE_WITH_CUDA
    nccl_ctxs_.reset(new platform::NCCLContextMap(gpu_list_));
#endif
  }

  void InitReduceOp(size_t input_scope_idx) {
    for (size_t j = 0; j < gpu_list_.size(); ++j) {
      local_scopes_.push_back(&(g_scope_.NewScope()));
      local_scopes_[j]->Var("out");
    }
    local_scopes_[input_scope_idx]->Var("input");

#ifdef PADDLE_WITH_CUDA
    op_handle_.reset(new ReduceOpHandle(local_scopes_, gpu_list_, *nccl_ctxs_));
#else
    op_handle_.reset(new ReduceOpHandle(local_scopes_, gpu_list_));
#endif

    // add input
    for (size_t j = 0; j < gpu_list_.size(); ++j) {
      op_handle_->dev_ctxes_[gpu_list_[j]] = ctxs_[j].get();
      vars_.emplace_back(new VarHandle());
      VarHandle *in_var_handle = static_cast<VarHandle *>(vars_.back().get());
      in_var_handle->place_ = gpu_list_[j];
      in_var_handle->name_ = "input";
      in_var_handle->version_ = 1;
      in_var_handle->scope_idx_ = j;
      in_var_handle->generated_op_ = nullptr;
      op_handle_->AddInput(in_var_handle);
    }

    // add dummy var
    vars_.emplace_back(new DummyVarHandle());
    DummyVarHandle *in_dummy_var_handle =
        static_cast<DummyVarHandle *>(vars_.back().get());
    in_dummy_var_handle->generated_op_ = nullptr;
    op_handle_->AddInput(in_dummy_var_handle);

    // add output
    vars_.emplace_back(new VarHandle());
    VarHandle *out_var_handle = static_cast<VarHandle *>(vars_.back().get());
    out_var_handle->place_ = gpu_list_[input_scope_idx];
    out_var_handle->name_ = "out";
    out_var_handle->version_ = 2;
    out_var_handle->scope_idx_ = input_scope_idx;
    op_handle_->AddOutput(out_var_handle);

    // add dummy var
    vars_.emplace_back(new DummyVarHandle());
    DummyVarHandle *dummy_var_handle =
        static_cast<DummyVarHandle *>(vars_.back().get());
    op_handle_->AddOutput(dummy_var_handle);
  }

  void TestReduceSelectedRows(size_t output_scope_idx) {
    int height = kDims[0] * 2;
    std::vector<int64_t> rows{0, 1, 2, 3, 3, 0, 14, 7, 3, 1,
                              2, 4, 6, 3, 1, 1, 1,  1, 3, 7};
    std::vector<float> send_vector(f::product(kDims));
    for (size_t k = 0; k < send_vector.size(); ++k) {
      send_vector[k] = k;
    }

    for (size_t input_scope_idx = 0; input_scope_idx < gpu_list_.size();
         ++input_scope_idx) {
      auto in_var = local_scopes_[input_scope_idx]->Var("input");
      auto in_selected_rows = in_var->GetMutable<f::SelectedRows>();
      auto value = in_selected_rows->mutable_value();
      value->mutable_data<float>(kDims, gpu_list_[input_scope_idx]);

      in_selected_rows->set_height(height);
      in_selected_rows->set_rows(rows);

      paddle::framework::TensorFromVector<float>(
          send_vector, *(ctxs_[input_scope_idx]), value);
      value->Resize(kDims);
    }

    auto out_var = local_scopes_[output_scope_idx]->Var("out");
    auto out_selected_rows = out_var->GetMutable<f::SelectedRows>();

    auto in_var = local_scopes_[output_scope_idx]->Var("input");
    auto in_selected_rows = in_var->GetMutable<f::SelectedRows>();

    out_selected_rows->mutable_value()->ShareDataWith(
        in_selected_rows->value());

    op_handle_->Run(false);

    WaitAll();

    p::CPUPlace cpu_place;

    auto &out_select_rows = out_var->Get<f::SelectedRows>();
    auto rt = out_select_rows.value();

    PADDLE_ENFORCE_EQ(out_select_rows.height(), height, "height is not equal.");
    for (size_t k = 0; k < out_select_rows.rows().size(); ++k) {
      PADDLE_ENFORCE_EQ(out_select_rows.rows()[k], rows[k % rows.size()]);
    }

    f::Tensor result_tensor;
    f::TensorCopy(rt, cpu_place, *(ctxs_[output_scope_idx]), &result_tensor);
    float *ct = result_tensor.data<float>();

    for (int64_t j = 0; j < f::product(result_tensor.dims()); ++j) {
      ASSERT_NEAR(ct[j], send_vector[j % send_vector.size()], 1e-5);
    }
  }

  void TestReduceLodTensors(size_t output_scope_idx) {
    std::vector<float> send_vector(static_cast<size_t>(f::product(kDims)));
    for (size_t k = 0; k < send_vector.size(); ++k) {
      send_vector[k] = k;
    }
    f::LoD lod{{0, 10, 20}};

    for (size_t input_scope_idx = 0; input_scope_idx < gpu_list_.size();
         ++input_scope_idx) {
      auto in_var = local_scopes_[input_scope_idx]->Var("input");
      auto in_lod_tensor = in_var->GetMutable<f::LoDTensor>();
      in_lod_tensor->mutable_data<float>(kDims, gpu_list_[input_scope_idx]);
      in_lod_tensor->set_lod(lod);

      paddle::framework::TensorFromVector<float>(
          send_vector, *(ctxs_[input_scope_idx]), in_lod_tensor);
    }

    auto out_var = local_scopes_[output_scope_idx]->Var("out");
    auto out_lodtensor = out_var->GetMutable<f::LoDTensor>();

    auto in_var = local_scopes_[output_scope_idx]->Var("input");
    auto in_lodtensor = in_var->Get<f::LoDTensor>();

    out_lodtensor->ShareDataWith(in_lodtensor);

    op_handle_->Run(false);

    WaitAll();

    p::CPUPlace cpu_place;

    auto &rt = out_var->Get<f::LoDTensor>();

    f::Tensor result_tensor;
    f::TensorCopy(rt, cpu_place, *(ctxs_[output_scope_idx]), &result_tensor);
    float *ct = result_tensor.data<float>();

    for (int64_t j = 0; j < f::product(result_tensor.dims()); ++j) {
      ASSERT_NEAR(ct[j], send_vector[j] * gpu_list_.size(), 1e-5);
    }
  }
};

TEST(ReduceTester, TestCPUReduceTestSelectedRows) {
  TestReduceOpHandle test_op;
  size_t input_scope_idx = 0;
  test_op.InitCtxOnGpu(false);
  test_op.InitReduceOp(input_scope_idx);
  test_op.TestReduceSelectedRows(input_scope_idx);
}

// #ifdef PADDLE_WITH_CUDA
//
// TEST(ReduceTester, TestGPUReduceTestSelectedRows) {
//  TestReduceOpHandle test_op;
//  size_t input_scope_idx = 0;
//  test_op.InitCtxOnGpu(true);
//  test_op.InitReduceOp(input_scope_idx);
//  test_op.TestReduceSelectedRows(input_scope_idx);
// }
//
// TEST(ReduceTester, TestCPUReduceTestLodTensor) {
//  TestReduceOpHandle test_op;
//  size_t input_scope_idx = 0;
//  test_op.InitCtxOnGpu(true);
//  test_op.InitReduceOp(input_scope_idx);
//  test_op.TestReduceLodTensors(input_scope_idx);
// }
// #endif

}  // namespace details
}  // namespace framework
}  // namespace paddle
