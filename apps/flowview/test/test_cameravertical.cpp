// Camera calibration end to end through the headless `run` path (runGraph), as a user's build runs it:
// a document naming the camera kinds, a folder of footage bound to its boundary input, and the report
// and model written out as text.
//
// The footage is a board rendered by the production backend and seen by a known pinhole camera along
// a sweep of poses, drawn here in plain C++ (an inverse homography, supersampled) so this test needs
// no OpenCV of its own. What is checked is that the pieces connect: the document saves and loads, the
// sequence binds, the method runs through the real backends and says Ready, the model it found is the
// camera the footage came from, and the model file it wrote binds back into a second run that holds
// it. Accuracy is the plugin's tests' business.
//
// In a build without a camera backend the same document is REFUSED: its camera kinds are not
// registered there (ADR-0016), so it does not load as saved.

#include "clibinders.h"
#include "graphio.h"
#include "runmode.h"
#include "scene.h"

#include <lain/camera/backends.h>
#include <lain/camera/board/detection.h>
#include <lain/camera/board/rendering.h>
#include <lain/camera/calibration/estimator.h>
#include <lain/camera/calibration/report.h>
#include <lain/camera/cameramodel.h>
#include <lain/camera/flow/boardspecificationnode.h>
#include <lain/camera/flow/calibratecameranode.h>
#include <lain/camera/flow/cameramodelnode.h>
#include <lain/camera/flow/detectboardnode.h>
#include <lain/camera/flow/register.h>
#include <lain/camera/flow/renderboardnode.h>
#include <lain/camera/serialize/cameramodel.h>
#include <lain/core/uri.h>
#include <lain/flow/boundary.h>
#include <lain/flow/graph.h>
#include <lain/io/data/load.h>
#include <lain/io/image/codecs.h>
#include <lain/io/image/save.h>
#include <lain/io/sequence/openers.h>
#include <lain/io/video/codecs.h>
#include <lain/math/rigidtransform.h>
#include <lain/media/framesequence.h>
#include <lain/testing/scratch.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <variant>

using namespace lain;
namespace fs = std::filesystem;

namespace
{
	void ensureRegistered()
	{
		static const bool once = []
		{
			io::image::registerImageCodecs();
			io::video::registerVideoCodecs();
			io::sequence::registerSequenceOpeners();
			camera::registerCameraBackends();
			flowview::registerSceneSerialization();
			return true;
		}();
		(void)once;
	}

	bool haveCameraBackend()
	{
		return camera::board::canRender() && camera::board::canDetect() && camera::calibration::canEstimate();
	}

	// The camera kinds registered whatever this build's backends, so the document can be WRITTEN in a
	// build that cannot run it: it is what a build with a backend would have saved.
	core::Factory<flow::Node> writingFactory()
	{
		core::Factory<flow::Node> factory;
		flowview::registerExampleNodes(factory, 8);
		factory.registerType<camera::BoardSpecificationNode>(camera::kBoardSpecificationKey);
		factory.registerType<camera::RenderBoardNode>(camera::kRenderBoardKey);
		factory.registerType<camera::DetectBoardNode>(camera::kDetectBoardKey);
		factory.registerType<camera::CalibrateCameraNode>(camera::kCalibrateCameraKey);
		factory.registerType<camera::CameraModelNode>(camera::kCameraModelKey);
		return factory;
	}

	template <typename T>
	void setParam(flow::Graph& graph, flow::NodeId id, const std::string& name, T value)
	{
		flow::Node& node = graph.node(id);
		for (std::size_t i = 0; i < node.paramCount(); ++i)
		{
			if (node.param(i).name() == name)
			{
				REQUIRE(node.setParam(node.param(i).id(), value));
				return;
			}
		}
		FAIL("no parameter named " << name);
	}

	flow::PortAddress in(const flow::Graph& graph, flow::NodeId id, std::size_t index)
	{
		return {id, graph.node(id).input(index).id()};
	}

