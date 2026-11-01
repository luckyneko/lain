// The plan's asset jobs (assetjobs.h): what a gesture holds back, and what it owes once it ends. And the
// one set of assets every pane reads (planassets.h, M15 slice 9c): each job reaches the pool once however
// many panes read what it makes, and the plan's revision follows its snapshot. With no pool started a job
// runs as it is posted, so its outcome is there at the next collect.

#include "assetjobs.h"
#include "editorplan.h"
#include "planassets.h"

#include <lain/core/uri.h>
#include <lain/image/procedural.h>
#include <lain/rigplan/scene.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>

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

// A pane that draws the plan, as the Viewport does: it builds again whenever either revision has moved on,
// from the one set of assets.
struct Reader
{
	std::uint64_t plan = ~std::uint64_t(0);
	std::uint64_t assets = ~std::uint64_t(0);
	int builds = 0;
	const rp::SceneAssets* read = nullptr;

	void follow(const rigplan::PlanAssets& shared)
	{
		if (shared.planRevision() == plan && shared.assetsRevision() == assets)
			return;
		plan = shared.planRevision();
		assets = shared.assetsRevision();
		read = &shared.assets();
		++builds;
	}
};

TEST_CASE("two panes read one set of assets, and each job reaches the pool once", "[rigplan][assets]")
{
	const rp::RigPlan plan = bakingPlan();
	rigplan::PlanAssets shared;
	Reader viewport;
	Reader camera;
	// A frame as the window runs it: what the jobs made kept, the plan's assets brought up to date, and
	// then every reader.
	const auto frame = [&](const rp::RigPlan& p)
	{
		shared.poll();
		shared.update(p, false);
		viewport.follow(shared);
		camera.follow(shared);
	};

	frame(plan); // the plan first seen: its three jobs posted, and each reader builds without them
	CHECK(shared.posted() == 3);
	CHECK(shared.pending() == 3);
	CHECK(viewport.builds == 1);
	CHECK(camera.builds == 1);
	frame(plan); // the jobs' outcomes kept: each reader builds again, and nothing is asked for twice
	CHECK(shared.posted() == 3);
	CHECK(shared.pending() == 0);
	CHECK(viewport.builds == 2);
	CHECK(camera.builds == 2);
	CHECK(viewport.read == &shared.assets());
	CHECK(camera.read == viewport.read);
	CHECK(shared.assets().baked.size() == 1);
	CHECK(shared.problems().size() >= 2); // the prop's file, and the board with no renderer
	frame(plan);						  // nothing new
	CHECK(viewport.builds == 2);
	CHECK(camera.builds == 2);

	// Edited while a job runs (it has not been kept yet), the plan asks for that asset again: it is not
	// posted again.
	rp::RigPlan edited = plan;
	edited.room->floor.material.surface = lain::image::ProceduralTexture{4, 1.0}; // a bake of its own
	shared.update(edited, false);
	CHECK(shared.posted() == 4);
	edited.boxes.front().centre.x += 0.5;
	shared.update(edited, false);
	CHECK(shared.posted() == 4);
	shared.poll();
	CHECK(shared.pending() == 0);
}

TEST_CASE("the plan's revision changes exactly when its snapshot does", "[rigplan][assets]")
{
	rp::RigPlan plan = editorplan::plan(lain::core::Uri{"/missing/chair.obj"});
	rigplan::PlanAssets shared;
	CHECK(shared.planRevision() == 0);
	shared.update(plan, false);
	CHECK(shared.planRevision() == 1);
	for (int i = 0; i < 5; ++i)
		shared.update(plan, false);
	CHECK(shared.planRevision() == 1);
	// Another plan of the same value is the same plan: the snapshot is compared, not the object.
	const rp::RigPlan copy = plan;
	shared.update(copy, false);
	CHECK(shared.planRevision() == 1);

	// During a gesture the revision moves on with every change: only the jobs wait for the gesture.
	plan.boxes.front().centre.x += 1e-9;
	shared.update(plan, true);
	CHECK(shared.planRevision() == 2);
	shared.update(plan, true);
	CHECK(shared.planRevision() == 2);
	// Back again is a change from the last one seen, though an earlier one was the same.
	plan.boxes.front().centre.x -= 1e-9;
	shared.update(plan, false);
	CHECK(shared.planRevision() == 3);
	plan.timeline.frames += 1;
	shared.update(plan, false);
	CHECK(shared.planRevision() == 4);
}

TEST_CASE("the surfaces' meshes are made again only when the plan changes", "[rigplan][assets]")
{
	// A frame or an asset landing changes no surface, and a mesh is shared with the device by pointer, so a
	// mesh made again would be uploaded again: on every frame of playback, before these were shared.
	const rp::RigPlan plan = bakingPlan();
	rigplan::PlanAssets shared;
	shared.update(plan, false);
	REQUIRE_FALSE(shared.assets().surfaces.empty());
	REQUIRE_FALSE(shared.assets().poles.empty());
	// Held, so an address freed and handed out again cannot pass for the mesh kept.
	const auto surface = shared.assets().surfaces.front();
	const auto pole = shared.assets().poles.begin()->second;
	for (int i = 0; i < 3; ++i)
	{
		shared.poll(); // the jobs' outcomes, kept on the first
		shared.update(plan, false);
	}
	CHECK(shared.assetsRevision() == 1);
	CHECK(shared.assets().surfaces.front() == surface);
	CHECK(shared.assets().poles.begin()->second == pole);

	rp::RigPlan resized = plan;
	resized.room->size.x += 1.0;
	shared.update(resized, false);
	CHECK(shared.assets().surfaces.front() != surface);
}
