// Board patterns and specifications: what makes one, what refuses one, the fingerprint that
// identifies one, and where its corners are.

#include "testboard.h"

#include <lain/camera/board/pattern.h>
#include <lain/camera/board/specification.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string>

using namespace lain;
using namespace lain::camera::board;
using namespace lain::camera::testing;

namespace
{
	bool reports(const PatternResult& result, PatternProblem problem)
	{
		for (const PatternDiagnostic& d : result.diagnostics)
		{
			if (d.problem == problem)
				return true;
		}
		return false;
	}

	bool reports(const SpecificationResult& result, SpecificationProblem problem)
	{
		for (const SpecificationDiagnostic& d : result.diagnostics)
		{
			if (d.problem == problem)
				return true;
		}
		return false;
	}
} // namespace

TEST_CASE("a pattern counts its markers and its inner corners", "[camera][board]")
{
	const Pattern p = pattern();
	CHECK(p.markerCount() == 17); // 35 squares, the white half rounded down
	CHECK(p.cornerCount() == 24); // 6 x 4 inner corners
	CHECK(markerCapacity(Dictionary::Aruco5x5_100) == 100);
	CHECK(std::string(name(Dictionary::Aruco7x7_1000)) == "ARUCO_7X7_1000");
}

TEST_CASE("a pattern is described and fingerprinted canonically", "[camera][board]")
{
	// Pinned text and digest: changing either is a format change, which must bump the version line
	// rather than silently re-identifying every pattern.
	const Pattern p = pattern();
	CHECK(p.description() == "lain.board.charuco/1\n"
							 "dictionary ARUCO_5X5_100\n"
							 "squares 7x5\n"
							 "markerToSquare 0.750000\n"
							 "firstMarkerId 0\n"
							 "layout standard\n");
	CHECK(p.fingerprint().toString() == "bbb863628b1b8db2ebb93ca9c3ffa394c348146e407f6611ba943baebd919fe0");
}

TEST_CASE("every field of a pattern is part of its identity, and nothing else is", "[camera][board]")
{
	const Pattern base = pattern();
	CHECK(pattern().fingerprint() == base.fingerprint());

	// To a millionth: two patterns that print identically cannot fingerprint apart.
	PatternParameters nearly = patternParameters();
	nearly.markerToSquare = 0.7500000001;
	CHECK(pattern(nearly).fingerprint() == base.fingerprint());

	PatternParameters p = patternParameters();
	p.dictionary = Dictionary::Aruco5x5_250;
	CHECK(pattern(p).fingerprint() != base.fingerprint());
	p = patternParameters();
	p.squaresX = 5;
	p.squaresY = 7; // the same squares turned: a different board
	CHECK(pattern(p).fingerprint() != base.fingerprint());
	p = patternParameters();
	p.markerToSquare = 0.7;
	CHECK(pattern(p).fingerprint() != base.fingerprint());
	p = patternParameters();
	p.firstMarkerId = 17;
	CHECK(pattern(p).fingerprint() != base.fingerprint());
	p = patternParameters();
	p.layout = CharucoLayout::Legacy;
	CHECK(pattern(p).fingerprint() != base.fingerprint());
}

TEST_CASE("create refuses a pattern that cannot be printed as described", "[camera][board]")
{
	PatternParameters p = patternParameters();
	p.squaresY = 1;
	CHECK(reports(Pattern::create(p), PatternProblem::TooFewSquares));

	for (const double ratio : {0.0, 1.0, 1.5, -0.2, 1e-9, std::numeric_limits<double>::quiet_NaN()})
	{
		p = patternParameters();
		p.markerToSquare = ratio;
		INFO("ratio " << ratio);
		CHECK(reports(Pattern::create(p), PatternProblem::MarkerRatioOutOfRange));
	}

	p = patternParameters();
	p.firstMarkerId = 90; // 17 markers from 90 need ids up to 106 of 100
	CHECK(reports(Pattern::create(p), PatternProblem::DictionaryTooSmall));
	p.firstMarkerId = 83; // exactly fits: ids 83..99
	CHECK(Pattern::create(p).pattern.has_value());

	// Every problem at once, not only the first.
	PatternParameters worst;
	worst.dictionary = Dictionary::Aruco4x4_50;
	worst.squaresX = 1;
	worst.squaresY = 200;
	worst.markerToSquare = 2.0;
	CHECK(Pattern::create(worst).diagnostics.size() == 3);
}

TEST_CASE("a specification places each corner in the board frame, in metres", "[camera][board]")
{
	const Specification s = specification();
	CHECK(s.markerLength() == core::Length::fromMillimetres(18.0));

	// Row by row from the top left, one square in from each edge.
	REQUIRE(s.cornerPosition(0).has_value());
	CHECK(s.cornerPosition(0)->x == Catch::Approx(0.024));
	CHECK(s.cornerPosition(0)->y == Catch::Approx(0.024));
	CHECK(s.cornerPosition(0)->z == 0.0);
	CHECK(s.cornerPosition(5)->x == Catch::Approx(0.144)); // the end of the first row of six
	CHECK(s.cornerPosition(5)->y == Catch::Approx(0.024));
	CHECK(s.cornerPosition(6)->x == Catch::Approx(0.024)); // the start of the second
	CHECK(s.cornerPosition(6)->y == Catch::Approx(0.048));
	CHECK(s.cornerPosition(23)->y == Catch::Approx(0.096));
	CHECK_FALSE(s.cornerPosition(24).has_value());
}

TEST_CASE("create refuses a board instance it cannot measure by", "[camera][board]")
{
	Instance instance;
	instance.identity = "";
	instance.squareLength.value = core::Length{};
	instance.squareLength.lowerBound = core::Length::fromMillimetres(1.0);
	const SpecificationResult result = Specification::create(pattern(), instance);
	CHECK_FALSE(result.specification.has_value());
	CHECK(reports(result, SpecificationProblem::NoIdentity));
	CHECK(reports(result, SpecificationProblem::NonPositiveLength));
	CHECK(reports(result, SpecificationProblem::BoundsDoNotContainValue));

	// Bounds are optional, and either may be absent: unknown uncertainty is legitimate.
	instance.identity = "A3-001";
	instance.squareLength.value = core::Length::fromMillimetres(24.0);
	instance.squareLength.lowerBound = core::Length::fromMillimetres(23.95);
	CHECK(Specification::create(pattern(), instance).specification.has_value());
	instance.squareLength.upperBound = core::Length::fromMillimetres(23.99);
	CHECK(reports(Specification::create(pattern(), instance), SpecificationProblem::BoundsDoNotContainValue));
}
