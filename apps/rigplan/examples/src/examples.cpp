#include "examples.h"

#include <lain/camera/board/pattern.h>
#include <lain/camera/board/specification.h>
#include <lain/camera/distortion.h>
#include <lain/core/length.h>
#include <lain/core/uri.h>
#include <lain/core/uuid.h>
#include <lain/data/value.h>
#include <lain/image/procedural.h>
#include <lain/io/data/save.h>
#include <lain/rigplan/serialize/rigplan.h>

#include <optional>
#include <stdexcept>
#include <string>

namespace rigplan::examples
{
	using lain::core::Length;
	using lain::core::Uuid;
	namespace rp = lain::rigplan;

	// A uuid for an example's own part, written as it is stated here, so the document is the same bytes
	// on every rewrite. The last four hex digits tell the parts apart: a0.. mounts, c0.. cameras, f0..
	// lights, b0.. boards.
	static Uuid id(const char* last)
	{
		const std::string text = std::string("00000000-0000-7000-8000-00000000") + last;
		const std::optional<Uuid> parsed = Uuid::parse(text);
		if (!parsed)
			throw std::runtime_error("rigplan-examples: '" + text + "' is no uuid");
		return *parsed;
	}

	// --- three-poles --------------------------------------------------------------------------------
	//
	// A rig of six cameras round one working point at (0, 0, 1.25), the size of a desk or hand-capture
	// rig: three poles on an arc of 1 m to its south, at -40, 0 and +40 degrees, each turned to face it
	// and carrying a camera at 0.9 m looking up and one at 1.6 m looking down, both 12.5 cm out from
	// the pole toward the point, and a spot near the top aimed at it. M9's board (7 x 5 squares of
	// 24 mm, the board flowview's register.json detects) turns through the working point, so every
	// camera sees it in most frames and the observation graph is joined across the poles. A camera is
	// about 0.94 m from the board, so its 2000 px focal length puts about 51 px across a square, as
	// close as M15 slice 5c's capture detected to 0.06 to 0.09 px.
	//
	// It is stored as the vertical renders it (M15 slice 7): intrinsic scale 1 with installation locked
	// to it, and the sensor's own noise, so the plan alone reproduces that capture. Registered with its
	// truth/models the rig comes back near-exactly; with its nominal/ models, measurably worse.

	static rp::SensorSpecification sensor()
	{
		rp::SensorSpecification s;
		s.name = "mono-720p";
		s.image = {1280, 720};
		s.pixelPitch = Length::from<Length::Micrometres>(2.0);
		s.principalPointTolerance = Length::from<Length::Micrometres>(20.0); // 10 px either way
		// A monochrome machine-vision sensor: the room's procedural surfaces and the board are grey
		// anyway, and one channel is a third of the bytes to record, encode and write.
		s.sensorModel.channels = rp::SensorChannels::Grey;
		return s;
	}

	static rp::LensSpecification lens()
	{
		rp::LensSpecification l;
		l.name = "4mm";
		l.focalLength = Length::from<Length::Millimetres>(4.0);
		l.distortion = lain::camera::BrownConrady5{-0.05, 0.01, 0.0, 0.0, 0.0};
		l.focalLengthTolerance = 0.01;
		l.distortionTolerance = 0.1;
		return l;
	}

	static rp::Mount pole(const char* mountId, const char* name, lain::math::Vec2d floor, double yawDegrees)
	{
		rp::Mount m;
		m.id = id(mountId);
		m.name = name;
		m.kind = rp::Pole{floor, 2.2, 0.05};
		m.yawDegrees = yawDegrees;
		m.tolerance.position = Length::from<Length::Millimetres>(10.0);
		m.tolerance.yawDegrees = 1.0;
		return m;
	}

	static rp::PlacedCamera camera(const char* cameraId, const char* name, const char* mountId, double height, double tiltDegrees)
	{
		rp::PlacedCamera c;
		c.id = id(cameraId);
		c.name = name;
		c.camera = "mono-720p-4mm";
		c.mount = id(mountId);
		c.offset = {0.0, 0.125, height};
		c.aim = {0.0, tiltDegrees, 0.0};
		c.tolerance.offset = Length::from<Length::Millimetres>(5.0);
		c.tolerance.panDegrees = 0.5;
		c.tolerance.tiltDegrees = 0.5;
		c.tolerance.rollDegrees = 0.5;
		return c;
	}

	static rp::PlacedLight light(const char* lightId, const char* name, const char* mountId)
	{
		rp::PlacedLight l;
		l.id = id(lightId);
		l.name = name;
		l.light = "spot";
		l.mount = id(mountId);
		l.offset = {0.0, 0.125, 2.0};
		l.aim = {0.0, -40.0, 0.0}; // down at the working point, 0.75 m below and 0.875 m ahead
		return l;
	}