	flow::PortAddress out(const flow::Graph& graph, flow::NodeId id, std::size_t index = 0)
	{
		return {id, graph.node(id).output(index).id()};
	}

	// footage -> calibrateCamera (board from a boardSpecification of 24 mm squares) -> report, and
	// -> cameraModel -> model. `held` adds an `imported` model input and holds it (HoldAndValidate):
	// the document that checks a model file against footage instead of estimating one.
	fs::path writeDocument(const fs::path& file, bool held = false)
	{
		const core::Factory<flow::Node> factory = writingFactory();
		flow::Graph graph;
		flow::GroupInputNode& input = graph.boundaryInputNode();
		flow::GroupOutputNode& output = graph.boundaryOutputNode();
		const flow::PortId footage = input.addBoundary<media::FrameSequence>("footage");
		const flow::PortId report = output.addBoundary<camera::calibration::Report>("report");
		const flow::PortId model = output.addBoundary<camera::CameraModel>("model");

		const flow::NodeId spec = graph.add(factory.create(camera::kBoardSpecificationKey));
		const flow::NodeId calibrate = graph.add(factory.create(camera::kCalibrateCameraKey));
		const flow::NodeId extract = graph.add(factory.create(camera::kCameraModelKey));
		setParam(graph, spec, "squareLengthMm", 24.0f);
		setParam(graph, calibrate, "resamples", 8);
		// The footage's camera is a pinhole, so that is the model to ask for: asked to fit distortion
		// coefficients to a lens with none, the extra freedom trades against the principal point and
		// the verdict rightly withholds Ready. It is also a param away from its default, so the
		// document has to carry an enum by name for the run to ask for it.
		setParam(graph, calibrate, "model", camera::DistortionModel::None);
		if (held)
		{
			const flow::PortId imported = input.addBoundary<camera::CameraModel>("imported");
			REQUIRE(graph.connect(flow::PortAddress{input.id(), imported}, in(graph, calibrate, 2)) ==
					flow::Connection::Ok);
			setParam(graph, calibrate, "importedPolicy", camera::calibration::ImportedModelPolicy::HoldAndValidate);
		}

		REQUIRE(graph.connect(flow::PortAddress{input.id(), footage}, in(graph, calibrate, 0)) == flow::Connection::Ok);
		REQUIRE(graph.connect(out(graph, spec), in(graph, calibrate, 1)) == flow::Connection::Ok);
		REQUIRE(graph.connect(out(graph, calibrate), in(graph, extract, 0)) == flow::Connection::Ok);
		REQUIRE(graph.connect(out(graph, calibrate), flow::PortAddress{output.id(), report}) == flow::Connection::Ok);
		REQUIRE(graph.connect(out(graph, extract), flow::PortAddress{output.id(), model}) == flow::Connection::Ok);
		REQUIRE(flowview::saveGraph(core::Uri::fromPath(file), graph, factory));
		return file;
	}

	// The camera the footage is seen through: a 640x480 pinhole with distinct intrinsics.
	constexpr double kFx = 600.0, kFy = 602.0, kCx = 321.4, kCy = 238.6;
	constexpr int kWidth = 640, kHeight = 480;

	// The board at pose `i` of `count`: a sweep across the image that tilts it every way (the
	// plugin's synthetic sweep).
	math::RigidTransformd sweepPose(const camera::board::Specification& spec, std::size_t i, std::size_t count)
	{
		const double t = double(i) / double(count);
		const double tau = 6.283185307179586;
		const camera::board::PatternParameters& p = spec.pattern().parameters();
		const double square = spec.instance().squareLength.value.metres();
		const math::Vec3d centre{p.squaresX * square / 2, p.squaresY * square / 2, 0.0};
		const math::Quatd turn = math::angleAxis(0.5 * std::sin(tau * t), math::Vec3d{0, 1, 0}) *
								 math::angleAxis(0.4 * std::cos(2 * tau * t), math::Vec3d{1, 0, 0});
		const math::Vec3d offset{0.13 * std::cos(3 * tau * t), 0.09 * std::sin(3 * tau * t), 0.42};
		return math::RigidTransformd{turn, offset - turn * centre};
	}

