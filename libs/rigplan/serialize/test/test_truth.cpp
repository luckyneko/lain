// A synthetic capture's ground truth as documents: truth/poses.json and truth/board.json read back as
// they were written, and refuse what a hand edit got wrong. And a rig plan's reference camera.

#include "testplan.h"

#include <lain/rigplan/realisation.h>
#include <lain/rigplan/serialize/rigplan.h>
#include <lain/rigplan/serialize/truth.h>
#include <lain/rigplan/truth.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using lain::data::Value;

static Value& member(Value& object, std::string_view key)
{
	for (auto& [k, v] : *object.asObject())
	{
		if (k == key)
			return v;
	}
	FAIL("no member " << key);
	throw; // unreachable: FAIL throws
}

static Value& element(Value& array, std::size_t index)
{
	return (*array.asArray())[index];
}

static std::vector<std::string> keysOf(const Value& object)
{
	std::vector<std::string> keys;
	for (const auto& entry : *object.asObject())
		keys.push_back(entry.first);
	return keys;
}

static lain::rigplan::RigPlan planWithBoard()
{
	lain::rigplan::RigPlan plan = testplan::plan();
	plan.boards.push_back(testplan::board());
	return plan;
}

static lain::rigplan::Realisation realised(const lain::rigplan::RigPlan& plan)
{
	lain::rigplan::RealisationResult result = lain::rigplan::realise(plan);
	REQUIRE(result.realisation.has_value());
	return std::move(*result.realisation);
}

// A rotation read back is renormalised on construction, so it may differ from the one written by an ulp.
static void requireNear(const lain::math::RigidTransformd& a, const lain::math::RigidTransformd& b)
{
	REQUIRE(std::abs(std::abs(lain::math::dot(a.rotation(), b.rotation())) - 1.0) < 1e-15);
	REQUIRE(a.translation() == b.translation());
}

TEST_CASE("the truth's poses read back as they were written", "[rigplan][serialize][truth]")
{
	const auto poses = lain::rigplan::cameraPoses(realised(testplan::plan()));
	Value document = lain::rigplan::posesToValue(poses);
	REQUIRE(keysOf(document) == std::vector<std::string>{"version", "cameras"});
	Value& first = element(member(document, "cameras"), 0);
	REQUIRE(keysOf(first) == std::vector<std::string>{"id", "name", "planFromCamera"});
	REQUIRE(keysOf(member(first, "planFromCamera")) == std::vector<std::string>{"rotation", "translation"});
	REQUIRE(keysOf(member(member(first, "planFromCamera"), "rotation")) == std::vector<std::string>{"w", "x", "y", "z"});

	const lain::rigplan::PosesRead read = lain::rigplan::posesFromValue(document);
	for (const std::string& p : read.problems)
		UNSCOPED_INFO(p);
	REQUIRE(read.poses.has_value());
	REQUIRE(read.poses->size() == poses.size());
	for (std::size_t i = 0; i < poses.size(); ++i)
	{
		REQUIRE((*read.poses)[i].id == poses[i].id);
		REQUIRE((*read.poses)[i].name == poses[i].name);
		requireNear((*read.poses)[i].planFromCamera, poses[i].planFromCamera);
	}
}

