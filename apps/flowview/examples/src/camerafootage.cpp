#include "camerafootage.h"

#include "testcameraparameters.h" // the textured rig's camera, the tests' own
#include "texturedscene.h"		  // the scene with no board in it

#include <lain/camera/board/pattern.h>
#include <lain/camera/board/rendering.h>
#include <lain/camera/serialize/cameramodel.h>
#include <lain/camera/serialize/registeredrig.h>
#include <lain/core/length.h>
#include <lain/core/uri.h>
#include <lain/image/image.h>
#include <lain/io/data/save.h>
#include <lain/io/image/save.h>
#include <lain/task/task.h>

#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace flowview::examples::footage
{
	using namespace lain;
	namespace fs = std::filesystem;

	[[noreturn]] static void fail(const std::string& what)
	{
		throw std::runtime_error("camera footage: " + what);
	}

	// frame.0000.png, frame.0001.png, ...: what a still sequence binds by.
	static std::string stillName(std::size_t i)
	{
		const std::string n = std::to_string(i);
		return "frame." + std::string(n.size() < 4 ? 4 - n.size() : 0, '0') + n + ".png";
	}

	static void save(const fs::path& file, const image::Image& image)
	{
		if (!io::image::save(core::Uri::fromPath(file), image))
			fail("could not write " + file.string());
	}

	static void saveModel(const fs::path& file, const camera::CameraModel& model)
	{
		if (!io::data::save(core::Uri::fromPath(file), camera::cameraModelToValue(model)))
			fail("could not write " + file.string());
	}

	static camera::board::Rendering renderBoard()
	{
		const std::optional<camera::board::Rendering> rendering =
			camera::board::render(board().pattern(), camera::board::RenderRequest{60, 20}).rendering;
		if (!rendering)
			fail("this build has no board renderer");
		return *rendering;
	}

	const camera::synthetic::Pinhole& boardCamera()
	{
		static const camera::synthetic::Pinhole pinhole{};
		return pinhole;
	}

	static camera::CameraModel modelOf(const camera::synthetic::Pinhole& c)
	{
		camera::CameraModelParameters p;
		p.image = {std::uint32_t(c.width), std::uint32_t(c.height)};
		p.intrinsics = {c.fx, c.fy, c.cx, c.cy};
		p.distortion = camera::NoDistortion{};
		return *camera::CameraModel::create(p).model;
	}

	camera::CameraModel boardCameraModel()
	{
		return modelOf(boardCamera());
	}

	// The board rig's cameras: cam0 is boardCamera, and the others a tenth longer and shorter, their
	// principal points moved, so no two share a model.
	static camera::synthetic::Pinhole boardRigPinhole(std::size_t camera)
	{
		camera::synthetic::Pinhole p = boardCamera();
		if (camera == 1)
		{
			p.fx *= 1.1;
			p.fy *= 1.1;
			p.cx -= 3.4;
			p.cy += 2.9;
		}
		else if (camera == 2)
		{
			p.fx *= 0.9;
			p.fy *= 0.9;
			p.cx += 3.4;
			p.cy -= 2.4;
		}
		return p;
	}

	camera::CameraModel boardRigModel(std::size_t camera)
	{
		return modelOf(boardRigPinhole(camera));
	}

	camera::board::Specification board()
	{
		camera::board::PatternParameters parameters;
		parameters.squaresX = 7;
		parameters.squaresY = 5;
		parameters.markerToSquare = 0.75;
		const camera::board::Instance instance{"board", {core::Length::from<core::Length::Millimetres>(24), std::nullopt, std::nullopt}};
		return *camera::board::Specification::create(*camera::board::Pattern::create(parameters).pattern, instance).specification;
	}

	fs::path writeBoardFootage(const fs::path& dir, std::size_t frames, bool rendered)
	{
		const camera::board::Specification spec = board();
		const std::optional<camera::board::Rendering> rendering = rendered ? std::optional(renderBoard()) : std::nullopt;
		const camera::synthetic::Pinhole& pinhole = boardCamera();
		const fs::path folder = dir / "footage";
		fs::create_directories(folder);
		for (std::size_t i = 0; i < frames; ++i)
			save(folder / stillName(i), rendering ? camera::synthetic::view(*rendering, spec, pinhole,
																			camera::synthetic::sweepPose(spec, i, frames))
												  : image::Image(pinhole.width, pinhole.height));
		return folder;
	}

	fs::path writeMovingFootage(const fs::path& dir, std::size_t frames)
	{
		// Seed 2 and one grey level of noise, as the end-to-end calibration suite draws the same path.
		camera::synthetic::Scene scene = camera::synthetic::defaultScene(2);
		scene.noise = 1.0;
		const camera::CameraModel model = boardCameraModel();
		const std::vector<math::RigidTransformd> poses = camera::synthetic::arcPath(frames);
		std::vector<image::Image> drawn(frames);
		task::range(std::ptrdiff_t{0}, std::ptrdiff_t(frames),
					[&](std::ptrdiff_t i)
					{ drawn[std::size_t(i)] = camera::synthetic::render(scene, model, poses[std::size_t(i)], std::size_t(i), 2, 2); });

		const fs::path folder = dir / "footage";
		fs::create_directories(folder);
		for (std::size_t i = 0; i < frames; ++i)
			save(folder / stillName(i), drawn[i]);
		return folder;
	}

	math::RigidTransformd boardRigCamera(std::size_t camera)
	{
		// Turned about the board's mean position, 0.42 m ahead of cam0, about the vertical.
		static const double angles[] = {0.0, -0.25, 0.25};
		const math::Vec3d pivot{0, 0, 0.42};
		const math::Quatd turn = math::angleAxis(angles[camera], math::Vec3d{0, 1, 0});
		return math::RigidTransformd{turn, pivot - turn * pivot};
	}

	void writeBoardRig(const fs::path& dir, std::size_t frames, bool rendered)
	{
		const camera::board::Specification spec = board();
		const std::optional<camera::board::Rendering> rendering = rendered ? std::optional(renderBoard()) : std::nullopt;
		fs::create_directories(dir / "models");
		for (std::size_t c = 0; c < 3; ++c)
		{
			const std::string name = "cam" + std::to_string(c);
			const fs::path folder = dir / "rig" / name;
			fs::create_directories(folder);
			const camera::synthetic::Pinhole pinhole = boardRigPinhole(c);
			const math::RigidTransformd cameraFromReference = boardRigCamera(c).inverse();
			for (std::size_t i = 0; i < frames; ++i)
				save(folder / stillName(i),
					 rendering ? camera::synthetic::view(*rendering, spec, pinhole,
														 cameraFromReference * camera::synthetic::sweepPose(spec, i, frames))
							   : image::Image(pinhole.width, pinhole.height));
			saveModel(dir / "models" / (name + ".json"), boardRigModel(c));
		}
	}

	math::RigidTransformd texturedRigCamera(std::size_t camera)
	{
		static const double angles[] = {-0.25, 0.0, 0.25};
		return camera::synthetic::arcCamera(angles[camera]);
	}

	void writeTexturedRig(const fs::path& dir, bool rendered)
	{
		const camera::CameraModel model =
			*camera::CameraModel::create(camera::testing::parametersWith(camera::testing::brownConrady())).model;
		const camera::synthetic::Scene scene = camera::synthetic::defaultScene();
		fs::create_directories(dir / "models");
		for (std::size_t c = 0; c < 3; ++c)
		{
			const std::string name = "cam" + std::to_string(c);
			const fs::path folder = dir / "rig" / name;
			fs::create_directories(folder);
			// A still scene draws the same frame every time, so each camera's is drawn once.
			const image::Image frame = rendered ? camera::synthetic::render(scene, model, texturedRigCamera(c))
												: image::Image(int(model.image().width), int(model.image().height));
			for (std::size_t i = 0; i < 3; ++i)
				save(folder / stillName(i), frame);
			saveModel(dir / "models" / (name + ".json"), model);
		}
	}

	// The rig three cameras were drawn from, at `worldFromCamera`, as the rig document a registration
	// of them is scored against (compare-rigs.json): relative to cam0, the identity exactly, metric.
	static void saveTruthRig(const fs::path& file, const std::vector<math::RigidTransformd>& worldFromCamera)
	{
		const math::RigidTransformd cam0FromWorld = worldFromCamera.front().inverse();
		camera::registration::RegisteredRig rig;
		rig.reference = camera::capture::CameraIdentity{"cam0"};
		rig.scale = {camera::registration::Scale::Metric, "the poses the footage was drawn from"};
		for (std::size_t c = 0; c < worldFromCamera.size(); ++c)
		{
			rig.cameras.push_back({camera::capture::CameraIdentity{"cam" + std::to_string(c)},
								   c == 0 ? math::RigidTransformd{} : cam0FromWorld * worldFromCamera[c]});
		}
		if (!io::data::save(core::Uri::fromPath(file), camera::registration::registeredRigToValue(rig)))
			fail("could not write " + file.string());
	}

	void writeAll(const fs::path& folder)
	{
		writeBoardFootage(folder / "board", 24);
		writeMovingFootage(folder / "moving", 40);
		writeBoardRig(folder / "board-rig", 24, true);
		writeTexturedRig(folder / "textured-rig", true);

		// What each was drawn with, for the comparison examples to score a result against: the board
		// camera's model, and each rig's poses.
		saveModel(folder / "board" / "truth.json", boardCameraModel());
		saveTruthRig(folder / "board-rig" / "truth.json", {boardRigCamera(0), boardRigCamera(1), boardRigCamera(2)});
		saveTruthRig(folder / "textured-rig" / "truth.json",
					 {texturedRigCamera(0), texturedRigCamera(1), texturedRigCamera(2)});

		std::ofstream readme(folder / "README.txt");
		readme << "Camera footage for apps/flowview/examples, drawn by flowview-examples --camera-footage.\n"
				  "Run from this folder, with EX standing for the examples folder; or open the example in\n"
				  "flowview and bind the same folders in the Interface pane.\n\n"
				  "board/footage         one camera, a 7 x 5 board of 24 mm squares, 24 frames; board/truth.json its model\n"
				  "  flowview run -g EX/calibrate.json --footage board/footage --report report.txt --model model.json\n"
				  "  flowview run -g EX/compare-models.json --expected board/truth.json --found model.json\n"
				  "      --comparison comparison.txt\n"
				  "  flowview run -g EX/calibrate-fallback.json --footage board/footage --method board-then-targetless\n"
				  "      --attempts attempts.txt --model model.json\n\n"
				  "moving/footage        one camera moving round a textured scene, no board, 40 frames\n"
				  "  flowview run -g EX/calibrate-fallback.json --footage moving/footage --method board-then-targetless\n"
				  "      --attempts attempts.txt --model model.json\n\n"
				  "board-rig/            three cameras watching one board, 24 frames each, their models, and truth.json\n"
				  "                      the rig they were drawn from\n"
				  "  flowview run -g EX/register.json --footage board-rig/rig --models board-rig/models\n"
				  "      --report report.txt --rig rig.json\n"
				  "  flowview run -g EX/calibrate-and-register.json --footage board-rig/rig --report report.txt --rig rig.json\n"
				  "  flowview run -g EX/compare-rigs.json --expected board-rig/truth.json --found rig.json\n"
				  "      --comparison comparison.txt\n\n"
				  "textured-rig/         three fixed cameras round a textured scene, their models, and truth.json\n"
				  "  flowview run -g EX/register-targetless.json --footage textured-rig/rig --models textured-rig/models\n"
				  "      --report report.txt --rig rig.json\n"
				  "  flowview run -g EX/compare-rigs.json --expected textured-rig/truth.json --found rig.json\n"
				  "      --comparison comparison.txt\n";
		if (!readme)
			fail("could not write " + (folder / "README.txt").string());
	}
} // namespace flowview::examples::footage