	static rp::RigPlan threePoles()
	{
		rp::RigPlan p;
		p.specifications.sensors.push_back(sensor());
		p.specifications.lenses.push_back(lens());
		p.specifications.cameras.push_back({"mono-720p-4mm", "mono-720p", "4mm"});
		rp::LightSpecification spot;
		spot.name = "spot";
		spot.kind = rp::LightKind::Spot;
		spot.intensity = 0.45;
		spot.innerConeDegrees = 25;
		spot.outerConeDegrees = 40;
		p.specifications.lights.push_back(spot);

		rp::Room room;
		room.size = {5.0, 5.0, 2.7};
		rp::Material wall;
		wall.surface = lain::image::ProceduralTexture{1, 0.04};
		for (rp::RoomSurface* surface : {&room.floor, &room.ceiling, &room.north, &room.south, &room.east, &room.west})
			surface->material = wall; // one material: each surface's identity is mixed into its seed
		p.room = room;

		p.mounts.push_back(pole("a001", "left pole", {-0.643, -0.766}, -40.0));
		p.mounts.push_back(pole("a002", "middle pole", {0.0, -1.0}, 0.0));
		p.mounts.push_back(pole("a003", "right pole", {0.643, -0.766}, 40.0));

		p.cameras.push_back(camera("c001", "left-low", "a001", 0.9, 22.0));
		p.cameras.push_back(camera("c002", "left-high", "a001", 1.6, -22.0));
		p.cameras.push_back(camera("c003", "middle-low", "a002", 0.9, 22.0));
		p.cameras.push_back(camera("c004", "middle-high", "a002", 1.6, -22.0));
		p.cameras.push_back(camera("c005", "right-low", "a003", 0.9, 22.0));
		p.cameras.push_back(camera("c006", "right-high", "a003", 1.6, -22.0));

		p.lights.push_back(light("f001", "left spot", "a001"));
		p.lights.push_back(light("f002", "middle spot", "a002"));
		p.lights.push_back(light("f003", "right spot", "a003"));

		// A key places the centre of the squares, aimed as a camera that sees it face on: a pan of -40
		// faces the left pole's cameras, a tilt up faces the low ones.
		rp::BoardMover board;
		board.id = id("b001");
		board.name = "charuco";
		board.pattern.dictionary = lain::camera::board::Dictionary::Aruco5x5_100;
		board.pattern.squaresX = 7;
		board.pattern.squaresY = 5;
		board.pattern.markerToSquare = 0.75;
		board.instance.identity = "board";
		board.instance.squareLength.value = Length::from<Length::Millimetres>(24.0);
		board.path.keys = {{0, {-0.04, 0.0, 1.27}, {-25.0, 12.0, 0.0}},
						   {5, {0.03, 0.02, 1.22}, {-8.0, -15.0, 12.0}},
						   {11, {0.0, -0.02, 1.28}, {15.0, 15.0, -10.0}},
						   {17, {-0.03, 0.01, 1.23}, {28.0, -10.0, 6.0}},
						   {23, {0.03, 0.0, 1.26}, {2.0, 5.0, -15.0}}};
		p.boards.push_back(board);

		p.timeline.frames = 24;
		p.timeline.rate = {30, 1};
		p.seed = 15;
		p.scales.intrinsic = 1.0;
		p.scales.installation = 1.0;
		p.scales.locked = true;
		p.scales.noise = 1.0;
		return p;
	}

	// --- two-methods --------------------------------------------------------------------------------
	//
	// One rig registered both ways from the same footage (NOTES.txt follow-up 6): four fixed cameras over a
	// textured room, which targetless registration places them by, and a printed board moving through
	// their shared view, which board registration places them by and targetless registration drops as
	// moving content. It is M9's comparison scene (plugins/camera/test/test_comparison.cpp) in the plan
	// frame:
	//   * a 6 x 4.5 x 2.4 m room, every surface procedural, with a 0.8 x 0.6 x 0.9 m box on the floor;
	//   * four cameras 0.6 m apart on an arc of 3.2 m round (-0.2, 0.4, 0.6), 0.5 m above it and aimed
	//     at it: 1280 x 720 at 2 um behind a 2 mm Brown-Conrady lens, f = 1000 px;
	//   * a 7 x 5 board of 80 mm squares 1.6 m in front of them, about 50 px a square, on a Lissajous
	//     path that turns it as it goes, so no corner stays still on a camera's line of sight;
	//   * four point lamps near the ceiling.
	// The poses are the comparison's, worked out by lain's lookingAt and aimOf and written here as
	// literals rounded to 0.1 mm and 0.0001 degrees, so the document is the same bytes on every platform.
	// Measured before committing, over seeds 1 to 3 on a hardware GPU and the CPU device: both methods
	// Ready, every camera seeing the board in all 16 frames, nothing saturating (WORK.md, *Fixed-rig
	// footage from committed rig plans*).

