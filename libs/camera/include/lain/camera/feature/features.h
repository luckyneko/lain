#pragma once

#include "lain/camera/cameramodel.h" // ImageGeometry
#include "lain/camera/feature/status.h"
#include "lain/camera/provenance.h"
#include "lain/camera/scalepolicy.h"

#include <lain/core/factory.h>
#include <lain/core/time.h>
#include <lain/image/image.h>
#include <lain/math/types.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Scene features in one image (CONTEXT.md, "Scene-feature observation"): what a producer backend
// proposes, before lain matches anything (ADR-0016, "Producers propose; lain decides").
namespace lain::camera::feature
{
	// One feature where the backend found it, in the SOURCE image's pixels whatever scale it searched
	// at (CONTEXT.md, "Detection scale policy"): (0, 0) is the centre of the top-left pixel.
	struct Keypoint
	{
		math::Vec2d pixel{0.0};
		double size = 0;	 // the feature's diameter, in source pixels
		double angle = 0;	 // its orientation, radians
		double response = 0; // how strongly the detector responded; larger is stronger
	};

	// Every feature a backend found in one image, with a descriptor each. The descriptors are opaque:
	// only a matcher that accepts `kind` reads them (matching.h).
	struct Features
	{
		std::string kind;				   // the descriptor's kind ("sift"): what a matcher accepts
		std::uint32_t descriptorBytes = 0; // the size of one descriptor
		ImageGeometry image;			   // the source image the pixels are in
		double scale = 1.0;				   // the factor the backend searched at, in (0, 1]
		std::vector<Keypoint> keypoints;
		std::vector<std::uint8_t> descriptors; // keypoints.size() * descriptorBytes, in keypoint order

		std::size_t size() const { return keypoints.size(); }
		const std::uint8_t* descriptor(std::size_t i) const { return descriptors.data() + i * descriptorBytes; }
	};

	// The outcome of extracting features from one image: always a value.
	struct ExtractResult
	{
		Status status = Status::NoSolution;
		std::string detail; // why there are no features, when there are none
		Features features;
		Provenance provenance;
		core::Time elapsed;

		bool ok() const { return status == Status::Ok; }
	};

	// A feature extractor backend (ADR-0004's service shape). It finds features and describes them;
	// the extract() facade checks what it found and fills in the image and the scale, so every backend
	// reports those the same way.
	class Extractor
	{
	public:
		virtual ~Extractor() = default;

		virtual Provenance provenance() const = 0;

		// The features of `image`, searched for at `scale` (already resolved from the policy, in
		// (0, 1]) and reported in source pixels; nullopt when this backend cannot read the image.
		virtual std::optional<Features> extract(const image::Image& image, double scale) const = 0;
	};

	// The process-wide extractor registry, keyed by backend name ("opencv").
	core::Factory<Extractor>& extractorRegistry();

	// Whether this build can extract features at all: whether any backend registered.
	bool canExtract();

	// The features of `image`, searched for at the scale `scale` asks for, through the registered
	// backend. With none registered the result is NoBackend.
	ExtractResult extract(const image::Image& image, const ScalePolicy& scale = NativeScale{});
} // namespace lain::camera::feature
