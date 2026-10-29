#pragma once

#include <lain/camera/feature/matching.h>

namespace lain::camera::opencv
{
	// Nearest neighbours of SIFT descriptors behind lain::camera::feature's matcher seam. Exact is a
	// brute-force L2 search, the same on every run; Approximate is FLANN's randomised KD-trees,
	// reproducible only within a tolerance. lain applies the ratio and mutual tests.
	class OpenCVMatcher : public feature::Matcher
	{
	public:
		Provenance provenance() const override;
		bool accepts(std::string_view kind) const override;
		bool supports(feature::MatchSearch search) const override;
		std::vector<std::array<feature::Neighbour, 2>> nearest(const feature::Features& a, const feature::Features& b,
															   feature::MatchSearch search) const override;
	};
} // namespace lain::camera::opencv
