// The real-camera fixture harness, on fixtures written to scratch in the real on-disk format: a
// fixture.json and a folder of stills per session, read back through loadFixture exactly as a
// committed capture is. The footage is synthetic (a known pinhole, drawn in plain C++), so what the
// harness should conclude is known in advance: one camera seen twice passes, two cameras do not.
//
// The calibrating cases need a backend and SKIP without one; loading and the extended tier do not.

#include "fixture/fixture.h"
#include "fixture/manifest.h"
#include "registration.h"
#include "syntheticfootage.h"

#include <lain/camera/board/detection.h>
#include <lain/camera/board/rendering.h>
#include <lain/camera/calibration/estimator.h>
#include <lain/core/length.h>
#include <lain/core/sha256.h>
#include <lain/core/uri.h>
#include <lain/image/image.h>
#include <lain/io/image/save.h>
#include <lain/testing/scratch.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <variant>
#include <vector>

using namespace lain;
using namespace lain::camera;
using namespace lain::camera::fixture;
namespace fs = std::filesystem;

namespace
{
	bool haveBackend()
	{
		return board::canRender() && board::canDetect() && calibration::canEstimate();
	}

	board::SpecificationParameters boardParameters()
	{
		board::SpecificationParameters p;
		p.pattern.squaresX = 7;
		p.pattern.squaresY = 5;
		p.pattern.markerToSquare = 0.75;
		p.instance.identity = "synthetic";
		p.instance.squareLength.value = core::Length::fromMillimetres(24);
		return p;
	}

	CaptureRecord captureOf(int width, int height)
	{
		CaptureRecord record;
		record.device = {"lain", "synthetic pinhole", "test"};
		record.stream = {std::uint32_t(width), std::uint32_t(height), "GRAY8", 30.0};
		record.properties = {{"generator", "syntheticfootage.h"}};
		return record;
	}

	std::string frameName(std::size_t i)
	{
		const std::string digits = std::to_string(i);
		return "frame." + std::string(4 - digits.size(), '0') + digits + ".png";
	}

	// One capture session to write: which camera, which part of the sweep, and what its record says.
	struct SessionPlan
	{
		std::string name;
		std::size_t frames = 0;
		double phase = 0;
		synthetic::Pinhole camera;
		std::optional<ImportedModel> imported;
	};

	// A fixture folder in `root`: the board rendered by the production backend, each session's
	// footage seen through its camera, and fixture.json describing it all. The model asked for is
	// None, since every synthetic camera here is a pinhole.
	FixtureDocument writeFixture(const fs::path& root, const std::vector<SessionPlan>& plans)
	{
		FixtureDocument document;
		document.name = "synthetic";
		document.model = DistortionModel::None;
		document.board = boardParameters();

		const board::SpecificationRead spec = board::specificationFrom(document.board);
		REQUIRE(spec.specification.has_value());
		const std::optional<board::Rendering> rendering =
			board::render(spec.specification->pattern(), board::RenderRequest{60, 20}).rendering;
		REQUIRE(rendering.has_value());

		for (const SessionPlan& plan : plans)
		{
			const fs::path folder = root / plan.name;
			fs::create_directories(folder);
			for (std::size_t i = 0; i < plan.frames; ++i)
			{
				const math::RigidTransformd pose = synthetic::sweepPose(*spec.specification, i, plan.frames, plan.phase);
				REQUIRE(io::image::save(core::Uri::fromPath(folder / frameName(i)),
										synthetic::view(*rendering, *spec.specification, plan.camera, pose)));
			}
			SessionDocument session{plan.name, plan.name, captureOf(plan.camera.width, plan.camera.height)};
			session.capture.importedModel = plan.imported;
			document.sessions.push_back(std::move(session));
		}
		REQUIRE(writeFixtureDocument(root, document));
		return document;
	}

	fs::path freshDir(const std::string& name)
	{
		const fs::path dir = lain::testing::scratchDir() / name;
		fs::remove_all(dir);
		fs::create_directories(dir);
		return dir;
	}

