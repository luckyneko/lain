// Camera values as documents: camera models, board specifications and capture records, written and
// read back in memory. The reader is strict where lain::data is best-effort, so most cases here are
// about what it REFUSES, by name, instead of loading a default.

#include <lain/camera/serialize/board.h>
#include <lain/camera/serialize/cameramodel.h>
#include <lain/camera/serialize/capturerecord.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <variant>

using namespace lain;
using namespace lain::camera;
using lain::data::Value;

namespace
{
	CameraModelParameters parametersWith(Distortion distortion)
	{
		CameraModelParameters p;
		p.image = {1280, 800};
		p.intrinsics = {640.25, 641.5, 639.75, 401.125};
		p.distortion = distortion;
		return p;
	}

	CameraModel modelWith(Distortion distortion)
	{
		ModelResult result = CameraModel::create(parametersWith(distortion));
		REQUIRE(result.model.has_value());
		return *result.model;
	}

	// The same document with one member replaced, found by a path of keys.
	Value edited(Value document, std::initializer_list<const char*> path, Value replacement)
	{
		Value* at = &document;
		const char* last = nullptr;
		for (const char* key : path)
		{
			if (last)
			{
				Value* next = nullptr;
				for (auto& [name, value] : *at->asObject())
				{
					if (name == last)
						next = &value;
				}
				REQUIRE(next != nullptr);
				at = next;
			}
			last = key;
		}
		at->set(last, std::move(replacement));
		return document;
	}

	bool mentions(const std::vector<std::string>& problems, const std::string& text)
	{
		for (const std::string& problem : problems)
		{
			if (problem.find(text) != std::string::npos)
				return true;
		}
		return false;
	}

	std::string joined(const std::vector<std::string>& problems)
	{
		std::string text;
		for (const std::string& problem : problems)
			text += "\n  " + problem;
		return text;
	}
} // namespace

TEST_CASE("every distortion model round-trips, under its own stable key", "[camera][serialize]")
{
	// The keys ARE the serialization identity (ADR-0016): a document names a model by them, so this
	// pins them, and renaming one fails here rather than in someone's saved file.
	struct Case
	{
		Distortion distortion;
		const char* key;
	};
	const Case cases[] = {
		{NoDistortion{}, "none"},
		{BrownConrady5{-0.11, 0.052, 0.0011, -0.0004, 0.003}, "brownConrady5"},
		{InverseBrownConrady5{-0.055, 0.061, -0.0007, 0.0003, -0.019}, "inverseBrownConrady5"},
		{ModifiedBrownConrady5{-0.05, 0.02, 0.001, 0.0005, 0.0}, "modifiedBrownConrady5"},
		{RationalBrownConrady8{0.2, -0.01, 0.001, -0.002, 0.003, 0.25, 0.01, 0.002}, "rationalBrownConrady8"},
		{KannalaBrandt4{0.03, -0.004, 0.001, -0.0002}, "kannalaBrandt4"},
	};
	for (const Case& c : cases)
	{
		INFO("model: " << c.key);
		const CameraModel model = modelWith(c.distortion);
		const Value document = cameraModelToValue(model);
		REQUIRE(document.find("distortion") != nullptr);
		CHECK(*document.find("distortion")->find("type")->asString() == c.key);

		const CameraModelRead read = cameraModelFromValue(document);
		INFO("problems:" << joined(read.problems));
		REQUIRE(read.model.has_value());
		CHECK(read.problems.empty());
		CHECK(read.model->image() == model.image());
		CHECK(read.model->intrinsics().fx == model.intrinsics().fx);
		CHECK(read.model->intrinsics().cy == model.intrinsics().cy);
		CHECK(cameraModelToValue(*read.model) == document); // written back, byte for byte the same document
	}
}

TEST_CASE("a camera-model document is read strictly", "[camera][serialize]")
{
	const Value good = cameraModelToValue(modelWith(BrownConrady5{-0.11, 0.052, 0.0011, -0.0004, 0.003}));
	REQUIRE(cameraModelFromValue(good).model.has_value());

	SECTION("an unknown distortion model is named, not read as no distortion")
	{
		const CameraModelRead read = cameraModelFromValue(edited(good, {"distortion", "type"}, Value("brownConrady")));
		CHECK_FALSE(read.model.has_value());
		INFO("problems:" << joined(read.problems));
		CHECK(mentions(read.problems, "'distortion.type' is 'brownConrady'"));
	}
	SECTION("a misspelled coefficient is named, not read as zero")
	{
		Value document = good;
		Value* value = nullptr;
		for (auto& [name, member] : *document.asObject())
		{
			if (name == "distortion")
			{
				for (auto& [inner, arm] : *member.asObject())
				{
					if (inner == "value")
						value = &arm;
				}
			}
		}
		REQUIRE(value != nullptr);
		Value renamed = Value::object();
		for (const auto& [name, coefficient] : *value->asObject())
			renamed.set(name == "k2" ? "kk2" : name, coefficient);
		*value = renamed;

		const CameraModelRead read = cameraModelFromValue(document);
		CHECK_FALSE(read.model.has_value());
		INFO("problems:" << joined(read.problems));
		CHECK(mentions(read.problems, "'distortion.value.kk2' is not a known key"));
		CHECK(mentions(read.problems, "'distortion.value.k2' is missing"));
	}
	SECTION("a value of the wrong kind is named")
	{
		const CameraModelRead read = cameraModelFromValue(edited(good, {"intrinsics", "fx"}, Value("640")));
		CHECK_FALSE(read.model.has_value());
		CHECK(mentions(read.problems, "'intrinsics.fx' is a string"));
	}
	SECTION("parameters that are not a camera come back with create's reasons")
	{
		const CameraModelRead read = cameraModelFromValue(edited(good, {"intrinsics", "fx"}, Value(-640.0)));
		CHECK_FALSE(read.model.has_value());
		CHECK(mentions(read.problems, "fx"));
	}
	SECTION("a document with no version, or a newer one, is refused")
	{
		Value unversioned = Value::object();
		for (const auto& [name, member] : *good.asObject())
		{
			if (name != "version")
				unversioned.set(name, member);
		}
		CHECK(mentions(cameraModelFromValue(unversioned).problems, "no version"));
		CHECK(mentions(cameraModelFromValue(edited(good, {"version"}, Value(std::uint64_t{2}))).problems, "newer"));
	}
}