	static rp::SensorSpecification monoHd()
	{
		rp::SensorSpecification s;
		s.name = "mono-720p";
		s.image = {1280, 720};
		s.pixelPitch = Length::from<Length::Micrometres>(2.0);
		s.principalPointTolerance = Length::from<Length::Micrometres>(20.0); // 10 px either way
		s.sensorModel.channels = rp::SensorChannels::Grey;
		return s;
	}

	static rp::LensSpecification lens2mm()
	{
		rp::LensSpecification l;
		l.name = "2mm";
		l.focalLength = Length::from<Length::Millimetres>(2.0);
		l.distortion = lain::camera::BrownConrady5{0.1, -0.05, 0.001, -0.002, 0.01}; // the comparison's lens
		l.focalLengthTolerance = 0.01;
		l.distortionTolerance = 0.1;
		return l;
	}

	static rp::Mount fixedPoint(const char* mountId, const char* name, lain::math::Vec3d position, bool tolerant)
	{
		rp::Mount m;
		m.id = id(mountId);
		m.name = name;
		m.kind = rp::FixedPoint{position};
		if (tolerant)
		{
			m.tolerance.position = Length::from<Length::Millimetres>(10.0);
			m.tolerance.yawDegrees = 1.0;
		}
		return m;
	}

	static rp::PlacedCamera fixedCamera(const char* cameraId, const char* name, const char* mountId, rp::Aim aim)
	{
		rp::PlacedCamera c;
		c.id = id(cameraId);
		c.name = name;
		c.camera = "mono-720p-2mm";
		c.mount = id(mountId);
		c.aim = aim;
		c.tolerance.offset = Length::from<Length::Millimetres>(5.0);
		c.tolerance.panDegrees = 0.5;
		c.tolerance.tiltDegrees = 0.5;
		c.tolerance.rollDegrees = 0.5;
		return c;
	}

	static rp::PlacedLight lamp(const char* lightId, const char* name, const char* mountId)
	{
		rp::PlacedLight l;
		l.id = id(lightId);
		l.name = name;
		l.light = "lamp";
		l.mount = id(mountId);
		return l;
	}

