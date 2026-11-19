#include "orbit.h"

#include <lain/rigplan/realisation.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rigplan
{
	namespace math = lain::math;

	// The orbit's turn: a camera at rest on the plan, panned by its yaw and tilted down by its pitch, so
	// its image's right stays level whatever the two are.
	static math::Quatd turnOf(const Orbit& orbit)
	{
		return lain::rigplan::mountFromPlacement(math::Vec3d{0.0}, lain::rigplan::Aim{orbit.yawDegrees, -orbit.pitchDegrees, 0.0}).rotation();
	}

	// The focal length, in pixels, that gives `image` the orbit's vertical field of view.
	static double focalLength(const Orbit& orbit, lain::camera::ImageGeometry image)
	{
		return 0.5 * double(image.height) / std::tan(math::radians(orbit.verticalFovDegrees) / 2.0);
	}

	lain::render::View viewOf(const Orbit& orbit, lain::camera::ImageGeometry image)
	{
		const math::Quatd turn = turnOf(orbit);
		const math::Vec3d eye = orbit.target - turn * math::Vec3d{0.0, 0.0, orbit.distance};
		const double f = focalLength(orbit, image);

		lain::render::View view;
		view.worldFromCamera = math::RigidTransformd{turn, eye};
		view.image = image;
		view.intrinsics = {f, f, (double(image.width) - 1.0) / 2.0, (double(image.height) - 1.0) / 2.0};
		// In proportion to the distance, so a close look at a camera body and a look at a whole room both
		// keep the depth buffer's precision where the target is.
		view.nearPlane = std::max(0.001, orbit.distance / 200.0);
		view.farPlane = std::max(100.0, orbit.distance * 50.0);
		return view;
	}

	void orbitBy(Orbit& orbit, double yawDegrees, double pitchDegrees)
	{
		orbit.yawDegrees = std::remainder(orbit.yawDegrees + yawDegrees, 360.0);
		orbit.pitchDegrees = std::clamp(orbit.pitchDegrees + pitchDegrees, -Orbit::kMaxPitchDegrees, Orbit::kMaxPitchDegrees);
	}

	void panBy(Orbit& orbit, math::Vec2d drag, lain::camera::ImageGeometry image)
	{
		// A pixel at the target's distance covers distance / f metres. The scene follows the cursor, so the
		// eye, and the target with it, moves the other way along the image's right (camera X) and down
		// (camera Y).
		const math::Quatd turn = turnOf(orbit);
		const double metresPerPixel = orbit.distance / focalLength(orbit, image);
		orbit.target -= turn * math::Vec3d{drag.x * metresPerPixel, drag.y * metresPerPixel, 0.0};
	}

	void dollyBy(Orbit& orbit, double amount)
	{
		orbit.distance = std::clamp(orbit.distance * std::exp(-amount), Orbit::kMinDistance, Orbit::kMaxDistance);
	}

	void zoomBy(Orbit& orbit, double steps)
	{
		dollyBy(orbit, -steps * std::log(0.9));
	}

	Orbit framing(const Orbit& orbit, math::Vec3d lower, math::Vec3d upper, lain::camera::ImageGeometry image)
	{
		Orbit framed = orbit;
		framed.target = (lower + upper) / 2.0;
		const double radius = math::length(upper - lower) / 2.0;
		const double halfHeight = math::radians(orbit.verticalFovDegrees) / 2.0;
		const double halfWidth = std::atan(std::tan(halfHeight) * double(image.width) / double(std::max<std::uint32_t>(1, image.height)));
		// A sphere of `radius` seen from `d` subtends asin(radius / d) each side of its centre; a tenth more
		// leaves the box clear of the pane's edge.
		const double distance = 1.1 * radius / std::sin(std::min(halfHeight, halfWidth));
		framed.distance = std::clamp(distance, Orbit::kMinDistance, Orbit::kMaxDistance);
		return framed;
	}
} // namespace rigplan
