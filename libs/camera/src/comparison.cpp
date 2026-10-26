#include "lain/camera/comparison.h"

#include "angles.h"
#include "lain/camera/projection.h"

#include <lain/string/format.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace lain::camera
{
	// A model comparison's sampling step, in pixels along each axis.
	static constexpr std::uint32_t kSampleStep = 16;

	// The pixel centres sampled along an axis of `size` pixels: every kSampleStep-th from 0, then the
	// last, unless the step landed on it already.
	static std::vector<double> samplesAlong(std::uint32_t size)
	{
		std::vector<double> at;
		for (std::uint32_t i = 0; i < size; i += kSampleStep)
			at.push_back(double(i));
		if (size > 0 && (size - 1) % kSampleStep != 0)
			at.push_back(double(size - 1));
		return at;
	}

	ModelComparison compareModels(const CameraModel& expected, const CameraModel& found)
	{
		ModelComparison out;
		const ImageGeometry& image = expected.image();
		if (!(image == found.image()))
		{
			out.status = ModelComparisonStatus::DifferentImages;
			out.detail = lain::string::format("the expected model is {}x{} and the found one {}x{}", image.width,
											  image.height, found.image().width, found.image().height);
			return out;
		}

		out.focalDifference = std::max(std::abs(found.intrinsics().fx / expected.intrinsics().fx - 1),
									   std::abs(found.intrinsics().fy / expected.intrinsics().fy - 1));

		const std::vector<double> columns = samplesAlong(image.width);
		const std::vector<double> rows = samplesAlong(image.height);
		double sum = 0;
		for (const double v : rows)
		{
			for (const double u : columns)
			{
				double distance = std::numeric_limits<double>::infinity();
				const Unprojection<double> ray = unproject(expected, u, v);
				if (ray.ok())
				{
					const Projection<double> p = project(found, ray.x, ray.y, ray.z);
					if (p.ok())
						distance = std::hypot(p.u - u, p.v - v);
				}
				if (!std::isfinite(distance))
					++out.unreached;
				if (distance > out.worstPixels)
				{
					out.worstPixels = distance;
					out.worstAt = {u, v};
				}
				sum += distance;
				++out.samples;
			}
		}
		out.meanPixels = out.samples > 0 ? sum / double(out.samples) : 0.0;
		return out;
	}

	std::string ModelComparison::toString() const
	{
		if (!compared())
			return "not compared: " + detail;
		if (unreached > 0)
			return lain::string::format("{} of {} rays the found model cannot reach, the first at ({}, {}); focal {:.3g}%",
										unreached, samples, worstAt.x, worstAt.y, focalDifference * 100.0);
		return lain::string::format("mean {:.3g} px, worst {:.3g} px at ({}, {}), focal {:.3g}%, {} rays", meanPixels,
									worstPixels, worstAt.x, worstAt.y, focalDifference * 100.0, samples);
	}

	RigComparison compareRigs(const registration::RegisteredRig& expected, const registration::RegisteredRig& found)
	{
		RigComparison out;
		out.reference = found.reference;
		out.expectedScale = expected.scale.scale;
		out.foundScale = found.scale.scale;

		// The expected rig, derived again for found's reference when it is relative to another camera:
		// every transform taken from that camera's instead, and the reference the identity exactly, as a
		// rig document requires, not a product that rounds to it (rigplan's rigRelativeTo, the same rule).
		const std::optional<math::RigidTransformd> pivot = expected.referenceFromCamera(found.reference);
		if (!pivot)
		{
			out.status = RigComparisonStatus::UnknownReference;
			out.detail = lain::string::format("the found rig is relative to {}, which the expected rig does not hold",
											  found.reference.value);
			return out;
		}
		out.derivedAgain = expected.reference != found.reference;
		const math::RigidTransformd foundReferenceFromExpectedReference = pivot->inverse();
		const auto expectedOf = [&](const registration::RegisteredCamera& camera)
		{
			if (camera.camera == found.reference)
				return math::RigidTransformd{};
			return out.derivedAgain ? foundReferenceFromExpectedReference * camera.referenceFromCamera
									: camera.referenceFromCamera;
		};

		// Paired by identity, in found's order.
		struct Pair
		{
			capture::CameraIdentity camera;
			math::RigidTransformd expected;
			math::RigidTransformd found;
		};
		std::vector<Pair> pairs;
		for (const registration::RegisteredCamera& f : found.cameras)
		{
			const auto e = std::find_if(expected.cameras.begin(), expected.cameras.end(),
										[&](const registration::RegisteredCamera& c)
										{ return c.camera == f.camera; });
			if (e == expected.cameras.end())
				out.onlyFound.push_back(f.camera);
			else
				pairs.push_back({f.camera, expectedOf(*e), f.referenceFromCamera});
		}
		for (const registration::RegisteredCamera& e : expected.cameras)
		{
			if (!found.referenceFromCamera(e.camera))
				out.onlyExpected.push_back(e.camera);
		}

		double across = 0, along = 0;
		for (const Pair& p : pairs)
		{
			across += math::dot(p.found.translation(), p.expected.translation());
			along += math::dot(p.found.translation(), p.found.translation());
		}
		out.scale = along > 0 ? across / along : 1.0;
		for (const Pair& p : pairs)
		{
			out.cameras.push_back({p.camera, detail::rotationBetween(p.expected, p.found),
								   math::length(p.found.translation() * out.scale - p.expected.translation())});
		}
		return out;
	}

	// The first camera whose `field` is largest.
	static const CameraDifference* worstOf(const std::vector<CameraDifference>& cameras,
										   double CameraDifference::* field)
	{
		const CameraDifference* worst = nullptr;
		for (const CameraDifference& c : cameras)
		{
			if (!worst || c.*field > worst->*field)
				worst = &c;
		}
		return worst;
	}

	const CameraDifference* RigComparison::worstRotation() const
	{
		return worstOf(cameras, &CameraDifference::rotation);
	}

	const CameraDifference* RigComparison::worstCentre() const
	{
		return worstOf(cameras, &CameraDifference::centre);
	}

	static std::string joined(const std::vector<capture::CameraIdentity>& cameras)
	{
		std::string text;
		for (const capture::CameraIdentity& c : cameras)
			text += (text.empty() ? "" : ", ") + c.value;
		return text;
	}

	std::string RigComparison::toString() const
	{
		if (!compared())
			return "not compared: " + detail;
		const CameraDifference* rotation = worstRotation();
		const CameraDifference* centre = worstCentre();
		const bool metric = expectedScale == registration::Scale::Metric;
		std::string text = lain::string::format(
			"{} cameras relative to {}{}, found scaled by {:.6g}; worst rotation {:.3g} mrad ({}), worst centre {:.3g}{} ({})",
			cameras.size(), reference.value, derivedAgain ? " (the expected rig derived again for it)" : "", scale,
			rotation->rotation * 1e3, rotation->camera.value, metric ? centre->centre * 1e3 : centre->centre,
			metric ? " mm" : "", centre->camera.value);
		if (!onlyExpected.empty())
			text += "; only expected: " + joined(onlyExpected);
		if (!onlyFound.empty())
			text += "; only found: " + joined(onlyFound);
		return text;
	}
} // namespace lain::camera
