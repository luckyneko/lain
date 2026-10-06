#include "lain/camera/feature/features.h"

#include <lain/meta/enums.h>
#include <lain/string/format.h>

#include <cmath>
#include <memory>
#include <utility>

namespace lain::camera::feature
{
	// What the backend found, against its contract; the first breach, or nothing when there is none.
	// A pixel may sit anywhere within the source image's pixels, whose centres run from 0 to size − 1.
	static std::optional<std::string> breach(const Features& found, const ImageGeometry& image)
	{
		if (found.kind.empty())
			return std::string("it named no descriptor kind");
		if (found.descriptorBytes == 0 && !found.keypoints.empty())
			return std::string("its descriptors are zero bytes long");
		if (found.descriptors.size() != found.keypoints.size() * found.descriptorBytes)
		{
			return string::format("{} descriptor bytes for {} keypoints of {} bytes each", found.descriptors.size(),
								  found.keypoints.size(), found.descriptorBytes);
		}
		for (std::size_t i = 0; i < found.keypoints.size(); ++i)
		{
			const Keypoint& k = found.keypoints[i];
			if (!std::isfinite(k.pixel.x) || !std::isfinite(k.pixel.y) || !std::isfinite(k.size) ||
				!std::isfinite(k.angle) || !std::isfinite(k.response) || k.size < 0)
				return string::format("keypoint {} is not finite", i);
			if (k.pixel.x < -0.5 || k.pixel.y < -0.5 || k.pixel.x > double(image.width) - 0.5 ||
				k.pixel.y > double(image.height) - 0.5)
			{
				return string::format("keypoint {} at ({}, {}) is outside the {}x{} source image", i, k.pixel.x,
									  k.pixel.y, image.width, image.height);
			}
		}
		return std::nullopt;
	}

	core::Factory<Extractor>& extractorRegistry()
	{
		static core::Factory<Extractor> registry;
		return registry;
	}

	bool canExtract()
	{
		return !extractorRegistry().keys().empty();
	}

	ExtractResult extract(const image::Image& image, const ScalePolicy& policy)
	{
		const core::Time start = core::Time::now();
		ExtractResult out;
		const auto finish = [&](Status status, std::string detail)
		{
			out.status = status;
			out.detail = std::move(detail);
			out.elapsed = core::Time::now() - start;
			return std::move(out);
		};

		const std::vector<std::string> backends = extractorRegistry().keys();
		if (backends.empty())
			return finish(Status::NoBackend, "this build has no feature extractor (configure with -DLAIN_CAMERA_OPENCV=ON)");
		if (!image.valid())
			return finish(Status::Unsupported, "an empty image has no features");

		const ImageGeometry geometry{std::uint32_t(image.width()), std::uint32_t(image.height())};
		const double scale = resolveScale(policy, geometry);
		const std::unique_ptr<Extractor> extractor = extractorRegistry().create(backends.front());
		out.provenance = extractor->provenance();
		std::optional<Features> found = extractor->extract(image, scale);
		if (!found)
		{
			return finish(Status::Unsupported, string::format("the {} extractor cannot read a {} image", backends.front(),
															  meta::enums::name(image.pixelFormat())));
		}
		if (const std::optional<std::string> wrong = breach(*found, geometry))
			return finish(Status::BackendMisbehaved, string::format("the {} extractor's answer is malformed: {}",
																	backends.front(), *wrong));

		// The image and the scale are the facade's to state, so every backend reports them alike.
		found->image = geometry;
		found->scale = scale;
		out.features = std::move(*found);
		return finish(Status::Ok, {});
	}
} // namespace lain::camera::feature
