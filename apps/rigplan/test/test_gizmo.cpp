// The gizmo bridge (M15 slice 8d): ImGuizmo's matrices put a point where lain's projection does, and a
// manipulated matrix is written back within what each kind can hold, the rest ignored. And a mover's
// gizmo is its key at the frame shown, made there by a drag (9b).

#include "editorplan.h"
#include "gizmo.h"
#include "orbit.h"
#include "selection.h"

#include <lain/core/uri.h>
#include <lain/gui/gizmo.h>
#include <lain/rigplan/mover.h>
#include <lain/rigplan/realisation.h>
#include <lain/rigplan/serialize/rigplan.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <variant>

namespace math = lain::math;
namespace rp = lain::rigplan;
using Catch::Approx;
using rigplan::GizmoMode;
using rigplan::Selected;
using rigplan::Selection;

static rp::RigPlan plan()
{
	return editorplan::plan(lain::core::Uri{"/assets/chair.obj"});
}

static void requireNear(const math::Mat4d& a, const math::Mat4d& b, double tolerance)
{
	for (int c = 0; c < 4; ++c)
	{
		for (int r = 0; r < 4; ++r)
		{
			INFO("column " << c << " row " << r);
			CHECK(a[c][r] == Approx(b[c][r]).margin(tolerance));
		}
	}
}

TEST_CASE("a point under ImGuizmo's matrices lands on the pixel lain's projection gives", "[rigplan][gizmo]")
{
	// ImGuizmo's own mouse ray, through where lain projects a point, passes through the point: on a pane of
	// a different size in points than the view in pixels (a Retina display), and not at the window's
	// origin, as a pane is not.
	for (const lain::camera::ImageGeometry image : {lain::camera::ImageGeometry{1920, 1080}, lain::camera::ImageGeometry{700, 900}})
	{
		rigplan::Orbit orbit;
		orbit.target = {0.3, -0.4, 1.1};
		orbit.yawDegrees = -35;
		orbit.pitchDegrees = 28;
		const lain::render::View view = rigplan::viewOf(orbit, image);
		const rigplan::GizmoMatrices matrices = rigplan::gizmoMatrices(view);
		const ImVec2 rectPosition{37.0f, 52.0f};
		const ImVec2 rectSize{float(image.width) / 2.0f, float(image.height) / 2.0f};
		for (const math::Vec3d point : {math::Vec3d{0.3, -0.4, 1.1}, math::Vec3d{2.0, 1.0, 0.0}, math::Vec3d{-1.5, 2.5, 2.4}, math::Vec3d{1.0, -2.0, 0.5}})
		{
			const math::Vec3d c = view.worldFromCamera.inverse().apply(point);
			const double u = view.intrinsics.fx * c.x / c.z + view.intrinsics.cx;
			const double v = view.intrinsics.fy * c.y / c.z + view.intrinsics.cy;
			const ImVec2 mouse{rectPosition.x + float((u + 0.5) / double(image.width)) * rectSize.x,
							   rectPosition.y + float((v + 0.5) / double(image.height)) * rectSize.y};
			float origin[3];
			float direction[3];
			lain::gui::gizmo::ComputeMouseRay(matrices.view.data(), matrices.projection.data(), mouse, rectPosition, rectSize, origin, direction);
			const math::Vec3d o{origin[0], origin[1], origin[2]};
			const math::Vec3d d = math::normalize(math::Vec3d{direction[0], direction[1], direction[2]});
			const math::Vec3d toPoint = point - o;
			INFO("image " << image.width << " x " << image.height << ", point " << point.x << ", " << point.y << ", " << point.z);
			CHECK(math::dot(toPoint, d) > 0.0);
			// Float arithmetic through ImGuizmo: a few hundredths of a millimetre at metres.
			CHECK(math::length(toPoint - d * math::dot(toPoint, d)) < 1e-4 * math::length(toPoint));
		}
	}
}

