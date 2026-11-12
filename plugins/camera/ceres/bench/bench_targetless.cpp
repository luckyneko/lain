// What a targetless registration of 100 cameras costs through the Ceres refiner. It was a measurement
// test_targetless.cpp held to a 120 s bound until 2026-11-12. That test keeps what lain's code
// guarantees (it registers, Ready, the worst camera within a milliradian); this prints what it costs.
// Reported rather than checked: under `ctest -j8` beside the end-to-end suites the same registration
// took 154 to 181 s, and 36 s alone.
//
// The scene is test_targetless.cpp's own (scene::resetRing), so a number printed here describes the
// case beside it.

#include <lain/camera/ceres/register.h>
#include <lain/camera/registration/report.h>
#include <lain/camera/registration/targetless.h>
#include <lain/camera/testing/syntheticscene.h>
#include <lain/core/time.h>
#include <lain/meta/enums.h>
#include <lain/string/format.h>
#include <lain/task/task.h>
#include <lain/testing/skip.h>
#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>

#include <iostream>
#include <string>

using namespace lain;
using namespace lain::camera;
using namespace lain::camera::registration;
namespace scene = lain::camera::testing::scene;

static void line(const std::string& label, const std::string& value)
{
	std::cout << string::format("  {:<16} {}\n", label, value);
}

TEST_CASE("a rig of 100 cameras and 50000 tracks registers", "[camera][ceres][targetless][bench]")
{
	lain::testing::skipInDebug("a Debug build's timings mean nothing");
	lain::camera::ceres::registerBackend();
	scene::registerStandIns();
	// A ring of 100 cameras 1 m from the origin, looking at it, and 50,000 landmarks, each seen by the 8
	// cameras from its index on.
	//
	// Last measured on linux-x86_64, Release, four cores: 40 s in the method, 7.0 s of it the refinement,
	// the rest mostly the ten bootstrap registrations; 1.26 GB at peak for the whole test, its scene
	// included (WORK.md, M9 slice 3, sub-slice 8b). On 2026-10-19 the whole case took 36 s with nothing
	// beside it.
	scene::resetRing(100, 50000, 8, 9);
	const feature::TrackSet set = scene::trackSetOf(0.034 * 4, 5);
	REQUIRE(set.tracks.size() == 50000);

	const lain::testing::ThreadPool pool;
	const core::Time start = core::Time::now();
	const Report report = targetless::registerCameras(scene::rig(), set, Request{});
	const double seconds = (core::Time::now() - start).seconds();
	REQUIRE(report.status == RegistrationStatus::Succeeded);

	std::cout << string::format("a rig of 100 cameras and 50000 tracks registers, {} workers\n", task::threadCount());
	line("method", string::format("{:.1f} s, {}", seconds, meta::enums::name(report.verdict)));
	if (report.diagnostics.refinement)
		line("refinement", string::format("{:.1f} s, {} iterations", report.diagnostics.refinement->elapsed.seconds(),
										  report.diagnostics.refinement->iterations));
}
