// The editor's edits (M15 slice 8d): each changes the plan whole, or refuses and changes nothing, by
// check()'s own rules.

#include "document.h"
#include "editorplan.h"
#include "edits.h"
#include "selection.h"

#include <lain/core/uri.h>
#include <lain/rigplan/realisation.h>
#include <lain/rigplan/serialize/rigplan.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

namespace rp = lain::rigplan;
using rigplan::Selected;
using rigplan::Selection;
using rigplan::SpecificationKind;

static rp::RigPlan plan()
{
	return editorplan::plan(lain::core::Uri{"/assets/chair.obj"});
}

static std::string firstProblem(const rp::RigPlan& p)
{
	const std::vector<rp::PlanDiagnostic> problems = rp::check(p);
	return problems.empty() ? std::string() : problems.front().detail;
}

static const rp::PlacedCamera* camera(const rp::RigPlan& p, const lain::core::Uuid& id)
{
	const auto found = std::find_if(p.cameras.begin(), p.cameras.end(), [&](const rp::PlacedCamera& c)
									{ return c.id == id; });
	return found != p.cameras.end() ? &*found : nullptr;
}

TEST_CASE("the plan the edit tests start from checks clean", "[rigplan][edits]")
{
	CHECK(firstProblem(plan()).empty());
}

TEST_CASE("deleting a mount takes its cameras and lights with it, as one step", "[rigplan][edits]")
{
	rigplan::Document document = rigplan::newDocument();
	document.plan = plan();
	document.plan.reference = editorplan::cam1Id;
	document.history.reset(rigplan::snapshot(document.plan));
	const lain::data::Value before = rigplan::snapshot(document.plan);

	const rigplan::EditResult removed = rigplan::remove(document.plan, Selection::object(Selected::Mount, editorplan::poleId));
	REQUIRE(removed.changed());
	CHECK(*removed.selection == Selection::nothing());
	CHECK(document.plan.mounts.size() == 1);
	CHECK(document.plan.cameras.empty());			  // both were on the pole
	CHECK(document.plan.lights.size() == 1);		  // the spot is on the bracket
	CHECK_FALSE(document.plan.reference.has_value()); // its reference camera went with it
	CHECK(firstProblem(document.plan).empty());

	document.markChanged();
	rigplan::commit(document);
	REQUIRE(rigplan::undo(document));
	CHECK(rigplan::snapshot(document.plan) == before);

	// The bracket takes its light.
	REQUIRE(rigplan::remove(document.plan, Selection::object(Selected::Mount, editorplan::bracketId)).changed());
	CHECK(document.plan.lights.empty());
	CHECK(document.plan.cameras.size() == 2);
}

TEST_CASE("a specification is not deleted while anything names it", "[rigplan][edits]")
{
	rp::RigPlan p = plan();
	const lain::data::Value before = rigplan::snapshot(p);
	const rigplan::EditResult lens = rigplan::remove(p, Selection::specificationOf(SpecificationKind::Lens, "4mm"));
	CHECK_FALSE(lens.changed());
	CHECK(lens.refusal.find("hd-4mm") != std::string::npos);
	const rigplan::EditResult spec = rigplan::remove(p, Selection::specificationOf(SpecificationKind::Camera, "hd-4mm"));
	CHECK_FALSE(spec.changed());
	CHECK(spec.refusal.find("cam1") != std::string::npos);
	CHECK(rigplan::snapshot(p) == before);

	// With nothing naming them, each goes.
	REQUIRE(rigplan::remove(p, Selection::object(Selected::Mount, editorplan::poleId)).changed());
	REQUIRE(rigplan::remove(p, Selection::specificationOf(SpecificationKind::Camera, "hd-4mm")).changed());
	REQUIRE(rigplan::remove(p, Selection::specificationOf(SpecificationKind::Lens, "4mm")).changed());
	CHECK(p.specifications.lenses.empty());
}

