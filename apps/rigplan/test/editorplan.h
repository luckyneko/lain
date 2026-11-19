#pragma once

// A rig plan with one of every kind the editor selects, for its driver-free tests: a yawed pole carrying
// two cameras, a turned fixed point carrying a spot, a plane, a box, a static prop and a board with two
// keys, in the New plan's room. Every identity is fixed, so a failure names the same part each run.

#include "document.h" // newPlan

#include <lain/core/length.h>
#include <lain/core/uri.h>
#include <lain/core/uuid.h>
#include <lain/rigplan/rigplan.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace editorplan
{
	inline lain::core::Uuid id(const char* last)
	{
		return *lain::core::Uuid::parse(std::string("00000000-0000-7000-8000-00000000") + last);
	}

	inline const lain::core::Uuid poleId = id("a001");
	inline const lain::core::Uuid bracketId = id("a002");
	inline const lain::core::Uuid cam1Id = id("c001");
	inline const lain::core::Uuid cam2Id = id("c002");
	inline const lain::core::Uuid lightId = id("f001");
	inline const lain::core::Uuid planeId = id("e001");
	inline const lain::core::Uuid boxId = id("e002");
	inline const lain::core::Uuid propId = id("e003");
	inline const lain::core::Uuid boardId = id("b001");

	// A 0.4 m cube standing on its own origin, as an OBJ writes it: Y up, in metres.
	inline void writeCube(const std::filesystem::path& file)
	{
		std::filesystem::create_directories(file.parent_path());
		std::ofstream out(file);
		out << "v -0.2 0.0 -0.2\nv 0.2 0.0 -0.2\nv 0.2 0.4 -0.2\nv -0.2 0.4 -0.2\n"
			   "v -0.2 0.0 0.2\nv 0.2 0.0 0.2\nv 0.2 0.4 0.2\nv -0.2 0.4 0.2\n"
			   "f 1 2 3\nf 1 3 4\nf 5 7 6\nf 5 8 7\nf 1 5 6\nf 1 6 2\n"
			   "f 2 6 7\nf 2 7 3\nf 3 7 8\nf 3 8 4\nf 4 8 5\nf 4 5 1\n";
	}

	// The plan, its prop's mesh `mesh` (which need not exist for a test that draws nothing).
	inline lain::rigplan::RigPlan plan(const lain::core::Uri& mesh)
	{
		lain::rigplan::RigPlan p = rigplan::newPlan();
		const std::string camera = p.specifications.cameras.front().name;
		const std::string light = p.specifications.lights.front().name;

		lain::rigplan::Mount pole;
		pole.id = poleId;
		pole.name = "pole";
		pole.kind = lain::rigplan::Pole{{-2.0, 0.0}, 2.5, 0.05};
		pole.yawDegrees = 30.0;
		p.mounts.push_back(pole);

		lain::rigplan::Mount bracket;
		bracket.id = bracketId;
		bracket.name = "bracket";
		bracket.kind = lain::rigplan::FixedPoint{{2.5, 2.0, 2.2}};
		bracket.yawDegrees = -90.0;
		p.mounts.push_back(bracket);

		lain::rigplan::PlacedCamera cam1;
		cam1.id = cam1Id;
		cam1.name = "cam1";
		cam1.camera = camera;
		cam1.mount = poleId;
		cam1.offset = {0.0, 0.125, 1.5};
		cam1.aim = {10.0, -15.0, 5.0};
		p.cameras.push_back(cam1);

		lain::rigplan::PlacedCamera cam2 = cam1;
		cam2.id = cam2Id;
		cam2.name = "cam2";
		cam2.offset = {0.0, 0.125, 2.2};
		cam2.aim = {0.0, -25.0, 0.0};
		p.cameras.push_back(cam2);

		lain::rigplan::PlacedLight spot;
		spot.id = lightId;
		spot.name = "flood";
		spot.light = light;
		spot.mount = bracketId;
		spot.offset = {0.0, 0.1, -0.3};
		spot.aim = {0.0, -30.0, 0.0};
		p.lights.push_back(spot);

		lain::rigplan::Plane plane;
		plane.id = planeId;
		plane.name = "partition";
		plane.centre = {0.0, 2.0, 1.0};
		plane.aim = {20.0, 0.0, 0.0};
		p.planes.push_back(plane);

		lain::rigplan::Box box;
		box.id = boxId;
		box.name = "crate";
		box.centre = {1.2, -1.0, 0.4};
		box.size = {0.8, 0.8, 0.8};
		box.yawDegrees = 15.0;
		p.boxes.push_back(box);

		lain::rigplan::Prop prop;
		prop.id = propId;
		prop.name = "chair";
		prop.mesh = mesh;
		prop.position = {-1.0, -1.5, 0.0};
		prop.yawDegrees = 40.0;
		p.props.push_back(prop);

		lain::rigplan::BoardMover board;
		board.id = boardId;
		board.name = "board";
		board.pattern.dictionary = lain::camera::board::Dictionary::Aruco5x5_100;
		board.pattern.squaresX = 7;
		board.pattern.squaresY = 5;
		board.pattern.markerToSquare = 0.75;
		board.instance.identity = "board";
		board.instance.squareLength.value = lain::core::Length::from<lain::core::Length::Millimetres>(24.0);
		board.path.keys = {{0, {0.5, -0.5, 1.25}, {-10.0, 10.0, 0.0}}, {10, {0.8, 0.0, 1.4}, {10.0, 0.0, 5.0}}};
		p.boards.push_back(board);

		p.timeline.frames = 12;
		return p;
	}
} // namespace editorplan
