// The editor's document (M15 slice 8d): a plan opened and saved goes through ADR-0027 at the file, and
// an undo snapshot never does, so an undo restores the plan exactly and the selection by identity.

#include "document.h"
#include "editorplan.h"
#include "edits.h"
#include "selection.h"

#include <lain/core/uri.h>
#include <lain/io/data/codecs.h>
#include <lain/rigplan/serialize/rigplan.h>
#include <lain/testing/scratch.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;
namespace rp = lain::rigplan;

static const fs::path kExamples{LAIN_RIGPLAN_EXAMPLES_DIR};

static std::string bytesOf(const fs::path& file)
{
	std::ifstream in(file, std::ios::binary);
	return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

TEST_CASE("a plan opened and saved unchanged is the bytes it was read from", "[rigplan][document]")
{
	// So the editor's Save writes what the example builder writes: the one reader, the one writer and
	// ADR-0027 applied the same way at both ends.
	lain::io::data::registerDataCodecs();
	rigplan::DocumentRead opened = rigplan::openDocument(kExamples / "three-poles.json");
	INFO((opened.problems.empty() ? std::string() : opened.problems.front()));
	REQUIRE(opened.document.has_value());
	rigplan::Document& document = *opened.document;
	CHECK_FALSE(document.dirty);
	CHECK(document.path == fs::absolute(kExamples / "three-poles.json"));

	const fs::path folder = lain::testing::scratchDir() / "saved-three-poles";
	fs::create_directories(folder);
	document.dirty = true;
	REQUIRE(rigplan::saveDocument(document, folder / "three-poles.json").empty());
	CHECK(bytesOf(folder / "three-poles.json") == bytesOf(kExamples / "three-poles.json"));
	CHECK_FALSE(document.dirty);
	CHECK(document.path == folder / "three-poles.json");
}

TEST_CASE("a plan that will not read says why, naming the file", "[rigplan][document]")
{
	lain::io::data::registerDataCodecs();
	const fs::path missing = lain::testing::scratchDir() / "no-such-plan.json";
	const rigplan::DocumentRead opened = rigplan::openDocument(missing);
	CHECK_FALSE(opened.document.has_value());
	REQUIRE(opened.problems.size() == 1);
	CHECK(opened.problems.front().find(fs::absolute(missing).string()) != std::string::npos);
}

TEST_CASE("an undo restores the plan exactly, and keeps the selection by identity", "[rigplan][document]")
{
	// The plan names a prop's mesh inside the document's own folder, so a snapshot taken through the
	// document as it is written to a file (its paths relative) would come back with a relative mesh: an
	// undo meets no file.
	lain::io::data::registerDataCodecs();
	const fs::path folder = lain::testing::scratchDir() / "undo-plan";
	fs::create_directories(folder);
	rigplan::Document document = rigplan::newDocument();
	document.plan = editorplan::plan(lain::core::Uri::fromPath(folder / "props" / "chair.obj"));
	document.history.reset(rigplan::snapshot(document.plan));
	REQUIRE(rigplan::saveDocument(document, folder / "plan.json").empty());
	const lain::data::Value original = rigplan::snapshot(document.plan);

	// Moving a camera is a step; the selection names it, not its place in a list.
	const rigplan::Selection cam2 = rigplan::Selection::object(rigplan::Selected::Camera, editorplan::cam2Id);
	document.plan.cameras[1].offset.z += 0.3;
	document.markChanged();
	rigplan::commit(document);
	CHECK(document.dirty);

	// Deleting the camera before it reorders the list, so an index would now name another camera.
	REQUIRE(rigplan::remove(document.plan, rigplan::Selection::object(rigplan::Selected::Camera, editorplan::cam1Id)).changed());
	document.markChanged();
	rigplan::commit(document);
	CHECK(rigplan::exists(document.plan, cam2));

	// The first step back restores a snapshot commit took, not the baseline: the camera moved, cam1 back,
	// and the prop's mesh as it is held, absolute.
	const lain::core::Uri chair = lain::core::Uri::fromPath(folder / "props" / "chair.obj");
	REQUIRE(rigplan::undo(document));
	CHECK(document.plan.cameras.size() == 2);
	CHECK(document.plan.props.front().mesh == chair);
	REQUIRE(rigplan::undo(document));
	CHECK(rigplan::snapshot(document.plan) == original);
	CHECK(rigplan::exists(document.plan, cam2));
	CHECK(document.plan.cameras[1].id == editorplan::cam2Id);
	CHECK_FALSE(rigplan::undo(document)); // nothing before the plan was opened

	// A selection whose object the step removes is recognised as gone, and comes back with a redo.
	rigplan::EditResult added = rigplan::addCamera(document.plan, rigplan::Selection::object(rigplan::Selected::Mount, editorplan::poleId));
	REQUIRE(added.changed());
	const rigplan::Selection fresh = *added.selection;
	document.markChanged();
	rigplan::commit(document);
	REQUIRE(rigplan::undo(document));
	CHECK_FALSE(rigplan::exists(document.plan, fresh));
	REQUIRE(rigplan::redo(document));
	CHECK(rigplan::exists(document.plan, fresh));
}

TEST_CASE("a new plan can take a camera at once, and checks clean", "[rigplan][document]")
{
	rigplan::Document document = rigplan::newDocument();
	CHECK(document.path.empty());
	CHECK_FALSE(document.dirty);
	REQUIRE(document.plan.room.has_value());
	CHECK(document.plan.mounts.empty());
	CHECK(rp::check(document.plan).empty());

	const rigplan::EditResult pole = rigplan::addPole(document.plan);
	REQUIRE(pole.changed());
	const rigplan::EditResult camera = rigplan::addCamera(document.plan, *pole.selection);
	INFO(camera.refusal);
	REQUIRE(camera.changed());
	REQUIRE(rigplan::addLight(document.plan, *pole.selection).changed());
	const std::vector<rp::PlanDiagnostic> problems = rp::check(document.plan);
	INFO((problems.empty() ? std::string() : problems.front().detail));
	CHECK(problems.empty());
}