	calibration::Request quickRequest()
	{
		calibration::Request request;
		request.resamples = 8; // enough for a stability verdict, few enough for a test
		return request;
	}

	ImportedModel pinholeModel(const synthetic::Pinhole& camera, double focalScale, const std::string& source)
	{
		ImportedModel model;
		model.source = source;
		model.parameters.image = {std::uint32_t(camera.width), std::uint32_t(camera.height)};
		model.parameters.intrinsics = {camera.fx * focalScale, camera.fy * focalScale, camera.cx, camera.cy};
		model.parameters.distortion = NoDistortion{};
		return model;
	}

	std::string readText(const fs::path& file)
	{
		std::ifstream stream(file, std::ios::binary);
		return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
	}

	void writeText(const fs::path& file, const std::string& text)
	{
		std::ofstream(file, std::ios::binary) << text;
	}

	bool anyContains(const std::vector<std::string>& lines, const std::string& part)
	{
		return std::any_of(lines.begin(), lines.end(),
						   [&part](const std::string& line)
						   { return line.find(part) != std::string::npos; });
	}
} // namespace

TEST_CASE("a fixture of one camera seen twice passes, and an imported model is compared, not gated",
		  "[camera][fixture]")
{
	ensureRegistered();
	if (!haveBackend())
		SKIP("this build has no camera backend");

	const fs::path root = freshDir("fixture-one-camera");
	const synthetic::Pinhole camera{};
	// Session a carries a manufacturer model 2% out in focal length: a real factory model is never
	// exactly what a calibration finds, and a fixture must not fail because of it. 2% is beyond any
	// bound agreement can have (Ready caps it near 2.1% once the estimate's own error is added), so
	// the comparison below would fail the fixture if it gated.
	writeFixture(root, {{"a", 20, 0.0, camera, pinholeModel(camera, 1.02, "a factory model, 2% long")},
						{"b", 14, 0.37, camera, std::nullopt}});

	const FixtureLoad load = loadFixture(root);
	INFO("load problems: " << load.problems.size());
	REQUIRE(load.problems.empty());
	REQUIRE(load.fixture.has_value());
	REQUIRE(load.fixture->sessions.size() == 2);

	const FixtureReport report = runFixture(*load.fixture, quickRequest());
	INFO(report.toString());
	CHECK(report.passed());

	// The repeat check ran both halves in every direction two Ready sessions allow, and each agreed.
	REQUIRE(report.crossValidations.size() == 2);
	for (const CrossValidation& check : report.crossValidations)
	{
		CHECK(check.agrees);
		CHECK(std::holds_alternative<calibration::HeldOutEvidence>(check.evidence));
	}
	REQUIRE(report.agreements.size() == 1);
	CHECK(report.agreements.front().agrees);

	// The factory model was compared against its own session's views and reported beside the estimate.
	REQUIRE(report.manufacturer.size() == 1);
	const ManufacturerComparison& comparison = report.manufacturer.front();
	CHECK(comparison.session == "a");
	CHECK(comparison.source == "a factory model, 2% long");
	CHECK(comparison.imported.fx == camera.fx * 1.02);
	REQUIRE(comparison.estimated.has_value());
	CHECK(std::abs(comparison.estimated->fx - camera.fx) < 0.005 * camera.fx);
	REQUIRE(std::holds_alternative<calibration::HeldOutEvidence>(comparison.evidence));
	// ... and it is further from the estimate than two sessions may be from each other, so had it
	// gated, the fixture would have failed.
	REQUIRE(comparison.difference.has_value());
	CHECK(comparison.difference->focal > report.agreements.front().focalBound);
}

// Two sessions, one camera each. Each may well be Ready on its own: each IS a calibrated camera.
static FixtureReport twoCameras(const std::string& name, const synthetic::Pinhole& other)
{
	const fs::path root = freshDir(name);
	writeFixture(root, {{"a", 20, 0.0, synthetic::Pinhole{}, std::nullopt}, {"b", 20, 0.37, other, std::nullopt}});
	const FixtureLoad load = loadFixture(root);
	REQUIRE(load.fixture.has_value());
	return runFixture(*load.fixture, quickRequest());
}

