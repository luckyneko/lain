#pragma once

#include "assetjobs.h"

#include <lain/data/value.h>
#include <lain/rigplan/rigplan.h>
#include <lain/rigplan/scene.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rigplan
{
	// The open plan's assets, one set for every pane that draws the plan: the pictures, bakes, board
	// drawings and meshes it needs (lain::rigplan::SceneAssets), the jobs still making them (AssetJobs),
	// and two revisions a pane builds from. The Viewport, a camera view and the visibility map each read
	// it, so nothing is baked or read twice, and the plan's snapshot is taken and compared once a frame
	// rather than once a reader.
	//
	// The window drives it: poll() at the start of a frame, update() once every pane has had its say.
	// A reader keeps the revisions it last built from and builds again when either has moved on.
	class PlanAssets
	{
	public:
		// Keeps what the jobs made since the last frame. Never waits.
		void poll();

		// Once a frame, after every pane has had its say. When `plan`'s snapshot differs from the last one
		// seen, the plan revision moves on, the surfaces' and poles' meshes are made again and what the
		// plan no longer draws is pruned. Then what it needs and lacks is posted, unless a `gesture` is
		// under way (AssetJobs). The revision moves on during a gesture too: only the jobs wait for it.
		void update(const lain::rigplan::RigPlan& plan, bool gesture);

		const lain::rigplan::SceneAssets& assets() const { return m_assets; }
		// Changes exactly when the plan's snapshot does, so a reader that built from it is up to date with
		// the plan while it stands. 0 until a plan is seen.
		std::uint64_t planRevision() const { return m_planRevision; }
		// Changes whenever a job's outcome is kept: an asset made, or why one was not.
		std::uint64_t assetsRevision() const { return m_assetsRevision; }

		// Jobs posted and not yet kept, and why those that failed made nothing, each naming its owner and
		// file.
		std::size_t pending() const { return m_jobs.pending(); }
		std::vector<std::string> problems() const;
		// Every job that reached the pool, for a test: one asked for again while it runs is not posted again.
		std::size_t posted() const { return m_jobs.posted(); }

		// Waits for every job still running, keeping nothing they made: before the pool stops.
		void wait() { m_jobs.wait(); }

	private:
		lain::rigplan::SceneAssets m_assets;
		AssetJobs m_jobs;
		std::optional<lain::data::Value> m_snapshot; // the plan as last seen
		std::uint64_t m_planRevision = 0;
		std::uint64_t m_assetsRevision = 0;
		bool m_collected = false; // a job's outcome kept since the last update, so the plan is asked again
	};
} // namespace rigplan
