#pragma once

#include <lain/rigplan/rigplan.h>
#include <lain/rigplan/scene.h>
#include <lain/task/task.h>

#include <cstddef>
#include <map>
#include <string>

namespace rigplan
{
	// The Viewport's asset jobs: each picture to read, surface to bake, board to draw or mesh to read
	// that the plan needs and its assets lack, run on the process pool, so the frame loop never waits on
	// one (ADR-0025's rule, carried to rigplan).
	//
	// None is posted while a gesture is under way (a drag of a field or of the gizmo, or typing into a
	// field). A drag changes the plan every frame, and a job cannot be stopped once posted, so a room
	// resized by a drag would bake every size it passed through, each to the end. What the plan needs
	// meanwhile is owed, and posted once the gesture ends: the point at which a gesture becomes one step
	// of the undo history.
	class AssetJobs
	{
	public:
		AssetJobs() = default;
		AssetJobs(const AssetJobs&) = delete;
		AssetJobs& operator=(const AssetJobs&) = delete;
		~AssetJobs(); // waits for the jobs still running

		// Keeps in `assets` what each job that has finished made, or why it made nothing: true when any
		// had finished. Never waits.
		bool collect(lain::rigplan::SceneAssets& assets);

		// Once a frame. `changed`: the plan or its assets changed since the last call, so what `plan`
		// needs is asked again. Asked during a `gesture`, it stays owed, and is posted by the first call
		// after it. A job already running is not posted again.
		void post(const lain::rigplan::RigPlan& plan, const lain::rigplan::SceneAssets& assets, bool changed, bool gesture);

		// Jobs posted and not yet collected.
		std::size_t pending() const { return m_jobs.size(); }

		// Waits for every job still running and forgets them, keeping nothing they made.
		void wait();

	private:
		std::map<std::string, lain::task::Handle<lain::rigplan::AssetOutcome>> m_jobs; // by job key
		bool m_owed = false;
	};
} // namespace rigplan
