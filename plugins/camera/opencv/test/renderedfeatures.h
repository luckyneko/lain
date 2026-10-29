#pragma once

// What the OpenCV feature cases share (test_features.cpp, test_sequencefeatures.cpp): the backend
// registered once, a camera of any size, and features grouped and capped as extraction groups and
// caps them, restated so a case can measure a step extraction takes on its own.

#include "testcamera.h"

#include <lain/camera/cameramodel.h>
#include <lain/camera/feature/features.h>
#include <lain/camera/opencv/register.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace lain::camera::testing
{
	inline void ensureBackend()
	{
		static const bool once = []
		{
			opencv::registerBackend();
			return true;
		}();
		(void)once;
	}

	// A camera of `width` x `height` with the test camera's field of view (f 500 at 640 wide), its
	// principal point in the middle, through `distortion`: for the large-image cases, and for the
	// distortion sweep, where one k1 is a different lens at each field of view.
	inline CameraModel largeCamera(std::uint32_t width, std::uint32_t height, const Distortion& distortion = NoDistortion{})
	{
		CameraModelParameters p = parametersWith(distortion);
		const double f = 500.0 * double(width) / 640.0;
		p.image = {width, height};
		p.intrinsics = {f, f, 0.5 * double(width) - 0.5, 0.5 * double(height) - 0.5};
		ModelResult result = CameraModel::create(p);
		REQUIRE(result.model.has_value());
		return *result.model;
	}

	inline std::pair<long long, long long> cellOf(const feature::Keypoint& k)
	{
		return {std::llround(k.pixel.x * 64), std::llround(k.pixel.y * 64)};
	}

	// The rows of `f` by 1/64-pixel cell, as extraction groups a feature's orientations.
	inline std::map<std::pair<long long, long long>, std::vector<std::uint32_t>> cells(const feature::Features& f)
	{
		std::map<std::pair<long long, long long>, std::vector<std::uint32_t>> out;
		for (std::uint32_t i = 0; i < f.size(); ++i)
			out[cellOf(f.keypoints[i])].push_back(i);
		return out;
	}

	inline feature::Features subset(const feature::Features& f, const std::vector<std::uint32_t>& keep)
	{
		feature::Features out = f;
		out.keypoints.clear();
		out.descriptors.clear();
		for (const std::uint32_t i : keep)
		{
			out.keypoints.push_back(f.keypoints[i]);
			out.descriptors.insert(out.descriptors.end(), f.descriptor(i), f.descriptor(i) + f.descriptorBytes);
		}
		return out;
	}

	// The `n` strongest features, each with all its orientations, as extraction caps a frame.
	inline feature::Features strongest(const feature::Features& f, std::size_t n)
	{
		std::vector<std::vector<std::uint32_t>> features;
		for (const auto& [cell, members] : cells(f))
			features.push_back(members);
		const auto response = [&f](const std::vector<std::uint32_t>& rows)
		{
			double best = 0;
			for (const std::uint32_t r : rows)
				best = std::max(best, f.keypoints[r].response);
			return best;
		};
		std::stable_sort(features.begin(), features.end(), [&](const auto& a, const auto& b)
						 { return response(a) > response(b); });
		features.resize(std::min(n, features.size()));
		std::vector<std::uint32_t> rows;
		for (const std::vector<std::uint32_t>& feature : features)
			rows.insert(rows.end(), feature.begin(), feature.end());
		return subset(f, rows);
	}
} // namespace lain::camera::testing
