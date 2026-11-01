// The current frame (M15 slice 9a, CONTEXT.md "Current frame"): which capture frame the editor shows,
// and what selecting, scrubbing, an edit and an undo do to it.

#include "appcontext.h"
#include "editorplan.h"
#include "selection.h"

#include <lain/core/uri.h>

#include <catch2/catch_test_macros.hpp>

using rigplan::AppContext;
using rigplan::Selected;
using rigplan::Selection;

static AppContext context()
{
	AppContext ctx;
	ctx.document.plan = editorplan::plan(lain::core::Uri{"/assets/chair.obj"});
	return ctx;
}

TEST_CASE("while a key is selected the frame shown is the key's", "[rigplan][frame]")
{
	AppContext ctx = context();
	// The board is keyed at frames 0 and 10, on a timeline of 12.
	ctx.frame = 3;
	ctx.selection = Selection::keyOf(editorplan::boardId, 1);
	ctx.followFrame();
	CHECK(ctx.frame == 10);

	SECTION("and follows the key when its frame is moved")
	{
		ctx.document.plan.boards.front().path.keys[1].frame = 7;
		ctx.followFrame();
		CHECK(ctx.frame == 7);
	}
	SECTION("wherever it stands, past the timeline included")
	{
		ctx.document.plan.boards.front().path.keys[1].frame = 30;
		ctx.followFrame();
		CHECK(ctx.frame == 30);
	}
	SECTION("and stays where the key was once the key is let go of")
	{
		ctx.selection = Selection::object(Selected::Board, editorplan::boardId);
		ctx.followFrame();
		CHECK(ctx.frame == 10);
	}
}

TEST_CASE("going to another frame lets a selected key go to its mover", "[rigplan][frame]")
{
	AppContext ctx = context();
	ctx.selection = Selection::keyOf(editorplan::boardId, 1);
	ctx.followFrame();

	SECTION("another frame")
	{
		ctx.goToFrame(4);
		CHECK(ctx.frame == 4);
		CHECK(ctx.selection == Selection::object(Selected::Board, editorplan::boardId));
	}
	SECTION("the key's own frame keeps the key")
	{
		ctx.goToFrame(10);
		CHECK(ctx.frame == 10);
		CHECK(ctx.selection == Selection::keyOf(editorplan::boardId, 1));
	}
	SECTION("anything else selected stays selected")
	{
		ctx.selection = Selection::object(Selected::Camera, editorplan::cam1Id);
		ctx.goToFrame(6);
		CHECK(ctx.frame == 6);
		CHECK(ctx.selection == Selection::object(Selected::Camera, editorplan::cam1Id));
	}
	SECTION("a frame past the timeline is its last")
	{
		ctx.selection = Selection::nothing();
		ctx.goToFrame(40);
		CHECK(ctx.frame == 11);
	}
}

TEST_CASE("a timeline shortened under the editor brings the frame back within it", "[rigplan][frame]")
{
	AppContext ctx = context();
	ctx.goToFrame(11);
	REQUIRE(ctx.frame == 11);

	ctx.document.plan.timeline.frames = 5; // an edit, or an undo to a plan with a shorter timeline
	ctx.followFrame();
	CHECK(ctx.frame == 4);

	ctx.document.plan.timeline.frames = 0; // a timeline check() refuses still has a frame to show
	ctx.followFrame();
	CHECK(ctx.frame == 0);
}
