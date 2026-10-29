// calibration::includes, accepts and Attempts: the calibration fallback's vocabulary, which the
// fallback's gating node and its record both ask, over reports built as a calibration hands them back.

#include "testcamera.h"

#include <lain/camera/calibration/attempts.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <optional>

using namespace lain::camera;
using namespace lain::camera::calibration;
using Catch::Matchers::StartsWith;

namespace
{
	// A report that succeeded with the test camera, under `verdict`.
	Report succeeded(Verdict verdict)
	{
		Report report;
		report.status = CalibrationStatus::Succeeded;
		report.model = testing::cameraWith(NoDistortion{});
		report.verdict = verdict;
		return report;
	}

	// A report that failed for `failure`, as every method's refusal reads.
	Report failed(Failure failure, const char* detail)
	{
		Report report;
		report.failures.push_back({failure, detail});
		return report;
	}
} // namespace

TEST_CASE("an attempt is accepted when it succeeded and is Ready", "[camera][calibration][attempts]")
{
	CHECK(accepts(succeeded(Verdict::Ready)));
	CHECK_FALSE(accepts(succeeded(Verdict::Exploratory)));
	CHECK_FALSE(accepts(succeeded(Verdict::Rejected)));
	CHECK_FALSE(accepts(failed(Failure::NoDetector, "this build cannot detect boards")));
	// A failed report's verdict is never read: one that says Ready and has no model is not accepted.
	Report ready = failed(Failure::EstimationFailed, "no fit");
	ready.verdict = Verdict::Ready;
	CHECK_FALSE(accepts(ready));
}

TEST_CASE("the accepted attempt is the first that accepts", "[camera][calibration][attempts]")
{
	Attempts attempts;
	CHECK_FALSE(attempts.accepted().has_value());

	attempts.reports = {failed(Failure::NoDetector, "none"), succeeded(Verdict::Exploratory)};
	CHECK_FALSE(attempts.accepted().has_value()); // the attempts still say why

	// Two that accept: the earlier one in priority order, not the later.
	attempts.reports = {succeeded(Verdict::Exploratory), succeeded(Verdict::Ready), succeeded(Verdict::Ready)};
	REQUIRE(attempts.accepted().has_value());
	CHECK(*attempts.accepted() == 1);

	attempts.reports = {succeeded(Verdict::Ready), failed(Failure::NoFootage, "empty")};
	CHECK(attempts.accepted() == std::optional<std::size_t>{0});
}

TEST_CASE("a calibration method includes the single methods it names", "[camera][calibration][attempts]")
{
	CHECK(includes(Method::Board, Method::Board));
	CHECK_FALSE(includes(Method::Board, Method::Targetless));
	CHECK_FALSE(includes(Method::Targetless, Method::Board));
	CHECK(includes(Method::Targetless, Method::Targetless));
	CHECK(includes(Method::BoardThenTargetless, Method::Board));
	CHECK(includes(Method::BoardThenTargetless, Method::Targetless));
}

TEST_CASE("the attempts read as which was accepted, or as each that was not", "[camera][calibration][attempts]")
{
	CHECK(Attempts{}.toString() == "No attempts");

	const Attempts accepted{{failed(Failure::NoDetector, "this build cannot detect boards"), succeeded(Verdict::Ready)}};
	CHECK_THAT(accepted.toString(), StartsWith("Attempt 2 of 2 accepted: Ready: "));

	const Attempts none{{failed(Failure::NoDetector, "this build cannot detect boards"), succeeded(Verdict::Exploratory)}};
	const std::string text = none.toString();
	CHECK_THAT(text, StartsWith("No attempt of 2 accepted; 1: Failed: NoDetector (this build cannot detect boards); 2: "
								"Exploratory: "));
}
