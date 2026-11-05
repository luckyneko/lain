#pragma once

#include <lain/camera/feature/features.h>

namespace lain::camera::opencv
{
	// SIFT behind lain::camera::feature's extractor seam: 128-byte CV_8U descriptors of kind "sift",
	// searched at the factor the facade resolved and reported in source pixels.
	class SiftExtractor : public feature::Extractor
	{
	public:
		Provenance provenance() const override;
		std::optional<feature::Features> extract(const image::Image& image, double scale) const override;
	};
} // namespace lain::camera::opencv