TEST_CASE("a second session from a very different camera fails both halves of the repeat check",
		  "[camera][fixture]")
{
	ensureRegistered();
	if (!haveBackend())
		SKIP("this build has no camera backend");

	synthetic::Pinhole other;
	other.fx *= 1.25;
	other.fy *= 1.25;
	other.cx += 40;
	other.cy -= 30;
	const FixtureReport report = twoCameras("fixture-very-different", other);
	INFO(report.toString());
	CHECK_FALSE(report.passed());
	CHECK(anyContains(report.failures, "does not predict"));
	CHECK(anyContains(report.failures, "do not agree on the camera"));
}

TEST_CASE("a second session from a slightly different camera fails on agreement, which prediction cannot see",
		  "[camera][fixture]")
{
	ensureRegistered();
	if (!haveBackend())
		SKIP("this build has no camera backend");

	// 3% longer and 12 px off-centre: a different camera, or the same one after a focus change. Each
	// session's poses absorb most of that when the other's model is held on them (measured 0.27 mrad,
	// against 0.6 for Ready), so it is the direct comparison of the estimates that fails.
	synthetic::Pinhole other;
	other.fx *= 1.03;
	other.fy *= 1.03;
	other.cx += 12;
	other.cy -= 10;
	const FixtureReport report = twoCameras("fixture-slightly-different", other);
	INFO(report.toString());
	CHECK_FALSE(report.passed());
	CHECK(anyContains(report.failures, "do not agree on the camera"));
	REQUIRE(report.agreements.size() == 1);
	CHECK_FALSE(report.agreements.front().agrees);
}

TEST_CASE("a fixture with one session fails for want of a repeat check", "[camera][fixture]")
{
	ensureRegistered();
	if (!haveBackend())
		SKIP("this build has no camera backend");

	const fs::path root = freshDir("fixture-one-session");
	writeFixture(root, {{"a", 20, 0.0, synthetic::Pinhole{}, std::nullopt}});
	const FixtureLoad load = loadFixture(root);
	REQUIRE(load.fixture.has_value());
	const FixtureReport report = runFixture(*load.fixture, quickRequest());
	INFO(report.toString());
	CHECK_FALSE(report.passed());
	CHECK(anyContains(report.failures, "two Ready sessions"));
	CHECK(report.crossValidations.empty());
}