TEST_CASE("a duplicate is a new unit: a new identity, a new name, its own draws", "[rigplan][edits]")
{
	rp::RigPlan p = plan();
	p.scales.intrinsic = 1.0; // so a unit's draws show
	const rigplan::EditResult copy = rigplan::duplicate(p, Selection::object(Selected::Camera, editorplan::cam1Id));
	REQUIRE(copy.changed());
	const rp::PlacedCamera* original = camera(p, editorplan::cam1Id);
	const rp::PlacedCamera* duplicate = camera(p, copy.selection->id);
	REQUIRE(original);
	REQUIRE(duplicate);
	CHECK(duplicate->id != original->id);
	CHECK(duplicate->name == "cam1 2");
	CHECK(duplicate->mount == original->mount);
	CHECK(duplicate->offset == original->offset);

	// ADR-0029: its draws are keyed by its identity, so the same specification on the same mount draws
	// another unit.
	INFO(firstProblem(p));
	const rp::RealisationResult realised = rp::realise(p);
	REQUIRE(realised.realisation.has_value());
	const rp::RealisedCamera* drawnOriginal = nullptr;
	const rp::RealisedCamera* drawnCopy = nullptr;
	for (const rp::RealisedCamera& c : realised.realisation->cameras)
	{
		if (c.id == original->id)
			drawnOriginal = &c;
		if (c.id == duplicate->id)
			drawnCopy = &c;
	}
	REQUIRE(drawnOriginal);
	REQUIRE(drawnCopy);
	CHECK(drawnOriginal->model.intrinsics().fx != drawnCopy->model.intrinsics().fx);

	// A trailing number is replaced, not stacked.
	const rigplan::EditResult again = rigplan::duplicate(p, *copy.selection);
	REQUIRE(again.changed());
	CHECK(camera(p, again.selection->id)->name == "cam1 3");
}

TEST_CASE("a mount is duplicated with what it carries, each a new unit", "[rigplan][edits]")
{
	rp::RigPlan p = plan();
	const rigplan::EditResult copy = rigplan::duplicate(p, Selection::object(Selected::Mount, editorplan::poleId));
	REQUIRE(copy.changed());
	CHECK(p.mounts.size() == 3);
	CHECK(p.mounts.back().name == "pole 2");
	REQUIRE(p.cameras.size() == 4);
	CHECK(p.cameras[2].mount == copy.selection->id);
	CHECK(p.cameras[3].mount == copy.selection->id);
	CHECK(p.cameras[2].name == "cam1 2");
	CHECK(p.cameras[3].name == "cam2 2");
	CHECK(firstProblem(p).empty());
}

TEST_CASE("a key is duplicated one frame on, where that frame is free", "[rigplan][edits]")
{
	rp::RigPlan p = plan();
	const rigplan::EditResult copy = rigplan::duplicate(p, Selection::keyOf(editorplan::boardId, 0));
	REQUIRE(copy.changed());
	CHECK(*copy.selection == Selection::keyOf(editorplan::boardId, 1));
	REQUIRE(p.boards.front().path.keys.size() == 3);
	CHECK(p.boards.front().path.keys[1].frame == 1);
	CHECK(firstProblem(p).empty());

	// Frame 0's key again would land on frame 1, which has a key now.
	const rigplan::EditResult clash = rigplan::duplicate(p, Selection::keyOf(editorplan::boardId, 0));
	CHECK_FALSE(clash.changed());
	CHECK_FALSE(clash.refusal.empty());

	// A mover keeps its last key.
	rp::RigPlan single = plan();
	REQUIRE(rigplan::remove(single, Selection::keyOf(editorplan::boardId, 1)).changed());
	const rigplan::EditResult last = rigplan::remove(single, Selection::keyOf(editorplan::boardId, 0));
	CHECK_FALSE(last.changed());
	CHECK(single.boards.front().path.keys.size() == 1);
}

TEST_CASE("a refused rename changes nothing", "[rigplan][edits]")
{
	rp::RigPlan p = plan();
	const lain::data::Value before = rigplan::snapshot(p);
	const Selection cam1 = Selection::object(Selected::Camera, editorplan::cam1Id);

	const rigplan::EditResult taken = rigplan::rename(p, cam1, "CAM2"); // another camera's, ignoring case
	CHECK_FALSE(taken.changed());
	CHECK(taken.refusal.find("cam2") != std::string::npos);
	const rigplan::EditResult folder = rigplan::rename(p, cam1, "a/b"); // no folder can have it
	CHECK_FALSE(folder.changed());
	CHECK_FALSE(folder.refusal.empty());
	CHECK_FALSE(rigplan::rename(p, cam1, "").changed());
	CHECK_FALSE(rigplan::rename(p, Selection::specificationOf(SpecificationKind::Light, "flood"), "").changed());
	CHECK(rigplan::snapshot(p) == before);

	// Its own name is nothing to do, and no refusal.
	const rigplan::EditResult same = rigplan::rename(p, cam1, "cam1");
	CHECK_FALSE(same.changed());
	CHECK(same.refusal.empty());

	const rigplan::EditResult fine = rigplan::rename(p, cam1, "left");
	REQUIRE(fine.changed());
	CHECK(camera(p, editorplan::cam1Id)->name == "left");
}