TEST_CASE("the gizmo offers each kind what it can hold, and a mover its key at the frame", "[rigplan][gizmo]")
{
	const rp::RigPlan p = plan();
	const rp::NominalLayout layout = rp::nominalLayout(p);
	const auto target = [&](const Selection& s)
	{ return rigplan::gizmoTarget(p, layout, s, 0); };
	namespace gz = lain::gui::gizmo;

	const auto pole = target(Selection::object(Selected::Mount, editorplan::poleId));
	REQUIRE(pole);
	CHECK(rigplan::operation(*pole, GizmoMode::Translate) == (gz::TRANSLATE_X | gz::TRANSLATE_Y));
	CHECK(rigplan::operation(*pole, GizmoMode::Rotate) == gz::ROTATE_Z);
	const auto camera = target(Selection::object(Selected::Camera, editorplan::cam1Id));
	REQUIRE(camera);
	CHECK(rigplan::operation(*camera, GizmoMode::Translate) == gz::TRANSLATE);
	CHECK(rigplan::operation(*camera, GizmoMode::Rotate) == (gz::ROTATE_X | gz::ROTATE_Y | gz::ROTATE_Z));
	const auto room = target(Selection::room());
	REQUIRE(room);
	CHECK(rigplan::operation(*room, GizmoMode::Rotate) == 0);

	const auto board = target(Selection::object(Selected::Board, editorplan::boardId));
	REQUIRE(board);
	CHECK(rigplan::operation(*board, GizmoMode::Translate) == gz::TRANSLATE);
	CHECK(rigplan::operation(*board, GizmoMode::Rotate) == (gz::ROTATE_X | gz::ROTATE_Y | gz::ROTATE_Z));
	// At a keyed frame, the mover's gizmo is that key's.
	requireNear(board->planFromTarget, target(Selection::keyOf(editorplan::boardId, 0))->planFromTarget, 0.0);
	CHECK_FALSE(target(Selection::plan()));
	CHECK_FALSE(target(Selection::specificationOf(rigplan::SpecificationKind::Lens, "4mm")));
	CHECK(target(Selection::keyOf(editorplan::boardId, 1)));

	// A camera's gizmo stands at its mounting point, which its optical centre is at for this sensor.
	CHECK(math::length(math::Vec3d(camera->planFromTarget[3]) - layout.cameras[0].planFromCamera->translation()) < 1e-12);
}

TEST_CASE("the gizmo's own matrix written back changes nothing, for every kind", "[rigplan][gizmo]")
{
	const std::vector<Selection> selections{
		Selection::object(Selected::Mount, editorplan::poleId),
		Selection::object(Selected::Mount, editorplan::bracketId),
		Selection::object(Selected::Camera, editorplan::cam1Id),
		Selection::object(Selected::Camera, editorplan::cam2Id),
		Selection::object(Selected::Light, editorplan::lightId),
		Selection::object(Selected::Plane, editorplan::planeId),
		Selection::object(Selected::Box, editorplan::boxId),
		Selection::object(Selected::Prop, editorplan::propId),
		Selection::keyOf(editorplan::boardId, 0),
		Selection::keyOf(editorplan::boardId, 1),
		Selection::object(Selected::Board, editorplan::boardId), // at frame 0, which is keyed
		Selection::room(),
	};
	for (const Selection& selection : selections)
	{
		for (const GizmoMode mode : {GizmoMode::Translate, GizmoMode::Rotate})
		{
			rp::RigPlan p = plan();
			const rp::NominalLayout layout = rp::nominalLayout(p);
			const std::optional<rigplan::GizmoTarget> before = rigplan::gizmoTarget(p, layout, selection, 0);
			REQUIRE(before);
			// Through ImGuizmo's floats, as a drag hands it back.
			math::Mat4d floated{1.0};
			for (int c = 0; c < 4; ++c)
			{
				for (int r = 0; r < 4; ++r)
					floated[c][r] = double(float(before->planFromTarget[c][r]));
			}
			rigplan::apply(p, layout, selection, 0, mode, floated);
			const std::optional<rigplan::GizmoTarget> after = rigplan::gizmoTarget(p, rp::nominalLayout(p), selection, 0);
			REQUIRE(after);
			INFO("selection kind " << int(selection.kind) << ", mode " << int(mode));
			requireNear(after->planFromTarget, before->planFromTarget, 1e-6);
			CHECK(rp::check(p).empty());
		}
	}
}

