// The renderers the end-to-end tests stand on, checked against each other. texturedscene.h draws a
// printed board moving through a textured scene, so one rig can be registered by both methods from
// the same footage; syntheticfootage.h draws the board alone, and its frames are what board
// calibration and registration were measured on. Built where the board renderer is, since a board
// raster comes from it.

#include "registration.h"
#include "syntheticfootage.h"
#include "testboard.h"
#include "texturedscene.h"

#include <lain/camera/board/rendering.h>
#include <lain/camera/cameramodel.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace lain;
using namespace lain::camera;

TEST_CASE("a board in the textured scene draws as the board footage does", "[camera][rendering]")
{
	fixture::ensureRegistered();
	const board::Specification spec = camera::testing::specification();
	const board::RenderResult rendered = board::render(spec.pattern(), board::RenderRequest{60, 20});
	REQUIRE(rendered.rendering.has_value());
	const auto rendering = std::make_shared<const board::Rendering>(*rendered.rendering);

	const synthetic::Pinhole pinhole{};
	CameraModelParameters p;
	p.image = {std::uint32_t(pinhole.width), std::uint32_t(pinhole.height)};
	p.intrinsics = {pinhole.fx, pinhole.fy, pinhole.cx, pinhole.cy};
	const CameraModel model = *CameraModel::create(p).model;

	// The camera at the identity, so the board's pose in the scene is its pose in the camera; three
	// poses of the sweep, one tilted well off square. Both renderers average the same 3x3 rays per
	// pixel and meet the same plane, so they can differ only by rounding. Measured: identical, every
	// pixel at all three poses; the bound allows one grey level for another compiler's rounding.
	std::vector<math::RigidTransformd> poses;
	for (const std::size_t i : {0, 5, 11})
		poses.push_back(synthetic::sweepPose(spec, i, 16));
	synthetic::Scene scene;
	scene.board = synthetic::boardSurface(rendering, spec.instance().squareLength.value.metres(), poses);

	for (std::size_t f = 0; f < poses.size(); ++f)
	{
		CAPTURE(f);
		const image::Image expected = synthetic::view(*rendering, spec, pinhole, poses[f]);
		const image::Image found = synthetic::render(scene, model, math::RigidTransformd{}, f, 3);
		REQUIRE(found.width() == expected.width());
		REQUIRE(found.height() == expected.height());
		int worst = 0;
		std::size_t board = 0;
		for (std::size_t i = 0; i < std::size_t(found.width()) * std::size_t(found.height()); ++i)
		{
			worst = std::max(worst, std::abs(int(found.data()[i]) - int(expected.data()[i])));
			board += expected.data()[i] != 128 ? 1 : 0;
		}
		CHECK(board > 10000); // the board is in view, not just the background
		CHECK(worst <= 1);
	}

	// Past its last pose the board is gone, and the scene is its background.
	const image::Image empty = synthetic::render(scene, model, math::RigidTransformd{}, poses.size(), 1);
	CHECK(std::all_of(empty.data(), empty.data() + std::size_t(empty.width()) * std::size_t(empty.height()),
					  [](std::uint8_t v)
					  { return v == 128; }));
}