TEST_CASE("the truth's board corners read back as they were written", "[rigplan][serialize][truth]")
{
	const lain::rigplan::RigPlan plan = planWithBoard();
	const lain::rigplan::Realisation realisation = realised(plan);
	std::vector<lain::rigplan::BoardTruth> truth;
	for (std::size_t f : {std::size_t{12}, std::size_t{0}}) // written frames ascending, whatever order given
	{
		const auto atFrame = lain::rigplan::boardTruth(plan, realisation, f);
		truth.insert(truth.end(), atFrame.begin(), atFrame.end());
	}
	Value document = lain::rigplan::boardTruthToValue(truth);
	Value& frame = element(member(document, "frames"), 0);
	REQUIRE(member(frame, "frame") == Value(std::uint64_t{0}));
	Value& board = element(member(frame, "boards"), 0);
	REQUIRE(keysOf(board) == std::vector<std::string>{"id", "name", "planFromBoard", "cameras"});
	Value& cam0 = element(member(board, "cameras"), 0);
	REQUIRE(keysOf(cam0) == std::vector<std::string>{"id", "name", "inView"});
	REQUIRE(keysOf(element(member(cam0, "inView"), 0)) == std::vector<std::string>{"corner", "u", "v"});

	const lain::rigplan::BoardTruthRead read = lain::rigplan::boardTruthFromValue(document);
	for (const std::string& p : read.problems)
		UNSCOPED_INFO(p);
	REQUIRE(read.truth.has_value());
	REQUIRE(read.truth->size() == 2);
	const lain::rigplan::BoardTruth& back = (*read.truth)[0];
	const lain::rigplan::BoardTruth& original = truth[1]; // frame 0
	REQUIRE(back.frame == 0);
	REQUIRE(back.board == original.board);
	REQUIRE(back.name == original.name);
	requireNear(back.planFromBoard, original.planFromBoard);
	REQUIRE(back.cameras.size() == original.cameras.size());
	for (std::size_t c = 0; c < back.cameras.size(); ++c)
	{
		REQUIRE(back.cameras[c].camera == original.cameras[c].camera);
		REQUIRE(back.cameras[c].inView.size() == original.cameras[c].inView.size());
		for (std::size_t k = 0; k < back.cameras[c].inView.size(); ++k)
		{
			REQUIRE(back.cameras[c].inView[k].corner == original.cameras[c].inView[k].corner);
			REQUIRE(back.cameras[c].inView[k].pixel == original.cameras[c].inView[k].pixel);
		}
	}
	REQUIRE((*read.truth)[1].frame == 12);
}

TEST_CASE("a truth document refuses what a hand edit got wrong", "[rigplan][serialize][truth]")
{
	const Value written = lain::rigplan::posesToValue(lain::rigplan::cameraPoses(realised(testplan::plan())));
	const auto mentions = [](const std::vector<std::string>& problems, std::string_view text)
	{
		for (const std::string& p : problems)
		{
			if (p.find(text) != std::string::npos)
				return true;
		}
		return false;
	};

	Value document = written;
	member(element(member(document, "cameras"), 1), "id") = Value(std::string("cam1"));
	auto read = lain::rigplan::posesFromValue(document);
	REQUIRE_FALSE(read.poses.has_value());
	REQUIRE(mentions(read.problems, "cameras[1].id"));

	document = written;
	member(member(member(element(member(document, "cameras"), 0), "planFromCamera"), "rotation"), "w") = Value(2.0);
	read = lain::rigplan::posesFromValue(document);
	REQUIRE_FALSE(read.poses.has_value());
	REQUIRE(mentions(read.problems, "not a unit quaternion"));

	document = written;
	auto& entries = *element(member(document, "cameras"), 0).asObject();
	entries[1].first = "nmae";
	read = lain::rigplan::posesFromValue(document);
	REQUIRE_FALSE(read.poses.has_value());
	REQUIRE(mentions(read.problems, "nmae"));

	document = written;
	member(document, "version") = Value(std::uint64_t{2});
	REQUIRE_FALSE(lain::rigplan::posesFromValue(document).poses.has_value());
	REQUIRE_FALSE(lain::rigplan::boardTruthFromValue(Value(std::string("not a document"))).truth.has_value());
}

TEST_CASE("a rig plan's reference camera reads back, and is absent when it names none", "[rigplan][serialize][truth]")
{
	lain::rigplan::RigPlan plan = testplan::plan();
	REQUIRE_FALSE(lain::rigplan::rigPlanToValue(plan).find("reference"));
	plan.reference = testplan::cam2Id;
	Value document = lain::rigplan::rigPlanToValue(plan);
	REQUIRE(member(document, "reference") == Value(testplan::cam2Id.toString()));
	const auto read = lain::rigplan::rigPlanFromValue(document);
	REQUIRE(read.plan.has_value());
	REQUIRE(read.plan->reference == testplan::cam2Id);

	member(document, "reference") = Value(std::string("cam2"));
	REQUIRE_FALSE(lain::rigplan::rigPlanFromValue(document).plan.has_value());
}