	// The raster at (x, y), bilinearly, on mid grey beyond its edges.
	double sample(const image::Image& raster, double x, double y)
	{
		const auto at = [&raster](long px, long py) -> double
		{
			if (px < 0 || py < 0 || px >= raster.width() || py >= raster.height())
				return 128;
			return raster.data()[std::size_t(py) * std::size_t(raster.width()) + std::size_t(px)];
		};
		const long x0 = long(std::floor(x)), y0 = long(std::floor(y));
		const double fx = x - double(x0), fy = y - double(y0);
		return (at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx) * (1 - fy) +
			   (at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx) * fy;
	}

	// What the camera sees of the rendered board at `pose`, on mid grey: each pixel the mean of 3x3
	// rays, each ray met with the board plane and sampled from the raster. Supersampled, and sampled
	// bilinearly, because aliased markers bias every corner: nearest sampling put fx 0.33% out.
	image::Image view(const camera::board::Rendering& rendering, const camera::board::Specification& spec,
					  const math::RigidTransformd& pose)
	{
		const image::Image& raster = rendering.raster;
		// Board metres -> raster pixel. The board's top-left corner is raster coordinate margin - 0.5,
		// since pixel (0, 0) is the centre of the first pixel.
		const double perMetre = rendering.request.pixelsPerSquare / spec.instance().squareLength.value.metres();
		const double origin = double(rendering.request.marginPixels) - 0.5;

		// A ray d (camera frame) meets the plane where b = R^T (lambda d - t) has b.z = 0.
		const math::Mat3d r = math::mat3_cast(pose.rotation());
		const math::Vec3d t = pose.translation();
		const math::Vec3d c{math::dot(r[0], t), math::dot(r[1], t), math::dot(r[2], t)}; // R^T t

		constexpr int s = 3;
		image::Image frame{kWidth, kHeight, image::PixelFormat::Gray8, image::ColorSpace::sRGB};
		for (int v = 0; v < kHeight; ++v)
		{
			for (int u = 0; u < kWidth; ++u)
			{
				double sum = 0;
				for (int j = 0; j < s; ++j)
				{
					for (int i = 0; i < s; ++i)
					{
						const math::Vec3d d{(u + (i + 0.5) / s - 0.5 - kCx) / kFx, (v + (j + 0.5) / s - 0.5 - kCy) / kFy, 1.0};
						const math::Vec3d q{math::dot(r[0], d), math::dot(r[1], d), math::dot(r[2], d)}; // R^T d
						double value = 128;
						if (q.z != 0 && c.z / q.z > 0)
						{
							const double lambda = c.z / q.z;
							value = sample(raster, (lambda * q.x - c.x) * perMetre + origin,
										   (lambda * q.y - c.y) * perMetre + origin);
						}
						sum += value;
					}
				}
				frame.data()[std::size_t(v) * kWidth + std::size_t(u)] = std::uint8_t(std::lround(sum / (s * s)));
			}
		}
		return frame;
	}

	// The footage, as a folder of stills: what a user would bind.
	fs::path writeFootage(const fs::path& dir, std::size_t frames)
	{
		camera::board::PatternParameters parameters;
		parameters.squaresX = 7;
		parameters.squaresY = 5;
		parameters.markerToSquare = 0.75;
		camera::board::Instance instance{"board", {core::Length::fromMillimetres(24), std::nullopt, std::nullopt}};
		const camera::board::Specification spec =
			*camera::board::Specification::create(*camera::board::Pattern::create(parameters).pattern, instance)
				 .specification;
		const std::optional<camera::board::Rendering> rendering =
			camera::board::render(spec.pattern(), camera::board::RenderRequest{60, 20});
		REQUIRE(rendering.has_value());

		const fs::path folder = dir / "footage";
		fs::create_directories(folder);
		for (std::size_t i = 0; i < frames; ++i)
		{
			const std::string name = "frame." + std::string(4 - std::to_string(i).size(), '0') + std::to_string(i) + ".png";
			REQUIRE(io::image::save(core::Uri::fromPath(folder / name), view(*rendering, spec, sweepPose(spec, i, frames))));
		}
		return folder;
	}

