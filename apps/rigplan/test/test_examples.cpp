// The committed example rig plans (apps/rigplan/examples), held to the catalog that writes them. They
// are the first rig plans in the tree written by an EARLIER build rather than by the test reading them,
// so they are also the first check that an existing rig plan still reads: committing the first froze
// the rig plan document at version 1 (M15 slice 7).
//
// Three claims. Every committed example is byte for byte what the builder writes today, so a builder
// change that is not regenerated, or a hand edit, fails here rather than drifting. Every one reads back
// strictly into a rig plan check() finds nothing wrong with, and whose estimate has no problem. And one
// that waves a board is refused a capture where no board renderer is, naming the board.

#include "examples.h"

#include <lain/core/uri.h>
#include <lain/data/value.h>
#include <lain/io/data/codecs.h>
#include <lain/io/data/load.h>
#include <lain/io/image/codecs.h>
#include <lain/io/mesh/codecs.h>
#include <lain/rigplan/capture.h>
#include <lain/rigplan/rigplan.h>
#include <lain/rigplan/serialize/rigplan.h>
#include <lain/testing/scratch.h>

#include <archimedes/archimedes.h>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using rigplan::examples::catalog;
using rigplan::examples::Example;

static const fs::path kExamples{LAIN_RIGPLAN_EXAMPLES_DIR};

static void registerCodecs()
{
	lain::io::data::registerDataCodecs();
	lain::io::image::registerImageCodecs(); // the estimate encodes a flat frame through each sensor's codec
	lain::io::mesh::registerMeshCodecs();
}

static std::string bytesOf(const fs::path& file)
{
	std::ifstream in(file, std::ios::binary);
	return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// The committed example as rigplan render reads it: its paths read against its own folder (ADR-0027).
static lain::rigplan::RigPlanRead readCommitted(const Example& example)
{
	std::optional<lain::data::Value> document = lain::io::data::load(lain::core::Uri::fromPath(kExamples / (example.name + ".json")));
	REQUIRE(document.has_value());
	lain::rigplan::resolvePaths(*document, kExamples);
	return lain::rigplan::rigPlanFromValue(*document);
}

static bool mentions(const std::vector<std::string>& problems, const std::string& text)
{
	for (const std::string& problem : problems)
		if (problem.find(text) != std::string::npos)
			return true;
	return false;
}

TEST_CASE("the committed example rig plans are exactly what the builder writes", "[rigplan][examples]")
{
	// Regenerate into scratch and compare bytes. A failure here means a builder changed without the
	// documents being rewritten (`cmake --build build --target rig-plans`, then commit what it wrote),
	// or a document was edited by hand. Since the document is frozen at version 1, a change of SHAPE
	// that shows up here is a new document version and a migrator, not a regeneration.
	registerCodecs();
	const fs::path scratch = lain::testing::scratchDir() / "rig-plans";
	fs::create_directories(scratch);
	rigplan::examples::writeDocuments(scratch);
	for (const Example& example : catalog())
	{
		INFO(example.name);
		REQUIRE(fs::exists(kExamples / (example.name + ".json")));
		CHECK(bytesOf(scratch / (example.name + ".json")) == bytesOf(kExamples / (example.name + ".json")));
	}
}

TEST_CASE("every example rig plan reads back strictly and can be realised", "[rigplan][examples]")
{
	registerCodecs();
	for (const Example& example : catalog())
	{
		INFO(example.name);
		const lain::rigplan::RigPlanRead read = readCommitted(example);
		INFO((read.problems.empty() ? std::string() : read.problems.front()));
		REQUIRE(read.problems.empty());
		REQUIRE(read.plan.has_value());

		const std::vector<lain::rigplan::PlanDiagnostic> problems = lain::rigplan::check(*read.plan);
		INFO((problems.empty() ? std::string() : problems.front().detail));
		CHECK(problems.empty());
		// What was read writes back as the builder wrote it: a field the reader dropped would show here.
		CHECK(lain::rigplan::rigPlanToValue(*read.plan) == lain::rigplan::rigPlanToValue(example.build()));

		const lain::rigplan::SizeEstimate estimate = lain::rigplan::estimateSize(*read.plan, {});
		INFO((estimate.problems.empty() ? std::string() : estimate.problems.front()));
		CHECK(estimate.problems.empty());
		CHECK(estimate.cameras.size() == read.plan->cameras.size());
		CHECK(estimate.frames == read.plan->timeline.frames);
	}
}

TEST_CASE("an example that waves a board is refused a capture where no board renderer is", "[rigplan][examples]")
{
	// This executable registers no camera backend, so no board renderer: the plan still reads whole (a
	// board mover is vocabulary), and a capture is refused before anything is written, naming the board.
	registerCodecs();
	for (const Example& example : catalog())
	{
		if (!example.needsBoardRenderer)
			continue;
		INFO(example.name);
		const lain::rigplan::RigPlanRead read = readCommitted(example);
		REQUIRE(read.plan.has_value());
		REQUIRE_FALSE(read.plan->boards.empty());

		lain::rigplan::CaptureOptions options;
		options.out = lain::testing::scratchPath("refused-capture");
		acm::Device none; // never reached: the refusal comes first
		const lain::rigplan::CaptureReport report = lain::rigplan::writeCapture(*read.plan, none, options);
		CHECK(report.refused);
		CHECK(mentions(report.problems, "board '" + read.plan->boards.front().name + "'"));
		CHECK_FALSE(fs::exists(options.out));
	}
}