TEST_CASE("a camera moved by the gizmo moves by that much in the plan, on a turned mount", "[rigplan][gizmo]")
{
	rp::RigPlan p = plan();
	const rp::NominalLayout layout = rp::nominalLayout(p);
	const Selection cam1 = Selection::object(Selected::Camera, editorplan::cam1Id);
	const math::Mat4d start = rigplan::gizmoTarget(p, layout, cam1, 0)->planFromTarget;

	// Along the plan's +X: on a pole turned 30 degrees, that is not the mount's X.
	math::Mat4d moved = start;
	moved[3] += math::Vec4d{0.2, 0.0, -0.1, 0.0};
	REQUIRE(rigplan::apply(p, layout, cam1, 0, GizmoMode::Translate, moved));
	requireNear(rigplan::gizmoTarget(p, rp::nominalLayout(p), cam1, 0)->planFromTarget, moved, 1e-12);
	CHECK(p.cameras[0].offset.x == Approx(0.2 * std::cos(math::radians(30.0))));
	CHECK(p.cameras[0].aim.panDegrees == 10.0); // a move leaves the aim exactly as it was

	// Turned 25 degrees about the plan's +Z: the mount's yaw is undone, so the aim's pan takes it.
	const math::Mat4d turned = math::rotate(math::Mat4d(1.0), math::radians(25.0), math::Vec3d{0.0, 0.0, 1.0}) * start;
	rp::RigPlan q = plan();
	REQUIRE(rigplan::apply(q, layout, cam1, 0, GizmoMode::Rotate, turned));
	const math::Mat4d now = rigplan::gizmoTarget(q, rp::nominalLayout(q), cam1, 0)->planFromTarget;
	requireNear(math::Mat4d(math::Mat3d(now)), math::Mat4d(math::Mat3d(turned)), 1e-9);
	CHECK(q.cameras[0].offset == plan().cameras[0].offset); // a turn leaves the offset exactly as it was
	CHECK(q.cameras[0].aim.panDegrees == Approx(35.0));
	CHECK(q.cameras[0].aim.tiltDegrees == Approx(-15.0));
	CHECK(q.cameras[0].aim.rollDegrees == Approx(5.0));
}

