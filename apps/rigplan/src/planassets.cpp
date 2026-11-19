#include "planassets.h"

#include "document.h" // snapshot: the plan as one value, which is what "changed" is asked of

#include <utility>

namespace rigplan
{
	namespace rp = lain::rigplan;

	void PlanAssets::poll()
	{
		if (m_jobs.collect(m_assets))
		{
			++m_assetsRevision;
			m_collected = true;
		}
	}

	void PlanAssets::update(const rp::RigPlan& plan, bool gesture)
	{
		lain::data::Value now = snapshot(plan);
		const bool changed = !m_snapshot || *m_snapshot != now;
		if (changed)
		{
			m_snapshot = std::move(now);
			++m_planRevision;
			// The surfaces' and poles' meshes at once (the plan alone decides them); everything slower on
			// the pool, below.
			rp::sceneGeometry(plan, m_assets);
			rp::pruneAssets(plan, m_assets);
		}
		m_jobs.post(plan, m_assets, changed || m_collected, gesture);
		m_collected = false;
	}

	std::vector<std::string> PlanAssets::problems() const
	{
		std::vector<std::string> out;
		for (const auto& [key, problems] : m_assets.failed)
		{
			(void)key;
			out.insert(out.end(), problems.begin(), problems.end());
		}
		return out;
	}
} // namespace rigplan
