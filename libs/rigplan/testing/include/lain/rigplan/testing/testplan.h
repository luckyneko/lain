#pragma once

// A small rig plan for the rigplan tests: two mounts, three cameras, one light, every tolerance
// non-zero, so a draw that should move something does.

#include <lain/core/uuid.h>
#include <lain/rigplan/rigplan.h>
#include <lain/testing/rootedpath.h>

#include <filesystem>
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

		// Out from the pole's axis, on its side: a camera inside the pole would see nothing else.
		p.cameras.push_back(camera(cam0Id, "cam0", poleId, {0.0, 0.125, 1.5}, {}));
		p.cameras.push_back(camera(cam1Id, "cam1", poleId, {0.0, 0.125, 2.2}, {0.0, -15.0, 0.0}));
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
	inline const lain::core::Uuid partitionId = id("0193a6f0-0000-7000-8000-00000000e001");
	inline const lain::core::Uuid crateId = id("0193a6f0-0000-7000-8000-00000000e002");
	inline const lain::core::Uuid walkerId = id("0193a6f0-0000-7000-8000-00000000e003");

	// The test plan in a room, with a partition, a crate and a walking figure: every kind of material
	// once, and a path of each kind under `folder` (which need not exist; the path rule is lexical).
	//
	// Its paths are spelled generic, as a document stores them. Uri::fromPath keeps the native
	// separators, so on Windows a plan built that way would come back from a save and load as the
	// same files under another spelling, and a round trip compared as text would differ.
	inline lain::rigplan::RigPlan furnished(const std::filesystem::path& folder = lain::testing::rootedPath("projects/hall"))
	{
		lain::rigplan::RigPlan p = plan();
		lain::rigplan::Room room;
		room.size = {10.0, 8.0, 3.2};
		room.floor.material.surface = lain::image::ProceduralTexture{11, 0.05};
		room.north.material.surface = lain::rigplan::ImageTexture{lain::core::Uri{(folder / "textures" / "brick.png").generic_string()}, {0.5, 0.25}};
		room.ceiling.present = false;
		p.room = room;

		lain::rigplan::Plane partition;
		partition.id = partitionId;
		partition.name = "partition";
		partition.centre = {1.0, 2.0, 1.0};
		partition.size = {2.0, 2.0};
		partition.aim = {30.0, 0.0, 0.0};
		partition.material.surface = lain::rigplan::SolidColour{{0.2f, 0.4f, 0.6f}};
		partition.material.specular = {0.5f, 0.5f, 0.5f};
		partition.material.shininess = 16.0;
		p.planes.push_back(partition);

		lain::rigplan::Box crate;
		crate.id = crateId;
		crate.name = "crate";
		crate.centre = {-2.0, 1.0, 0.4};
		crate.size = {0.8, 0.8, 0.8};
		crate.yawDegrees = 15.0;
		p.boxes.push_back(crate);

		lain::rigplan::Prop walker;
		walker.id = walkerId;
		walker.name = "walker";
		walker.mesh = lain::core::Uri{lain::testing::rootedPath("assets/people/walk.<frame:04>.obj").generic_string()}; // outside the folder
		walker.clip = true;
		walker.clipRate = {24, 1};
		walker.unitMetres = 0.01; // modelled in centimetres
		walker.position = {0.5, -1.0, 0.0};
		p.props.push_back(walker);
		return p;
	}

	inline const lain::core::Uuid boardId = id("0193a6f0-0000-7000-8000-00000000b001");

	// M9's board (7 x 5 squares of 24 mm, ARUCO_5X5_100) carried across in front of the pole's cameras:
	// face on to cam0 at frame 0, 0.875 m straight ahead of it, then turned and moved by frame 12 to
	// where it leaves cam0's image on the right, and held there from frame 20.
	inline lain::rigplan::BoardMover board()
	{
		lain::rigplan::BoardMover b;
		b.id = boardId;
		b.name = "charuco";
		b.pattern.dictionary = lain::camera::board::Dictionary::Aruco5x5_100;
		b.pattern.squaresX = 7;
		b.pattern.squaresY = 5;
		b.pattern.markerToSquare = 0.75;
		b.instance.identity = "board-1";
		b.instance.squareLength.value = Length::from<Length::Millimetres>(24.0);
		b.path.keys = {{0, {2.0, 4.0, 1.5}, {0.0, 0.0, 0.0}},
					   {12, {2.3, 4.2, 1.6}, {-20.0, 10.0, 5.0}},
					   {20, {2.3, 4.2, 1.6}, {-20.0, 10.0, 5.0}}};
		return b;
	}
} // namespace testplan
