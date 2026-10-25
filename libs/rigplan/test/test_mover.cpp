// Movers (mover.h; CONTEXT.md, "Mover"): a path's poses between and beyond its keys, a board placed by
// its path, a prop on a path, and what check() refuses of them.

#include "testplan.h"

#include <lain/rigplan/mover.h>
#include <lain/rigplan/realisation.h>
#include <lain/rigplan/rigplan.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <string>

using lain::math::Quatd;
using lain::math::RigidTransformd;
using lain::math::Vec3d;
using lain::rigplan::Aim;
using lain::rigplan::Path;

static void requireSame(const RigidTransformd& a, const RigidTransformd& b)
{
	REQUIRE(a.rotation() == b.rotation());
	REQUIRE(a.translation() == b.translation());
}

// The angle between two rotations, radians.
static double angleBetween(const Quatd& a, const Quatd& b)
{
	return 2.0 * std::acos(std::min(1.0, std::abs(lain::math::dot(a, b))));
}

static double distance(const Vec3d& a, const Vec3d& b)
{
	return lain::math::length(a - b);
}

TEST_CASE("a path holds its keys, and moves between them at a constant rate", "[rigplan][mover]")
{
	Path path;
	path.keys = {{2, {0.0, 1.0, 1.0}, Aim{}}, {6, {1.0, 1.0, 2.0}, Aim{90.0, 0.0, 0.0}}, {10, {1.0, 3.0, 2.0}, Aim{90.0, 30.0, 0.0}}};
	const auto keyPose = [&](std::size_t k)
	{ return lain::rigplan::mountFromPlacement(path.keys[k].position, path.keys[k].aim); };

	// At a key, the key's pose exactly; before the first and after the last, held.
	requireSame(lain::rigplan::poseAt(path, 2), keyPose(0));
	requireSame(lain::rigplan::poseAt(path, 6), keyPose(1));
	requireSame(lain::rigplan::poseAt(path, 10), keyPose(2));
	requireSame(lain::rigplan::poseAt(path, 0), keyPose(0));
	requireSame(lain::rigplan::poseAt(path, 50), keyPose(2));

	// Between two keys, linear in position and turning at a constant rate: a pan of 90 degrees over four
	// frames is 22.5 a frame.
	const double quarter = lain::math::radians(22.5);
	for (std::size_t f = 3; f <= 5; ++f)
	{
		const RigidTransformd pose = lain::rigplan::poseAt(path, f);
		const double t = double(f - 2) / 4.0;
		REQUIRE(distance(pose.translation(), Vec3d{t, 1.0, 1.0 + t}) < 1e-12);
		REQUIRE(angleBetween(pose.rotation(), keyPose(0).rotation()) == Catch::Approx(quarter * double(f - 2)).margin(1e-12));
	}
	// And the next segment turns about the pose's own tilt, the rest staying where the keys put it.
	REQUIRE(angleBetween(lain::rigplan::poseAt(path, 8).rotation(), keyPose(1).rotation()) ==
			Catch::Approx(lain::math::radians(15.0)).margin(1e-12));
}

TEST_CASE("a path turns the shorter way", "[rigplan][mover]")
{
	// From a pan of 170 to one of -170 is 20 degrees through 180, not 340 through 0.
	Path path;
	path.keys = {{0, {0.0, 0.0, 1.0}, Aim{170.0, 0.0, 0.0}}, {10, {0.0, 0.0, 1.0}, Aim{-170.0, 0.0, 0.0}}};
	const Vec3d forward = lain::rigplan::poseAt(path, 5).rotate({0.0, 0.0, 1.0}); // camera-frame forward
	REQUIRE(distance(forward, Vec3d{0.0, -1.0, 0.0}) < 1e-12);					  // a pan of 180 looks along -Y
}

TEST_CASE("a path of one key holds it everywhere", "[rigplan][mover]")
{
	Path path;
	path.keys = {{7, {1.0, 2.0, 3.0}, Aim{10.0, 20.0, 30.0}}};
	for (std::size_t f : {std::size_t{0}, std::size_t{7}, std::size_t{100}})
		requireSame(lain::rigplan::poseAt(path, f), lain::rigplan::mountFromPlacement(path.keys[0].position, path.keys[0].aim));
}

TEST_CASE("a path that cannot be followed says why", "[rigplan][mover]")
{
	REQUIRE(lain::rigplan::pathProblems(Path{}).size() == 1);

	Path path;
	path.keys = {{0, {0.0, 0.0, 0.0}, Aim{}}, {5, {0.0, 0.0, 0.0}, Aim{}}};
	REQUIRE(lain::rigplan::pathProblems(path).empty());
	path.keys[1].frame = 0; // two keys at one frame
	REQUIRE(lain::rigplan::pathProblems(path).size() == 1);
	path.keys = {{5, {0.0, 0.0, 0.0}, Aim{}}, {2, {0.0, 0.0, 0.0}, Aim{}}}; // out of order
	REQUIRE(lain::rigplan::pathProblems(path).size() == 1);
	path.keys = {{0, {std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0}, Aim{}}, {1, {0.0, 0.0, 0.0}, Aim{0.0, INFINITY, 0.0}}};
	REQUIRE(lain::rigplan::pathProblems(path).size() == 2);
}

