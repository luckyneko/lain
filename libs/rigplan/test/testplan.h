#pragma once

// A small rig plan for the rigplan tests: two mounts, three cameras, one light, every tolerance
// non-zero, so a draw that should move something does.

#include <lain/core/uuid.h>
#include <lain/rigplan/rigplan.h>

#include <string>

namespace testplan
{
	using lain::core::Length;

	inline lain::core::Uuid id(const char* text)
	{
		return *lain::core::Uuid::parse(text);
	}

	// A 1280 x 720 sensor of 2 um pixels, with 20 um of decentring.
	inline lain::rigplan::SensorSpecification sensor(std::string name = "hd")
	{
		lain::rigplan::SensorSpecification s;
		s.name = std::move(name);
		s.image = {1280, 720};
		s.pixelPitch = Length::from<Length::Micrometres>(2.0);
		s.principalPointTolerance = Length::from<Length::Micrometres>(20.0);
		return s;
	}

	// A 4 mm lens with mild barrel distortion, 1% on focal length and 10% on distortion.
	inline lain::rigplan::LensSpecification lens(std::string name = "4mm")
	{
		lain::rigplan::LensSpecification l;
		l.name = std::move(name);
		l.focalLength = Length::from<Length::Millimetres>(4.0);
		l.distortion = lain::camera::BrownConrady5{-0.05, 0.01, 0.0, 0.0, 0.0};
		l.focalLengthTolerance = 0.01;
		l.distortionTolerance = 0.1;
		return l;
	}

	inline const lain::core::Uuid poleId = id("0193a6f0-0000-7000-8000-00000000a001");
	inline const lain::core::Uuid wallId = id("0193a6f0-0000-7000-8000-00000000a002");
	inline const lain::core::Uuid cam0Id = id("0193a6f0-0000-7000-8000-00000000c000");
	inline const lain::core::Uuid cam1Id = id("0193a6f0-0000-7000-8000-00000000c001");
	inline const lain::core::Uuid cam2Id = id("0193a6f0-0000-7000-8000-00000000c002");
	inline const lain::core::Uuid lightId = id("0193a6f0-0000-7000-8000-00000000f000");

	inline lain::rigplan::PlacedCamera camera(const lain::core::Uuid& cameraId, std::string name,
											  const lain::core::Uuid& mount, lain::math::Vec3d offset, lain::rigplan::Aim aim)
	{
		lain::rigplan::PlacedCamera c;
		c.id = cameraId;
		c.name = std::move(name);
		c.camera = "hd-4mm";
		c.mount = mount;
		c.offset = offset;
		c.aim = aim;
		c.tolerance.offset = Length::from<Length::Millimetres>(5.0);
		c.tolerance.panDegrees = 0.5;
		c.tolerance.tiltDegrees = 0.5;
		c.tolerance.rollDegrees = 0.5;
		return c;
	}

	// A pole at (2, 3) with two cameras, a wall bracket across the room with one camera and a spot,
	// realised at full scale with seed 7.
	inline lain::rigplan::RigPlan plan()
	{
		lain::rigplan::RigPlan p;
		p.specifications.sensors.push_back(sensor());
		p.specifications.lenses.push_back(lens());
		p.specifications.cameras.push_back({"hd-4mm", "hd", "4mm"});
		lain::rigplan::LightSpecification spot;
		spot.name = "flood";
		spot.kind = lain::rigplan::LightKind::Spot;
		p.specifications.lights.push_back(spot);

		lain::rigplan::Mount pole;
		pole.id = poleId;
		pole.name = "north pole";
		pole.kind = lain::rigplan::Pole{{2.0, 3.0}, 2.5, 0.05};
		pole.tolerance.position = Length::from<Length::Millimetres>(10.0);
		pole.tolerance.yawDegrees = 1.0;
		p.mounts.push_back(pole);

		lain::rigplan::Mount wall;
		wall.id = wallId;
		wall.name = "east wall";
		wall.kind = lain::rigplan::FixedPoint{{6.0, 3.0, 2.8}};
		wall.yawDegrees = 90.0;
		wall.tolerance.position = Length::from<Length::Millimetres>(10.0);
		wall.tolerance.yawDegrees = 1.0;
		p.mounts.push_back(wall);

		p.cameras.push_back(camera(cam0Id, "cam0", poleId, {0.0, 0.0, 1.5}, {}));
		p.cameras.push_back(camera(cam1Id, "cam1", poleId, {0.0, 0.0, 2.2}, {0.0, -15.0, 0.0}));
		p.cameras.push_back(camera(cam2Id, "cam2", wallId, {0.0, 0.1, 0.0}, {0.0, -30.0, 0.0}));

		lain::rigplan::PlacedLight light;
		light.id = lightId;
		light.name = "flood";
		light.light = "flood";
		light.mount = wallId;
		light.aim = {0.0, -45.0, 0.0};
		p.lights.push_back(light);

		p.timeline.frames = 24;
		p.seed = 7;
		p.scales.intrinsic = 1.0;
		return p;
	}
} // namespace testplan
