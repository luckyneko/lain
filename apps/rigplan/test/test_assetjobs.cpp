// The Viewport's asset jobs (assetjobs.h): what a gesture holds back, and what it owes once it ends.
// With no pool started a job runs as it is posted, so its outcome is there at the next collect.

#include "assetjobs.h"
#include "editorplan.h"

#include <lain/core/uri.h>
#include <lain/image/procedural.h>
#include <lain/rigplan/scene.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>

namespace rp = lain::rigplan;

// The editor's plan with a procedural floor coarse enough to bake at once (20 texels a metre), and a
// prop whose file is not there. Its jobs: the bake, the prop's read, which fails, and the board's
// drawing, which fails too, since no board renderer is registered here.
static rp::RigPlan bakingPlan()
{
	rp::RigPlan plan = editorplan::plan(lain::core::Uri{"/missing/chair.obj"});
	plan.room->floor.material.surface = lain::image::ProceduralTexture{3, 1.0};
	return plan;
}

TEST_CASE("a gesture posts no asset job, and what it owed is posted once it ends", "[rigplan][assets]")
{
	const rp::RigPlan plan = bakingPlan();
	rp::SceneAssets assets;
	rigplan::AssetJobs jobs;
	const std::size_t needed = rp::missingAssets(plan, assets).size();
	REQUIRE(needed == 3);

	jobs.post(plan, assets, true, true); // the plan changed, mid-drag
	CHECK(jobs.pending() == 0);
	jobs.post(plan, assets, false, true); // the drag goes on
	CHECK(jobs.pending() == 0);
	jobs.post(plan, assets, false, false); // and ends, the plan unchanged since: what it owed goes now
	CHECK(jobs.pending() == needed);

	CHECK(jobs.collect(assets));
	CHECK(jobs.pending() == 0);
	CHECK(assets.baked.size() == 1);
	CHECK(assets.failed.size() == 2);
	CHECK(rp::missingAssets(plan, assets).empty()); // each made, or failed and not asked for again
	CHECK_FALSE(jobs.collect(assets));
}

TEST_CASE("outside a gesture a change posts its jobs at once, and no change asks nothing", "[rigplan][assets]")
{
	const rp::RigPlan plan = bakingPlan();
	rp::SceneAssets assets;
	rigplan::AssetJobs jobs;
	jobs.post(plan, assets, true, false);
	CHECK(jobs.pending() == 3);
	jobs.post(plan, assets, true, false); // asked again before any is collected: still one entry a job
	CHECK(jobs.pending() == 3);
	CHECK(jobs.collect(assets));

	// The bake let go of without saying so: with nothing changed, what the plan needs is not asked.
	assets.baked.clear();
	jobs.post(plan, assets, false, false);
	CHECK(jobs.pending() == 0);
	jobs.post(plan, assets, true, false);
	CHECK(jobs.pending() == 1);
}
