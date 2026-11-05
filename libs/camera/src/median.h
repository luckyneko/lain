#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>
#include <vector>

// How lain::camera takes a median, wherever it takes one: every registration method and feature-track
// extraction. Private to lain::camera.
namespace lain::camera::detail
{
	// The median of `values`: the middle value, or the mean of the middle two when there is an even
	// count; infinity when there are none.
	inline double median(std::vector<double> values)
	{
		if (values.empty())
			return std::numeric_limits<double>::infinity();
		const std::size_t half = values.size() / 2;
		std::nth_element(values.begin(), values.begin() + std::ptrdiff_t(half), values.end());
		const double upper = values[half];
		if (values.size() % 2 == 1)
			return upper;
		const double lower = *std::max_element(values.begin(), values.begin() + std::ptrdiff_t(half));
		return (lower + upper) / 2;
	}
} // namespace lain::camera::detail