TEST_CASE("a fixture's problems are named when it loads", "[camera][fixture]")
{
	ensureRegistered();
	const fs::path root = freshDir("fixture-load");

	// Blank frames are enough: loading opens the footage and never looks for a board.
	const auto writeFrames = [&root](const std::string& name, int width, int height)
	{
		fs::create_directories(root / name);
		image::Image blank{width, height, image::PixelFormat::Gray8, image::ColorSpace::sRGB};
		std::memset(blank.data(), 128, std::size_t(width) * std::size_t(height));
		for (std::size_t i = 0; i < 2; ++i)
			REQUIRE(io::image::save(core::Uri::fromPath(root / name / frameName(i)), blank));
	};
	writeFrames("a", 64, 48);

	FixtureDocument document;
	document.name = "loading";
	document.board = boardParameters();
	document.sessions.push_back({"a", "a", captureOf(64, 48)});

	SECTION("a well-formed fixture loads")
	{
		REQUIRE(writeFixtureDocument(root, document));
		const FixtureLoad load = loadFixture(root);
		CHECK(load.problems.empty());
		REQUIRE(load.fixture.has_value());
		CHECK(load.fixture->sessions.front().image == ImageGeometry{64, 48});
		CHECK(load.fixture->sessions.front().footage.size() == 2);
	}

	SECTION("the template a person fills in loads once it has frames")
	{
		// camera-fixture-tool template writes this; its sessions are "a" and "b".
		writeFrames("b", 64, 48);
		REQUIRE(writeFixtureDocument(root, templateDocument("from the template")));
		const FixtureLoad load = loadFixture(root);
		INFO(load.problems.size());
		CHECK(load.problems.empty());
		REQUIRE(load.fixture.has_value());
		CHECK(load.fixture->sessions.size() == 2);
		CHECK(load.fixture->model == DistortionModel::BrownConrady5);
		CHECK(load.fixture->board.pattern().parameters().squaresX == 9);
	}

	SECTION("a misspelled key is named, not defaulted")
	{
		REQUIRE(writeFixtureDocument(root, document));
		std::string text = readText(root / kFixtureFile);
		const std::size_t at = text.find("\"squaresX\"");
		REQUIRE(at != std::string::npos);
		text.replace(at, 10, "\"squareX\"");
		writeText(root / kFixtureFile, text);
		const FixtureLoad load = loadFixture(root);
		INFO(load.problems.size());
		CHECK_FALSE(load.fixture.has_value());
		CHECK(anyContains(load.problems, "squareX"));
	}

	SECTION("an unknown distortion model name is named")
	{
		REQUIRE(writeFixtureDocument(root, document));
		std::string text = readText(root / kFixtureFile);
		const std::size_t at = text.find("\"BrownConrady5\"");
		REQUIRE(at != std::string::npos);
		text.replace(at, 15, "\"BrownConrady6\"");
		writeText(root / kFixtureFile, text);
		const FixtureLoad load = loadFixture(root);
		CHECK_FALSE(load.fixture.has_value());
		CHECK(anyContains(load.problems, "BrownConrady6"));
	}

	SECTION("a missing frames folder is named")
	{
		document.sessions.push_back({"b", "b", captureOf(64, 48)});
		REQUIRE(writeFixtureDocument(root, document));
		const FixtureLoad load = loadFixture(root);
		CHECK_FALSE(load.fixture.has_value());
		CHECK(anyContains(load.problems, "session 'b': frames folder 'b' does not exist"));
	}

	SECTION("frames of another size than the recorded stream are named")
	{
		document.sessions.front().capture.stream.width = 1280;
		document.sessions.front().capture.stream.height = 800;
		REQUIRE(writeFixtureDocument(root, document));
		const FixtureLoad load = loadFixture(root);
		CHECK_FALSE(load.fixture.has_value());
		CHECK(anyContains(load.problems, "the frames are 64x48, and the capture record says the stream was 1280x800"));
	}

	SECTION("an imported model that is not a valid camera model is named")
	{
		ImportedModel imported;
		imported.source = "hand-typed";
		imported.parameters.image = {64, 48};
		imported.parameters.intrinsics = {-50, 50, 32, 24};
		document.sessions.front().capture.importedModel = imported;
		REQUIRE(writeFixtureDocument(root, document));
		const FixtureLoad load = loadFixture(root);
		CHECK_FALSE(load.fixture.has_value());
		CHECK(anyContains(load.problems, "session 'a': imported model:"));
	}

	SECTION("an imported model for another image size is named")
	{
		ImportedModel imported;
		imported.source = "another stream's";
		imported.parameters.image = {1280, 800};
		imported.parameters.intrinsics = {640, 640, 640, 400};
		document.sessions.front().capture.importedModel = imported;
		REQUIRE(writeFixtureDocument(root, document));
		const FixtureLoad load = loadFixture(root);
		CHECK_FALSE(load.fixture.has_value());
		CHECK(anyContains(load.problems, "the imported model is for 1280x800"));
	}

	SECTION("a board that is not a valid specification is named")
	{
		document.board.pattern.squaresX = 1;
		REQUIRE(writeFixtureDocument(root, document));
		const FixtureLoad load = loadFixture(root);
		CHECK_FALSE(load.fixture.has_value());
		CHECK(anyContains(load.problems, "board: "));
	}

	SECTION("a newer fixture version is refused")
	{
		document.version = kFixtureVersion + 1;
		REQUIRE(writeFixtureDocument(root, document));
		const FixtureLoad load = loadFixture(root);
		CHECK_FALSE(load.fixture.has_value());
		CHECK(anyContains(load.problems, "this build reads version"));
	}

	SECTION("a folder without fixture.json is named")
	{
		const FixtureLoad load = loadFixture(root / "a");
		CHECK_FALSE(load.fixture.has_value());
		CHECK(anyContains(load.problems, "there is no fixture.json"));
	}
}