TEST_CASE("a pole never leaves the floor, and what a kind cannot hold is ignored", "[rigplan][gizmo]")
{
	rp::RigPlan p = plan();
	const rp::NominalLayout layout = rp::nominalLayout(p);
	const Selection pole = Selection::object(Selected::Mount, editorplan::poleId);
	const math::Mat4d start = rigplan::gizmoTarget(p, layout, pole, 0)->planFromTarget;

	math::Mat4d lifted = start;
	lifted[3] += math::Vec4d{0.5, -0.25, 0.75, 0.0};
	REQUIRE(rigplan::apply(p, layout, pole, 0, GizmoMode::Translate, lifted));
	const math::Mat4d placed = rigplan::gizmoTarget(p, rp::nominalLayout(p), pole, 0)->planFromTarget;
	CHECK(placed[3].x == Approx(start[3].x + 0.5));
	CHECK(placed[3].y == Approx(start[3].y - 0.25));
	CHECK(placed[3].z == 0.0);
	CHECK(std::get<rp::Pole>(p.mounts[0].kind).floor == math::Vec2d{-1.5, -0.25});

	// Tilted and yawed: a box keeps the yaw alone, and stays where it is.
	const math::Mat4d tilt = math::rotate(math::Mat4d(1.0), math::radians(20.0), math::Vec3d{1.0, 0.0, 0.0});
	const math::Mat4d yaw = math::rotate(math::Mat4d(1.0), math::radians(15.0), math::Vec3d{0.0, 0.0, 1.0});
	const Selection box = Selection::object(Selected::Box, editorplan::boxId);
	const math::Mat4d boxStart = rigplan::gizmoTarget(p, rp::nominalLayout(p), box, 0)->planFromTarget;
	const math::Mat4d tilted = yaw * boxStart * tilt; // yawed 15 more, then tilted about its own +X
	REQUIRE(rigplan::apply(p, rp::nominalLayout(p), box, 0, GizmoMode::Rotate, tilted));
	CHECK(p.boxes[0].centre == math::Vec3d{1.2, -1.0, 0.4});
	// The yaw is read off the turned +X across the floor, which a tilt about its own +X leaves alone.
	CHECK(p.boxes[0].yawDegrees == Approx(30.0));

	// A move in Rotate mode, and a turn in Translate mode, are not that mode's to write.
	const math::Mat4d boxNow = rigplan::gizmoTarget(p, rp::nominalLayout(p), box, 0)->planFromTarget;
	const double boxYaw = p.boxes[0].yawDegrees;
	math::Mat4d shifted = boxNow;
	shifted[3] += math::Vec4d{1.0, 1.0, 1.0, 0.0};
	rigplan::apply(p, rp::nominalLayout(p), box, 0, GizmoMode::Rotate, shifted);
	CHECK(p.boxes[0].centre == math::Vec3d{1.2, -1.0, 0.4});
	CHECK(p.boxes[0].yawDegrees == Approx(boxYaw));
	const math::Vec3d at{boxNow[3]};
	const math::Mat4d spun = math::translate(math::Mat4d(1.0), at) * yaw * math::translate(math::Mat4d(1.0), -at) * boxNow;
	CHECK_FALSE(rigplan::apply(p, rp::nominalLayout(p), box, 0, GizmoMode::Translate, spun));
	CHECK(p.boxes[0].yawDegrees == Approx(boxYaw));

	// A room moves across the floor only, and does not turn.
	const Selection room = Selection::room();
	math::Mat4d raised = rigplan::gizmoTarget(p, rp::nominalLayout(p), room, 0)->planFromTarget;
	raised[3] += math::Vec4d{0.5, 0.5, 2.0, 0.0};
	REQUIRE(rigplan::apply(p, rp::nominalLayout(p), room, 0, GizmoMode::Translate, raised));
	CHECK(p.room->centre == math::Vec2d{0.5, 0.5});
	CHECK_FALSE(rigplan::apply(p, rp::nominalLayout(p), room, 0, GizmoMode::Rotate, yaw));
}

static bool sameKey(const rp::PathKey& a, const rp::PathKey& b)
{
	return a.frame == b.frame && a.position == b.position && a.aim.panDegrees == b.aim.panDegrees && a.aim.tiltDegrees == b.aim.tiltDegrees &&
		   a.aim.rollDegrees == b.aim.rollDegrees;
}