	std::string readText(const fs::path& file)
	{
		std::ifstream stream(file, std::ios::binary);
		return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
	}

	// A camera-model document on disk, read back through the production reader.
	std::optional<camera::CameraModel> readModel(const fs::path& file)
	{
		const std::optional<data::Value> document = io::data::load(core::Uri::fromPath(file));
		if (!document)
			return std::nullopt;
		return camera::cameraModelFromValue(*document).model;
	}
} // namespace

TEST_CASE("run calibrates a camera from a folder of footage", "[camera][runmode]")
{
	ensureRegistered();
	const fs::path dir = lain::testing::scratchDir() / "camera-vertical";
	fs::remove_all(dir);
	fs::create_directories(dir);
	const fs::path document = writeDocument(dir / "calibrate.json");

	core::Factory<flow::Node> factory;
	flowview::registerExampleNodes(factory, 8);
	flowview::BoundaryBinders binders;
	flowview::registerBoundaryBinders(binders);

	flowview::RunOptions options;
	options.graphPath = document.string();

	if (!haveCameraBackend())
	{
		// No backend, so no camera kinds: the document does not load as saved, and running what is
		// left of it would run a different graph.
		CHECK(flowview::runGraph(options, factory, binders) != 0);
		return;
	}

	const fs::path footage = writeFootage(dir, 24);
	const fs::path reportText = dir / "report.txt";
	const fs::path modelFile = dir / "model.json";
	options.bindings = {"--footage", footage.string(), "--report", reportText.string(), "--model", modelFile.string()};
	REQUIRE(flowview::runGraph(options, factory, binders) == 0);

	const std::string report = readText(reportText);
	INFO("report: " << report);
	CHECK(report.rfind("Ready: ", 0) == 0);

	// The model output is a camera-model DOCUMENT, read back by the production reader.
	const std::optional<camera::CameraModel> model = readModel(modelFile);
	REQUIRE(model.has_value());
	INFO("model: " << model->toString());
	CHECK(std::holds_alternative<camera::NoDistortion>(model->distortion()));
	CHECK(model->image() == camera::ImageGeometry{kWidth, kHeight});
	// Measured 0.14% and 0.3 px off; the bounds leave room for another platform's arithmetic, and
	// still catch a model that is not this camera's.
	CHECK(std::abs(model->intrinsics().fx - kFx) < 0.005 * kFx);
	CHECK(std::abs(model->intrinsics().fy - kFy) < 0.005 * kFy);
	CHECK(std::abs(model->intrinsics().cx - kCx) < 1.0);
	CHECK(std::abs(model->intrinsics().cy - kCy) < 1.0);

	SECTION("the model file binds back, and a held model is validated rather than re-estimated")
	{
		// The round trip a user makes: calibrate once, keep model.json, then check it against other
		// footage. `--imported` reads the file the first run wrote.
		flowview::RunOptions held;
		held.graphPath = writeDocument(dir / "validate.json", true).string();
		const fs::path heldReport = dir / "held.txt";
		const fs::path heldModel = dir / "held.json";
		held.bindings = {"--footage", footage.string(), "--imported", modelFile.string(),
						 "--report", heldReport.string(), "--model", heldModel.string()};
		REQUIRE(flowview::runGraph(held, factory, binders) == 0);

		// A held model can demonstrate no stability of its own, so it is at most Exploratory.
		const std::string text = readText(heldReport);
		INFO("held report: " << text);
		CHECK(text.rfind("Exploratory: ", 0) == 0);
		// ... and it comes out exactly as it went in: held, never estimated.
		const std::optional<camera::CameraModel> out = readModel(heldModel);
		REQUIRE(out.has_value());
		CHECK(camera::cameraModelToValue(*out) == camera::cameraModelToValue(*model));
	}
}
