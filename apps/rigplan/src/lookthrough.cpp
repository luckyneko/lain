#include "lookthrough.h"

#include <lain/image/convert.h>
#include <lain/rigplan/specification.h>
#include <lain/string/format.h>

#include <algorithm>
#include <utility>

namespace rigplan
{
	using lain::core::Uuid;
	using lain::string::format;
	namespace rp = lain::rigplan;

	static const rp::PlacedCamera* placedCamera(const rp::RigPlan& plan, const Uuid& id)
	{
		const auto found = std::find_if(plan.cameras.begin(), plan.cameras.end(), [&](const rp::PlacedCamera& camera)
										{ return camera.id == id; });
		return found != plan.cameras.end() ? &*found : nullptr;
	}

	void follow(LookChoice& choice, const rp::RigPlan& plan, const Selection& selection)
	{
		if (choice.camera && !placedCamera(plan, *choice.camera))
		{
			choice.camera.reset();
			choice.pinned = false;
		}
		if (selection.kind == Selected::Camera && !choice.pinned && placedCamera(plan, selection.id))
			choice.camera = selection.id;
	}

	LookThrough lookThrough(const rp::RigPlan& plan, const Uuid& camera, LookModel model)
	{
		LookThrough look;
		const rp::PlacedCamera* placed = placedCamera(plan, camera);
		if (!placed)
		{
			look.problems.push_back("the camera looked through is no longer in the plan");
			return look;
		}
		look.name = placed->name;

		const rp::CameraSpecification* specification = rp::findCamera(plan.specifications, placed->camera);
		const rp::SensorSpecification* sensor = specification ? rp::findSensor(plan.specifications, specification->sensor) : nullptr;
		if (sensor)
			look.recorder = rp::Recorder::create(sensor->sensorModel);
		if (!look.recorder)
			look.problems.push_back(format("camera '{}' has no sensor that records: see Issues", look.name));

		if (model == LookModel::Nominal)
		{
			rp::NominalLayout layout = rp::nominalLayout(plan);
			for (const rp::NominalLayout::Camera& c : layout.cameras)
			{
				if (c.id != camera)
					continue;
				if (c.planFromCamera)
					look.planFromCamera = *c.planFromCamera;
				else
					look.problems.push_back(format("camera '{}' has no pose as planned: see Issues", look.name));
				look.model = c.model;
				if (!c.model)
					look.problems.push_back(format("camera '{}' has no nominal model: see Issues", look.name));
			}
			look.layout = std::move(layout);
			return look;
		}

		// Realised, or nothing: a capture is rendered through the realisation, so the nominal camera in its
		// place would show a picture no capture of this plan holds.
		rp::RealisationResult drawn = rp::realise(plan);
		if (!drawn.realisation)
		{
			for (const rp::RealisationDiagnostic& diagnostic : drawn.diagnostics)
				look.problems.push_back(diagnostic.detail);
			return look;
		}
		for (const rp::RealisedCamera& c : drawn.realisation->cameras)
		{
			if (c.id != camera)
				continue;
			look.model = c.model;
			look.planFromCamera = c.planFromCamera;
		}
		if (!look.model)
			look.problems.push_back(format("camera '{}' is not in the plan's realisation", look.name));
		look.layout = std::move(*drawn.realisation);
		return look;
	}

	rp::SceneResult lookThroughScene(const rp::RigPlan& plan, const LookThrough& look, const rp::SceneAssets& assets, std::size_t frame)
	{
		return std::visit([&](const auto& layout)
						  { return rp::renderScene(plan, layout, assets, frame); }, look.layout);
	}

	lain::render::CaptureView captureView(const LookThrough& look)
	{
		return {look.planFromCamera};
	}

	static constexpr std::uint32_t kDivisors[] = {1, 2, 4, 8};

	static lain::camera::ImageGeometry dividedBy(const lain::camera::ImageGeometry& image, std::uint32_t divisor)
	{
		return {std::max<std::uint32_t>(1, image.width / divisor), std::max<std::uint32_t>(1, image.height / divisor)};
	}

	std::uint32_t lookDivisor(const lain::camera::ImageGeometry& image, std::uint32_t width, std::uint32_t height)
	{
		for (const std::uint32_t divisor : kDivisors)
		{
			const lain::camera::ImageGeometry at = dividedBy(image, divisor);
			if (at.width <= width && at.height <= height)
				return divisor;
		}
		return kDivisors[std::size(kDivisors) - 1];
	}

	lain::camera::ModelResult modelAtDivisor(const lain::camera::CameraModel& model, std::uint32_t divisor)
	{
		return lain::camera::scaled(model, dividedBy(model.image(), std::max<std::uint32_t>(1, divisor)));
	}

	lain::render::CaptureSettings settingsAtDivisor(const lain::render::CaptureSettings& settings, std::uint32_t divisor)
	{
		lain::render::CaptureSettings at = settings;
		at.interpolationTolerance *= double(std::max<std::uint32_t>(1, divisor));
		return at;
	}

	lain::image::Image displayed(const rp::Recorder& recorder, const lain::image::Image& light)
	{
		namespace image = lain::image;
		const image::Image recorded = recorder.record(light, rp::NoiseKey{}, 0.0);
		if (!recorded.valid())
			return {};
		// To float first, a Grey recording replicated and an opaque alpha added, still in its own curve;
		// then its curve undone and sRGB's applied in one pass; then to 8 bits.
		const image::Image wide = image::convert(recorded, image::PixelFormat::RGBA32F);
		return image::convert(image::convert(wide, image::ColorSpace::sRGB), image::PixelFormat::RGBA8);
	}
} // namespace rigplan