	static rp::RigPlan twoMethods()
	{
		rp::RigPlan p;
		p.specifications.sensors.push_back(monoHd());
		p.specifications.lenses.push_back(lens2mm());
		p.specifications.cameras.push_back({"mono-720p-2mm", "mono-720p", "2mm"});
		rp::LightSpecification point;
		point.name = "lamp";
		point.kind = rp::LightKind::Point;
		point.intensity = 1.15; // the most light short of the board's white saturating
		p.specifications.lights.push_back(point);

		rp::Room room;
		room.centre = {0.0, -0.75};
		room.size = {6.0, 4.5, 2.4};
		rp::Material surface;
		surface.surface = lain::image::ProceduralTexture{1, 0.04};
		for (rp::RoomSurface* s : {&room.floor, &room.ceiling, &room.north, &room.south, &room.east, &room.west})
			s->material = surface; // one material: each surface's identity is mixed into its seed
		p.room = room;
		rp::Box box;
		box.id = id("d001");
		box.name = "box";
		box.centre = {-0.2, 0.1, 0.45};
		box.size = {0.8, 0.6, 0.9};
		box.material = surface;
		p.boxes.push_back(box);

		p.mounts.push_back(fixedPoint("a001", "cam0 stand", {-1.0843, -2.6754, 1.1}, true));
		p.mounts.push_back(fixedPoint("a002", "cam1 stand", {-0.5004, -2.7859, 1.1}, true));
		p.mounts.push_back(fixedPoint("a003", "cam2 stand", {0.1004, -2.7859, 1.1}, true));
		p.mounts.push_back(fixedPoint("a004", "cam3 stand", {0.6843, -2.6754, 1.1}, true));
		p.mounts.push_back(fixedPoint("a101", "lamp 1 hook", {-1.5, -1.8, 2.3}, false));
		p.mounts.push_back(fixedPoint("a102", "lamp 2 hook", {1.5, -1.8, 2.3}, false));
		p.mounts.push_back(fixedPoint("a103", "lamp 3 hook", {-1.5, 0.4, 2.3}, false));
		p.mounts.push_back(fixedPoint("a104", "lamp 4 hook", {1.5, 0.4, 2.3}, false));

		p.cameras.push_back(fixedCamera("c001", "cam0", "a001", {-16.0421, -8.8806, 0.0}));
		p.cameras.push_back(fixedCamera("c002", "cam1", "a002", {-5.3865, -8.8806, 0.0}));
		p.cameras.push_back(fixedCamera("c003", "cam2", "a003", {5.3865, -8.8806, 0.0}));
		p.cameras.push_back(fixedCamera("c004", "cam3", "a004", {16.0421, -8.8806, 0.0}));

		p.lights.push_back(lamp("f001", "lamp 1", "a101"));
		p.lights.push_back(lamp("f002", "lamp 2", "a102"));
		p.lights.push_back(lamp("f003", "lamp 3", "a103"));
		p.lights.push_back(lamp("f004", "lamp 4", "a104"));

		rp::BoardMover board;
		board.id = id("b001");
		board.name = "charuco";
		board.pattern.dictionary = lain::camera::board::Dictionary::Aruco5x5_100;
		board.pattern.squaresX = 7;
		board.pattern.squaresY = 5;
		board.pattern.markerToSquare = 0.75;
		board.instance.identity = "board";
		board.instance.squareLength.value = Length::from<Length::Millimetres>(80.0);
		board.pixelsPerSquare = 120;
		// One key a frame, so no frame is interpolated.
		board.path.keys = {{0, {-0.1415, -1.1717, 0.7161}, {-7.5178, 2.9151, -1.2083}},
						   {1, {-0.0333, -1.1732, 0.7065}, {-18.89, -11.391, -2.9303}},
						   {2, {0.0494, -1.1616, 0.7795}, {-19.5093, -22.6298, -3.1123}},
						   {3, {0.0942, -1.1435, 0.8925}, {-7.947, -16.9483, -1.2616}},
						   {4, {0.0942, -1.1297, 0.9792}, {7.6011, -1.036, 1.207}},
						   {5, {0.0494, -1.1281, 0.9888}, {18.0374, 5.4308, 2.8855}},
						   {6, {-0.0333, -1.1398, 0.9158}, {18.6032, -5.8093, 2.8873}},
						   {7, {-0.1415, -1.1578, 0.8029}, {8.0401, -20.8993, 1.2918}},
						   {8, {-0.2585, -1.1717, 0.7161}, {-8.0401, -20.8993, -1.2918}},
						   {9, {-0.3667, -1.1732, 0.7065}, {-18.6032, -5.8093, -2.8873}},
						   {10, {-0.4494, -1.1616, 0.7795}, {-18.0374, 5.4308, -2.8855}},
						   {11, {-0.4942, -1.1435, 0.8925}, {-7.6011, -1.036, -1.207}},
						   {12, {-0.4942, -1.1297, 0.9792}, {7.947, -16.9483, 1.2616}},
						   {13, {-0.4494, -1.1281, 0.9888}, {19.5093, -22.6298, 3.1123}},
						   {14, {-0.3667, -1.1398, 0.9158}, {18.89, -11.391, 2.9303}},
						   {15, {-0.2585, -1.1578, 0.8029}, {7.5178, 2.9151, 1.2083}}};
		p.boards.push_back(board);

		p.timeline.frames = 16;
		p.timeline.rate = {30, 1};
		p.seed = 1;
		p.scales.intrinsic = 1.0;
		p.scales.installation = 1.0;
		p.scales.locked = true;
		p.scales.noise = 1.0;
		return p;
	}

	const std::vector<Example>& catalog()
	{
		static const std::vector<Example> examples{
			{"three-poles", "six cameras on three poles round a working point, and M9's board turning through it", &threePoles, true},
			{"two-methods", "four fixed cameras round a textured room and a board moving through it, registered both ways", &twoMethods, true},
		};
		return examples;
	}

	void writeDocuments(const std::filesystem::path& folder)
	{
		for (const Example& example : catalog())
		{
			const rp::RigPlan plan = example.build();
			const std::vector<rp::PlanDiagnostic> problems = rp::check(plan);
			if (!problems.empty())
				throw std::runtime_error("rigplan-examples: " + example.name + ": " + problems.front().detail);
			lain::data::Value document = rp::rigPlanToValue(plan);
			rp::relativizePaths(document, folder);
			const std::filesystem::path file = folder / (example.name + ".json");
			if (!lain::io::data::save(lain::core::Uri::fromPath(file), document))
				throw std::runtime_error("rigplan-examples: " + example.name + ": could not write " + file.string());
		}
	}
} // namespace rigplan::examples
