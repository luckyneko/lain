// The gizmo bridge (M15 slice 8d): ImGuizmo's matrices put a point where lain's projection does, and a
// manipulated matrix is written back within what each kind can hold, the rest ignored.

#include "editorplan.h"
#include "gizmo.h"
#include "orbit.h"
#include "selection.h"

#include <lain/core/uri.h>
#include <lain/gui/gizmo.h>
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

TEST_CASE("the gizmo offers each kind what it can hold, and nothing for a mover", "[rigplan][gizmo]")
{
	const rp::RigPlan p = plan();
	const rp::NominalLayout layout = rp::nominalLayout(p);
	const auto target = [&](const Selection& s)
	{ return rigplan::gizmoTarget(p, layout, s); };
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

	CHECK_FALSE(target(Selection::object(Selected::Board, editorplan::boardId)));
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
		Selection::room(),
	};
	for (const Selection& selection : selections)
	{
		for (const GizmoMode mode : {GizmoMode::Translate, GizmoMode::Rotate})
		{
			rp::RigPlan p = plan();
			const rp::NominalLayout layout = rp::nominalLayout(p);
			const std::optional<rigplan::GizmoTarget> before = rigplan::gizmoTarget(p, layout, selection);
			REQUIRE(before);
			// Through ImGuizmo's floats, as a drag hands it back.
			math::Mat4d floated{1.0};
			for (int c = 0; c < 4; ++c)
			{
				for (int r = 0; r < 4; ++r)
					floated[c][r] = double(float(before->planFromTarget[c][r]));
			}
			rigplan::apply(p, layout, selection, mode, floated);
			const std::optional<rigplan::GizmoTarget> after = rigplan::gizmoTarget(p, rp::nominalLayout(p), selection);
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
	const math::Mat4d start = rigplan::gizmoTarget(p, layout, cam1)->planFromTarget;

	// Along the plan's +X: on a pole turned 30 degrees, that is not the mount's X.
	math::Mat4d moved = start;
	moved[3] += math::Vec4d{0.2, 0.0, -0.1, 0.0};
	REQUIRE(rigplan::apply(p, layout, cam1, GizmoMode::Translate, moved));
	requireNear(rigplan::gizmoTarget(p, rp::nominalLayout(p), cam1)->planFromTarget, moved, 1e-12);
	CHECK(p.cameras[0].offset.x == Approx(0.2 * std::cos(math::radians(30.0))));
	CHECK(p.cameras[0].aim.panDegrees == 10.0); // a move leaves the aim exactly as it was

	// Turned 25 degrees about the plan's +Z: the mount's yaw is undone, so the aim's pan takes it.
	const math::Mat4d turned = math::rotate(math::Mat4d(1.0), math::radians(25.0), math::Vec3d{0.0, 0.0, 1.0}) * start;
	rp::RigPlan q = plan();
	REQUIRE(rigplan::apply(q, layout, cam1, GizmoMode::Rotate, turned));
	const math::Mat4d now = rigplan::gizmoTarget(q, rp::nominalLayout(q), cam1)->planFromTarget;
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
	const math::Mat4d start = rigplan::gizmoTarget(p, layout, pole)->planFromTarget;

	math::Mat4d lifted = start;
	lifted[3] += math::Vec4d{0.5, -0.25, 0.75, 0.0};
	REQUIRE(rigplan::apply(p, layout, pole, GizmoMode::Translate, lifted));
	const math::Mat4d placed = rigplan::gizmoTarget(p, rp::nominalLayout(p), pole)->planFromTarget;
	CHECK(placed[3].x == Approx(start[3].x + 0.5));
	CHECK(placed[3].y == Approx(start[3].y - 0.25));
	CHECK(placed[3].z == 0.0);
	CHECK(std::get<rp::Pole>(p.mounts[0].kind).floor == math::Vec2d{-1.5, -0.25});

	// Tilted and yawed: a box keeps the yaw alone, and stays where it is.
	const math::Mat4d tilt = math::rotate(math::Mat4d(1.0), math::radians(20.0), math::Vec3d{1.0, 0.0, 0.0});
	const math::Mat4d yaw = math::rotate(math::Mat4d(1.0), math::radians(15.0), math::Vec3d{0.0, 0.0, 1.0});
	const Selection box = Selection::object(Selected::Box, editorplan::boxId);
	const math::Mat4d boxStart = rigplan::gizmoTarget(p, rp::nominalLayout(p), box)->planFromTarget;
	const math::Mat4d tilted = yaw * boxStart * tilt; // yawed 15 more, then tilted about its own +X
	REQUIRE(rigplan::apply(p, rp::nominalLayout(p), box, GizmoMode::Rotate, tilted));
	CHECK(p.boxes[0].centre == math::Vec3d{1.2, -1.0, 0.4});
	// The yaw is read off the turned +X across the floor, which a tilt about its own +X leaves alone.
	CHECK(p.boxes[0].yawDegrees == Approx(30.0));

	// A move in Rotate mode, and a turn in Translate mode, are not that mode's to write.
	const math::Mat4d boxNow = rigplan::gizmoTarget(p, rp::nominalLayout(p), box)->planFromTarget;
	const double boxYaw = p.boxes[0].yawDegrees;
	math::Mat4d shifted = boxNow;
	shifted[3] += math::Vec4d{1.0, 1.0, 1.0, 0.0};
	rigplan::apply(p, rp::nominalLayout(p), box, GizmoMode::Rotate, shifted);
	CHECK(p.boxes[0].centre == math::Vec3d{1.2, -1.0, 0.4});
	CHECK(p.boxes[0].yawDegrees == Approx(boxYaw));
	const math::Vec3d at{boxNow[3]};
	const math::Mat4d spun = math::translate(math::Mat4d(1.0), at) * yaw * math::translate(math::Mat4d(1.0), -at) * boxNow;
	CHECK_FALSE(rigplan::apply(p, rp::nominalLayout(p), box, GizmoMode::Translate, spun));
	CHECK(p.boxes[0].yawDegrees == Approx(boxYaw));

	// A room moves across the floor only, and does not turn.
	const Selection room = Selection::room();
	math::Mat4d raised = rigplan::gizmoTarget(p, rp::nominalLayout(p), room)->planFromTarget;
	raised[3] += math::Vec4d{0.5, 0.5, 2.0, 0.0};
	REQUIRE(rigplan::apply(p, rp::nominalLayout(p), room, GizmoMode::Translate, raised));
	CHECK(p.room->centre == math::Vec2d{0.5, 0.5});
	CHECK_FALSE(rigplan::apply(p, rp::nominalLayout(p), room, GizmoMode::Rotate, yaw));
}
