/*
 * Copyright (c) 2020-2022, NVIDIA CORPORATION.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without modification, are permitted
 * provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright notice, this list of
 *       conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright notice, this list of
 *       conditions and the following disclaimer in the documentation and/or other materials
 *       provided with the distribution.
 *     * Neither the name of the NVIDIA CORPORATION nor the names of its contributors may be used
 *       to endorse or promote products derived from this software without specific prior written
 *       permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL NVIDIA CORPORATION BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TOR (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *//*
 */

/** @file   relative_l2.h
 *  @author Thomas Müller, NVIDIA
 *  @brief  Implementation of the relative l2 loss and its gradient (following Noise2Noise [Lehtinen et al. 2018])
 */

#pragma once

#include <tiny-cuda-nn/common.h>
#include <tiny-cuda-nn/gpu_matrix.h>
#include <tiny-cuda-nn/common_device.h>
#include <tiny-cuda-nn/loss.h>

TCNN_NAMESPACE_BEGIN

template <typename T>
__global__ void relative_l2_loss(
	const uint32_t n_elements,
	const uint32_t stride,
	const uint32_t dims,
	const float loss_scale,
	const T* __restrict__ predictions,
	const float* __restrict__ targets,
	float* __restrict__ values,
	T* __restrict__ gradients,
	const float* __restrict__ data_pdf = nullptr
) {
	const uint32_t i = threadIdx.x + blockIdx.x * blockDim.x;
	if (i >= n_elements) return;

	const uint32_t intra_elem_idx = i % stride;
	const uint32_t inter_elem_idx = i / stride;
	if (intra_elem_idx >= dims) {
		values[i] = 0;
		gradients[i] = 0;
		return;
	}

	const uint32_t target_idx = inter_elem_idx * dims + intra_elem_idx;

	const uint32_t n_total = n_elements / stride * dims;

	const float prediction = (float)predictions[i];
	const float prediction_sq_plus_epsilon = prediction * prediction + 0.01f;

	const float pdf = data_pdf ? data_pdf[target_idx] : 1;
	const float difference = prediction - targets[target_idx];

	values[i] = difference * difference / prediction_sq_plus_epsilon / pdf / n_total;

	float gradient = 2 * difference / prediction_sq_plus_epsilon / pdf;
	gradients[i] = (T)(loss_scale * gradient / n_total);
}

template <typename T>
class RelativeL2Loss : public Loss<T> {
public:
	void evaluate(
		cudaStream_t stream,
		const uint32_t stride,
		const uint32_t dims,
		const float loss_scale,
		const GPUMatrix<T>& prediction,
		const GPUMatrix<float>& target,
		GPUMatrix<float>& values,
		GPUMatrix<T>& gradients,
		const GPUMatrix<float>* data_pdf = nullptr) const override {
		if (prediction.n() != target.n()) {
			throw std::runtime_error(std::string("Prediction and target don't have matching batch size ") + std::to_string(prediction.n()) + "!=" + std::to_string(target.n()));
		}

		if (prediction.m() != stride) {
			throw std::runtime_error(std::string("Prediction does not have appropriate dimensions ") + std::to_string(prediction.m()) + "!=" + std::to_string(stride));
		}

		if (target.m() != dims) {
			throw std::runtime_error(std::string("Target does not have appropriate dimensions ") + std::to_string(target.m()) + "!=" + std::to_string(dims));
		}

		linear_kernel(relative_l2_loss<T>, 0, stream,
			prediction.n_elements(),
			stride,
			dims,
			loss_scale,
			prediction.data(),
			target.data(),
			values.data(),
			gradients.data(),
			data_pdf ? data_pdf->data() : nullptr
		);
	}

	void update_hyperparams(const json& params) override { }

	json hyperparams() const override {
		return {
			{"otype", "RelativeL2"},
		};
	}
};

TCNN_NAMESPACE_END