TEST_CASE("a mover dragged at a frame without a key is keyed there, and the drag moves that key alone", "[rigplan][gizmo]")
{
	// The board has keys at frames 0 and 10. At frame 4 its gizmo stands where its path puts it, so the
	// handles do not jump when the drag's first change makes the key.
	rp::RigPlan p = plan();
	const rp::NominalLayout layout = rp::nominalLayout(p);
	const Selection board = Selection::object(Selected::Board, editorplan::boardId);
	const rp::Path before = p.boards[0].path;
	const std::size_t frame = 4;
	const std::optional<rigplan::GizmoTarget> start = rigplan::gizmoTarget(p, layout, board, frame);
	REQUIRE(start);
	requireNear(start->planFromTarget, rp::poseAt(before, frame).matrix(), 1e-12);

	// Moved: a key is made at frame 4 with the moved position and the path's own aim there.
	math::Mat4d moved = start->planFromTarget;
	moved[3] += math::Vec4d{0.0, 0.2, 0.1, 0.0};
	REQUIRE(rigplan::apply(p, layout, board, frame, GizmoMode::Translate, moved));
	const rp::Path& after = p.boards[0].path;
	REQUIRE(after.keys.size() == 3);
	CHECK(sameKey(after.keys[0], before.keys[0]));
	CHECK(sameKey(after.keys[2], before.keys[1]));
	CHECK(after.keys[1].frame == frame);
	CHECK(math::length(after.keys[1].position - math::Vec3d(moved[3])) < 1e-12);
	requireNear(math::Mat4d(math::Mat3d(rp::poseAt(after, frame).matrix())), math::Mat4d(math::Mat3d(start->planFromTarget)), 1e-12);
	// From key to key outside the one made, nothing moved: the frame either key holds is as it was.
	for (const std::size_t f : {std::size_t(0), std::size_t(10), std::size_t(11)})
	{
		INFO("frame " << f);
		requireNear(rp::poseAt(after, f).matrix(), rp::poseAt(before, f).matrix(), 1e-12);
	}
	// And the gizmo now stands on the key made, so the next frame of the drag goes on from there.
	requireNear(rigplan::gizmoTarget(p, rp::nominalLayout(p), board, frame)->planFromTarget, moved, 1e-12);

	// At a frame with a key, a drag moves that key and makes none: a turn, which leaves its position.
	const math::Mat4d keyed = rigplan::gizmoTarget(p, rp::nominalLayout(p), board, 10)->planFromTarget;
	const math::Mat4d turned = keyed * math::rotate(math::Mat4d(1.0), math::radians(20.0), math::Vec3d{0.0, 1.0, 0.0});
	REQUIRE(rigplan::apply(p, rp::nominalLayout(p), board, 10, GizmoMode::Rotate, turned));
	REQUIRE(p.boards[0].path.keys.size() == 3);
	CHECK(p.boards[0].path.keys[2].position == before.keys[1].position);
	requireNear(rigplan::gizmoTarget(p, rp::nominalLayout(p), board, 10)->planFromTarget, turned, 1e-9);
}

TEST_CASE("a prop mover is keyed by the gizmo as a board is, and a path that cannot be followed offers none", "[rigplan][gizmo]")
{
	rp::RigPlan p = plan();
	p.props[0].path = rp::Path{{rp::PathKey{2, {-1.0, -1.5, 0.0}, {40.0, 0.0, 0.0}}}};
	const Selection prop = Selection::object(Selected::Prop, editorplan::propId);
	const std::optional<rigplan::GizmoTarget> start = rigplan::gizmoTarget(p, rp::nominalLayout(p), prop, 7);
	REQUIRE(start);
	// Turned, after its one key: the key made at 7 keeps the position the path holds there.
	const math::Mat4d turned = start->planFromTarget * math::rotate(math::Mat4d(1.0), math::radians(30.0), math::Vec3d{0.0, 1.0, 0.0});
	REQUIRE(rigplan::apply(p, rp::nominalLayout(p), prop, 7, GizmoMode::Rotate, turned));
	REQUIRE(p.props[0].path->keys.size() == 2);
	CHECK(p.props[0].path->keys[1].frame == 7);
	CHECK(math::length(p.props[0].path->keys[1].position - math::Vec3d{-1.0, -1.5, 0.0}) < 1e-12);
	requireNear(rigplan::gizmoTarget(p, rp::nominalLayout(p), prop, 7)->planFromTarget, turned, 1e-9);

	// Keys out of frame order: no pose to stand at, and nothing a drag can key.
	p.props[0].path->keys[1].frame = 1;
	CHECK_FALSE(rigplan::gizmoTarget(p, rp::nominalLayout(p), prop, 4));
	const rp::Path unordered = *p.props[0].path;
	CHECK_FALSE(rigplan::apply(p, rp::nominalLayout(p), prop, 4, GizmoMode::Translate, math::Mat4d(1.0)));
	CHECK(p.props[0].path->keys.size() == unordered.keys.size());
}
