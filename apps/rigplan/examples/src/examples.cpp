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

	const std::vector<Example>& catalog()
	{
		static const std::vector<Example> examples{
			{"three-poles", "six cameras on three poles round a working point, and M9's board turning through it", &threePoles, true},
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