TEST_CASE("a board's squares are centred on its path, facing the camera that shares its aim", "[rigplan][mover]")
{
	const lain::rigplan::BoardMover board = testplan::board();
	const double square = 0.024;
	const Vec3d centre{7 * square / 2, 5 * square / 2, 0.0}; // the squares' centre, in the board frame
	for (std::size_t f : {std::size_t{0}, std::size_t{6}, std::size_t{12}, std::size_t{30}})
	{
		const RigidTransformd planFromBoard = lain::rigplan::planFromBoard(board, f);
		const RigidTransformd pose = lain::rigplan::poseAt(board.path, f);
		REQUIRE(distance(planFromBoard.apply(centre), pose.translation()) < 1e-12);

		// A camera with the board's aim, a metre before it, sees it face on and upright: the board frame
		// is that camera's frame, moved a metre along its forward axis and back by half the squares.
		const Vec3d forward = pose.rotate({0.0, 0.0, 1.0});
		const RigidTransformd planFromCamera{pose.rotation(), pose.translation() - forward};
		const RigidTransformd cameraFromBoard = planFromCamera.inverse() * planFromBoard;
		REQUIRE(angleBetween(cameraFromBoard.rotation(), Quatd{1.0, 0.0, 0.0, 0.0}) < 1e-12);
		REQUIRE(distance(cameraFromBoard.translation(), Vec3d{-centre.x, -centre.y, 1.0}) < 1e-12);
	}
}

TEST_CASE("a prop on a path stands upright at rest, and a pan is its yaw", "[rigplan][mover]")
{
	lain::rigplan::Prop standing;
	standing.id = testplan::walkerId;
	standing.name = "walker";
	standing.mesh = lain::core::Uri{"/assets/walk.obj"};
	standing.unitMetres = 0.01;
	standing.position = {0.5, -1.0, 0.0};
	standing.yawDegrees = 25.0;

	lain::rigplan::Prop moving = standing;
	moving.position = {}; // unused once it follows a path
	moving.yawDegrees = 0;
	moving.path = Path{{{0, {0.5, -1.0, 0.0}, Aim{25.0, 0.0, 0.0}}, {10, {1.5, -1.0, 0.0}, Aim{25.0, 0.0, 0.0}}}};

	const lain::math::Mat4d still = lain::rigplan::planFromProp(standing, 3);
	const lain::math::Mat4d atStart = lain::rigplan::planFromProp(moving, 0);
	for (int c = 0; c < 4; ++c)
	{
		for (int r = 0; r < 4; ++r)
			REQUIRE(atStart[c][r] == Catch::Approx(still[c][r]).margin(1e-12));
	}
	// Halfway along, it has moved half the way and turned not at all.
	const lain::math::Mat4d halfway = lain::rigplan::planFromProp(moving, 5);
	REQUIRE(halfway[3][0] == Catch::Approx(1.0).margin(1e-12));
	REQUIRE(halfway[0][0] == Catch::Approx(still[0][0]).margin(1e-12));
	// A static prop ignores the frame.
	const lain::math::Mat4d later = lain::rigplan::planFromProp(standing, 40);
	REQUIRE(later == still);
}

TEST_CASE("check() refuses a board or a path that cannot be shown", "[rigplan][mover]")
{
	using lain::rigplan::PlanProblem;
	const auto problemsOf = [](const lain::rigplan::RigPlan& plan)
	{ return lain::rigplan::check(plan); };

	lain::rigplan::RigPlan plan = testplan::plan();
	plan.boards.push_back(testplan::board());
	REQUIRE(problemsOf(plan).empty());

	const auto refusedAs = [&](lain::rigplan::RigPlan broken, PlanProblem problem)
	{
		const auto diagnostics = problemsOf(broken);
		REQUIRE(diagnostics.size() == 1);
		REQUIRE(diagnostics[0].problem == problem);
		return diagnostics[0].detail;
	};

	lain::rigplan::RigPlan broken = plan;
	broken.boards[0].pattern.squaresX = 1;
	REQUIRE(refusedAs(broken, PlanProblem::InvalidMover).find("board 'charuco'") != std::string::npos);
	broken = plan;
	broken.boards[0].instance.squareLength.value = {};
	refusedAs(broken, PlanProblem::InvalidMover);
	broken = plan;
	broken.boards[0].pixelsPerSquare = 0;
	refusedAs(broken, PlanProblem::InvalidMover);
	broken = plan;
	broken.boards[0].margin = -0.01;
	refusedAs(broken, PlanProblem::InvalidMover);
	broken = plan;
	broken.boards[0].backing = 0;
	refusedAs(broken, PlanProblem::InvalidMover);
	broken = plan;
	broken.boards[0].path.keys.clear();
	refusedAs(broken, PlanProblem::InvalidMover);

	broken = plan;
	broken.boards.push_back(testplan::board());
	broken.boards[1].id = testplan::id("0193a6f0-0000-7000-8000-00000000b002");
	refusedAs(broken, PlanProblem::InvalidMover); // two boards of one name
	broken.boards[1].name = "second";
	broken.boards[1].id = testplan::cam0Id; // a board is an owner, so its identity is unique among all of them
	refusedAs(broken, PlanProblem::DuplicateIdentity);

	broken = testplan::plan();
	lain::rigplan::Prop prop;
	prop.id = testplan::walkerId;
	prop.name = "walker";
	prop.mesh = lain::core::Uri{"/assets/walk.obj"};
	prop.path = Path{{{4, {0.0, 0.0, 0.0}, Aim{}}, {4, {1.0, 0.0, 0.0}, Aim{}}}};
	broken.props.push_back(prop);
	REQUIRE(refusedAs(broken, PlanProblem::InvalidMover).find("prop 'walker'") != std::string::npos);
}