TEST_CASE("the extended tier is verified, unavailable, or a mismatch, and never substituted",
		  "[camera][fixture][manifest]")
{
	ensureRegistered();
	const fs::path dataRoot = freshDir("fixture-data");
	const fs::path dataset = dataRoot / "capture-1";
	fs::create_directories(dataset / "a");
	writeText(dataset / kFixtureFile, "{}");
	writeText(dataset / "a" / "frame.0000.png", "first frame");
	writeText(dataset / "a" / "frame.0001.png", "second frame");
	writeText(dataset / ".DS_Store", "a file browser's"); // a dotfile is never part of a dataset

	const Manifest manifest = manifestOf(dataset, "capture-1");
	REQUIRE(manifest.files.size() == 3);
	CHECK(manifest.files[0].path == "a/frame.0000.png"); // '/'-separated, in order of path
	CHECK(manifest.files[1].path == "a/frame.0001.png");
	CHECK(manifest.files[2].path == "fixture.json");
	CHECK(manifest.files[0].bytes == 11);
	CHECK(manifest.files[0].sha256 == core::sha256("first frame").toString());

	// It is written and read back as the committed extended.json is.
	const fs::path file = dataRoot / kExtendedManifest;
	REQUIRE(writeManifest(file, manifest));
	const ManifestRead read = readManifest(file);
	REQUIRE(read.problems.empty());
	REQUIRE(read.manifest.has_value());
	CHECK(read.manifest->files.size() == 3);

	SECTION("the pinned data verifies")
	{
		const ExtendedTier tier = verifyExtended(*read.manifest, dataRoot);
		INFO(tier.reason);
		CHECK(tier.status == TierStatus::Verified);
		CHECK(tier.root == dataset);
	}

	SECTION("no data root, or a root without the dataset, is unavailable and says so")
	{
		const ExtendedTier unset = verifyExtended(*read.manifest, {});
		CHECK(unset.status == TierStatus::Unavailable);
		CHECK(unset.reason.find(kDataRootVariable) != std::string::npos);

		const ExtendedTier absent = verifyExtended(*read.manifest, dataRoot / "elsewhere");
		CHECK(absent.status == TierStatus::Unavailable);
		CHECK(absent.reason.find("capture-1") != std::string::npos);
	}

	SECTION("a changed byte is a mismatch naming the file")
	{
		writeText(dataset / "a" / "frame.0001.png", "second frbme"); // same size, other content
		const ExtendedTier tier = verifyExtended(*read.manifest, dataRoot);
		CHECK(tier.status == TierStatus::Mismatch);
		CHECK(tier.reason.find("'a/frame.0001.png' has another hash") != std::string::npos);
	}

	SECTION("a missing, resized or unpinned file is a mismatch naming each")
	{
		fs::remove(dataset / "a" / "frame.0000.png");
		writeText(dataset / "a" / "frame.0001.png", "a longer second frame");
		writeText(dataset / "a" / "frame.0002.png", "slipped in");
		const ExtendedTier tier = verifyExtended(*read.manifest, dataRoot);
		CHECK(tier.status == TierStatus::Mismatch);
		CHECK(tier.reason.find("'a/frame.0000.png' is missing") != std::string::npos);
		CHECK(tier.reason.find("'a/frame.0001.png' is 21 bytes, and 12 are pinned") != std::string::npos);
		CHECK(tier.reason.find("'a/frame.0002.png' is not in the manifest") != std::string::npos);
	}

	SECTION("a manifest with a misspelled key is refused")
	{
		std::string text = readText(file);
		const std::size_t at = text.find("\"sha256\"");
		REQUIRE(at != std::string::npos);
		text.replace(at, 8, "\"sha265\"");
		writeText(file, text);
		const ManifestRead typo = readManifest(file);
		CHECK_FALSE(typo.manifest.has_value());
		CHECK(anyContains(typo.problems, "sha265"));
	}
}