TEST_CASE("a specification renamed is renamed everywhere it is named", "[rigplan][edits]")
{
	rp::RigPlan p = plan();
	REQUIRE(rigplan::rename(p, Selection::specificationOf(SpecificationKind::Sensor, "hd"), "wide sensor").changed());
	REQUIRE(rigplan::rename(p, Selection::specificationOf(SpecificationKind::Lens, "4mm"), "fisheye").changed());
	const rigplan::EditResult camera = rigplan::rename(p, Selection::specificationOf(SpecificationKind::Camera, "hd-4mm"), "rig camera");
	REQUIRE(camera.changed());
	CHECK(*camera.selection == Selection::specificationOf(SpecificationKind::Camera, "rig camera"));
	REQUIRE(rigplan::rename(p, Selection::specificationOf(SpecificationKind::Light, "flood"), "key light").changed());

	CHECK(p.specifications.cameras.front().sensor == "wide sensor");
	CHECK(p.specifications.cameras.front().lens == "fisheye");
	for (const rp::PlacedCamera& c : p.cameras)
		CHECK(c.camera == "rig camera");
	CHECK(p.lights.front().light == "key light");
	CHECK(firstProblem(p).empty());

	// Onto another of its kind's name is refused.
	REQUIRE(rigplan::duplicate(p, Selection::specificationOf(SpecificationKind::Lens, "fisheye")).changed());
	const lain::data::Value before = rigplan::snapshot(p);
	CHECK_FALSE(rigplan::rename(p, Selection::specificationOf(SpecificationKind::Lens, "fisheye 2"), "fisheye").changed());
	CHECK(rigplan::snapshot(p) == before);
}

TEST_CASE("the reference is a camera of the plan, or none", "[rigplan][edits]")
{
	rp::RigPlan p = plan();
	REQUIRE(rigplan::setReference(p, Selection::object(Selected::Camera, editorplan::cam2Id)).changed());
	CHECK(p.reference == editorplan::cam2Id);
	CHECK_FALSE(rigplan::setReference(p, Selection::object(Selected::Light, editorplan::lightId)).changed());
	CHECK(p.reference == editorplan::cam2Id);
	REQUIRE(rigplan::setReference(p, Selection::nothing()).changed());
	CHECK_FALSE(p.reference.has_value());
	CHECK(firstProblem(p).empty());
}

TEST_CASE("everything added stands clear and checks clean", "[rigplan][edits]")
{
	rp::RigPlan p = plan();
	REQUIRE(rigplan::addPole(p).changed());
	REQUIRE(rigplan::addFixedPoint(p).changed());
	REQUIRE(rigplan::addCamera(p, Selection::object(Selected::Mount, p.mounts.back().id)).changed());
	REQUIRE(rigplan::addCamera(p, Selection::object(Selected::Camera, editorplan::cam1Id)).changed()); // on cam1's pole
	REQUIRE(rigplan::addLight(p, Selection::object(Selected::Mount, p.mounts[2].id)).changed());
	REQUIRE(rigplan::addPlane(p).changed());
	REQUIRE(rigplan::addBox(p).changed());
	REQUIRE(rigplan::addProp(p, lain::core::Uri{"/assets/table.obj"}).changed());
	REQUIRE(rigplan::addBoard(p).changed());
	CHECK(rigplan::addRoom(p).refusal == "the plan has a room already");
	CHECK(p.cameras[2].name == "cam3");
	CHECK(p.cameras[3].name == "cam4");
	CHECK(p.boards.back().name == "board 2");
	CHECK(p.boards.back().instance.identity == "board 2");
	CHECK(firstProblem(p).empty());

	// With no mount to go on, a camera is refused.
	CHECK_FALSE(rigplan::addCamera(p, Selection::object(Selected::Box, editorplan::boxId)).changed());
}