TEST_CASE("a board specification round-trips in millimetres, exactly", "[camera][serialize]")
{
	board::PatternParameters p;
	p.dictionary = board::Dictionary::Aruco6x6_250;
	p.squaresX = 9;
	p.squaresY = 6;
	p.markerToSquare = 0.7;
	p.firstMarkerId = 17;
	p.layout = board::CharucoLayout::Legacy;
	board::Instance instance;
	instance.identity = "print 2";
	instance.squareLength.value = core::Length::from<core::Length::Millimetres>(23.7);
	instance.squareLength.lowerBound = core::Length::from<core::Length::Millimetres>(23.65);
	const board::Specification spec =
		*board::Specification::create(*board::Pattern::create(p).pattern, instance).specification;

	const Value document = board::specificationToValue(spec);
	CHECK(document.find("instance")->find("squareLengthMm")->asDouble() == 23.7);
	CHECK(document.find("instance")->find("squareUpperBoundMm") == nullptr); // absent, not zero

	const board::SpecificationRead read = board::specificationFromValue(document);
	INFO("problems:" << joined(read.problems));
	REQUIRE(read.specification.has_value());
	CHECK(read.specification->pattern().fingerprint() == spec.pattern().fingerprint());
	CHECK(read.specification->instance().squareLength.value == core::Length::from<core::Length::Millimetres>(23.7));
	CHECK(read.specification->instance().squareLength.lowerBound == core::Length::from<core::Length::Millimetres>(23.65));
	CHECK_FALSE(read.specification->instance().squareLength.upperBound.has_value());
	CHECK(read.specification->instance().identity == "print 2");

	SECTION("a misspelled dictionary is named, not read as the first one")
	{
		const board::SpecificationRead bad =
			board::specificationFromValue(edited(document, {"pattern", "dictionary"}, Value("ARUCO_6X6_250")));
		CHECK_FALSE(bad.specification.has_value());
		INFO("problems:" << joined(bad.problems));
		CHECK(mentions(bad.problems, "'pattern.dictionary' is 'ARUCO_6X6_250'"));
	}
	SECTION("a pattern that is not a board comes back with create's reasons")
	{
		const board::SpecificationRead bad =
			board::specificationFromValue(edited(document, {"pattern", "squaresX"}, Value(std::uint64_t{1})));
		CHECK_FALSE(bad.specification.has_value());
		CHECK(mentions(bad.problems, "2x2"));
	}
}

TEST_CASE("a capture record round-trips, with and without an imported model", "[camera][serialize]")
{
	CaptureRecord record;
	record.device = {"Intel", "RealSense D455", "123622270388"};
	record.stream = {1280, 800, "RGB8", 30.0};
	record.properties = {{"firmware", "5.16.0.1"}, {"sdk", "2.55.1"}};
	record.importedModel = ImportedModel{"RealSense factory calibration",
										 parametersWith(InverseBrownConrady5{-0.055, 0.061, -0.0007, 0.0003, -0.019})};

	const std::optional<CaptureRecord> read = data::fromValue<CaptureRecord>(data::toValue(record));
	REQUIRE(read.has_value());
	CHECK(read->device.identity == "123622270388");
	CHECK(read->stream.format == "RGB8");
	CHECK(read->stream.framesPerSecond == 30.0);
	CHECK(read->properties == record.properties);
	REQUIRE(read->importedModel.has_value());
	CHECK(read->importedModel->source == "RealSense factory calibration");
	CHECK(data::toValue(read->importedModel->parameters) == data::toValue(record.importedModel->parameters));

	record.importedModel.reset();
	record.stream.framesPerSecond.reset();
	const Value bare = data::toValue(record);
	CHECK(bare.find("importedModel") == nullptr); // an absent model is absent, not an empty one
	const std::optional<CaptureRecord> readBare = data::fromValue<CaptureRecord>(bare);
	REQUIRE(readBare.has_value());
	CHECK_FALSE(readBare->importedModel.has_value());
	CHECK_FALSE(readBare->stream.framesPerSecond.has_value());
}
