// The cli's scalar binders — what `--<boundary> <text>` may mean, and what it must refuse.
//
// Compiles the production clibinders.cpp / graphio.cpp, so these are the real binders and the real
// Cast conversions the binary runs, not copies that could drift from them.

#include "clibinders.h"
#include "graphio.h"

#include <lain/camera/serialize/cameramodel.h>
#include <lain/core/uri.h>
#include <lain/flow/porttype.h>
#include <lain/flow/porttyperegistry.h>
#include <lain/flow/portvalue.h>
#include <lain/io/data/save.h>
#include <lain/media/frameposition.h>
#include <lain/testing/scratch.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <typeindex>
#include <typeinfo>

using namespace lain;

// --- file-local helpers (named static, not an anonymous namespace) ----------

static flowview::BoundaryBinders builtIn()
{
	flowview::BoundaryBinders binders;
	flowview::registerBoundaryBinders(binders);
	return binders;
}

// A string as an erased value — the shape convertValue takes.
static flow::PortValue text(const std::string& value)
{
	flow::PortValue pv;
	pv.set<std::string>(value);
	return pv;
}

TEST_CASE("an int binds only when the whole value is one", "[flowview][binder]")
{
	const flowview::BoundaryBinders binders = builtIn();

	CHECK(binders.bind(typeid(int), "12")->get<int>() == 12);
	CHECK(binders.bind(typeid(int), "-12")->get<int>() == -12);
	CHECK(binders.bind(typeid(int), "0")->get<int>() == 0);

	CHECK_FALSE(binders.bind(typeid(int), "12x").has_value());
	CHECK_FALSE(binders.bind(typeid(int), "12 ").has_value());
	CHECK_FALSE(binders.bind(typeid(int), "hello").has_value());
	CHECK_FALSE(binders.bind(typeid(int), "").has_value());

	// Out of range is refused rather than wrapped or clamped — the property core::parse exists for.
	CHECK_FALSE(binders.bind(typeid(int), "2147483648").has_value());
}

TEST_CASE("a negative frame position is refused, not wrapped", "[flowview][binder]")
{
	// THE REGRESSION. This binder was std::stoull, which does not refuse a negative — it NEGATES,
	// so "-12" bound position 18446744073709551604 and the sweep then asked a sequence for a frame
	// no footage has. A wrong answer that said nothing about being wrong, and nothing asked.
	const flowview::BoundaryBinders binders = builtIn();

	CHECK(binders.bind(typeid(media::FramePosition), "12")->get<media::FramePosition>().value == 12);
	CHECK(binders.bind(typeid(media::FramePosition), "0")->get<media::FramePosition>().value == 0);

	CHECK_FALSE(binders.bind(typeid(media::FramePosition), "-12").has_value());
	CHECK_FALSE(binders.bind(typeid(media::FramePosition), "-1").has_value());
	CHECK_FALSE(binders.bind(typeid(media::FramePosition), "12x").has_value());
	CHECK_FALSE(binders.bind(typeid(media::FramePosition), "").has_value());
}

TEST_CASE("a bound value and a Cast read text the same way", "[flowview][binder]")
{
	// One policy, two doors: the cli binder and the Cast node's string conversion are the same
	// routine, so they cannot drift into disagreeing about what "12x" is. Asserted as an AGREEMENT
	// rather than as two copies of one string list, since a list copied here is the drift it guards.
	flowview::registerSceneSerialization(); // the conversions, as the binary registers them
	const flowview::BoundaryBinders binders = builtIn();

	for (const std::string value : {"12", "-4", "0", "12x", "hello", "", " 12", "+12", "2147483648"})
	{
		const bool bound = binders.bind(typeid(int), value).has_value();
		const bool cast = !flow::convertValue(text(value), flow::portType<int>()).empty();
		INFO("text: '" << value << "'");
		CHECK(bound == cast);
	}
}

TEST_CASE("every scalar binder reads text the same way", "[flowview][binder]")
{
	// An int and a float go through two different MECHANISMS inside core::parse — from_chars for
	// one, strtof for the other, because libc++ marks floating-point from_chars as introduced in
	// macOS 26 and so it is unreachable for any ordinary deployment target. The mechanisms are the
	// implementation's business; what a caller is promised is that they answer alike, and nothing
	// about two separate mechanisms makes that true on its own. So it is asserted here.
	const flowview::BoundaryBinders binders = builtIn();

	for (const std::string value : {"+1", " 1", "0x10", "1 ", "", "hello"})
	{
		INFO("text: '" << value << "'");
		CHECK_FALSE(binders.bind(typeid(int), value).has_value());
		CHECK_FALSE(binders.bind(typeid(float), value).has_value());
	}

	// And what a float accepts that an int does not is a matter of what a float IS, not of which
	// parser it went through.
	CHECK(binders.bind(typeid(float), "1.5")->get<float>() == 1.5f);
	CHECK(binders.bind(typeid(float), "-1.5")->get<float>() == -1.5f);
	CHECK(binders.bind(typeid(float), "1e3")->get<float>() == 1000.0f);
	CHECK(binders.bind(typeid(float), "12")->get<float>() == 12.0f);
	CHECK_FALSE(binders.bind(typeid(int), "1.5").has_value());
	CHECK_FALSE(binders.bind(typeid(float), "1.5f").has_value());
}

TEST_CASE("a camera model binds from a camera-model document, and a typo is refused", "[flowview][binder]")
{
	// What `run --imported factory.json` reads. Through the real json codec and the real binder, so
	// this is the file a user writes or a calibration's model output left behind.
	flowview::registerSceneSerialization(); // the json codec
	const flowview::BoundaryBinders binders = builtIn();
	const std::filesystem::path dir = lain::testing::scratchDir() / "camera-model-binder";
	std::filesystem::create_directories(dir);

	camera::CameraModelParameters p;
	p.image = {1280, 800};
	p.intrinsics = {640.25, 641.5, 639.75, 401.125};
	p.distortion = camera::InverseBrownConrady5{-0.055, 0.061, -0.0007, 0.0003, -0.019};
	const camera::CameraModel model = *camera::CameraModel::create(p).model;
	const std::filesystem::path file = dir / "factory.json";
	REQUIRE(io::data::save(core::Uri::fromPath(file), camera::cameraModelToValue(model)));

	const std::optional<flow::PortValue> bound = binders.bind(typeid(camera::CameraModel), file.string());
	REQUIRE(bound.has_value());
	REQUIRE(bound->holds<camera::CameraModel>());
	CHECK(camera::cameraModelToValue(bound->get<camera::CameraModel>()) == camera::cameraModelToValue(model));

	// A coefficient name typed wrong. lain::data alone would read the model with k2 = 0 and say
	// nothing; the camera reader names the key, and the binder refuses.
	std::ifstream in(file);
	std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
	const std::size_t at = text.find("\"k2\"");
	REQUIRE(at != std::string::npos);
	text.replace(at, 4, "\"kk2\"");
	const std::filesystem::path typo = dir / "typo.json";
	std::ofstream(typo) << text;
	CHECK_FALSE(binders.bind(typeid(camera::CameraModel), typo.string()).has_value());

	CHECK_FALSE(binders.bind(typeid(camera::CameraModel), (dir / "absent.json").string()).has_value());
}
