#include "assetjobs.h"

#include <lain/string/format.h>

#include <exception>
#include <utility>

namespace rigplan
{
	using namespace lain;
	namespace rp = lain::rigplan;

	AssetJobs::~AssetJobs()
	{
		wait();
	}

	bool AssetJobs::collect(rp::SceneAssets& assets)
	{
		bool any = false;
		for (auto it = m_jobs.begin(); it != m_jobs.end();)
		{
			if (!it->second.complete())
			{
				++it;
				continue;
			}
			rp::AssetOutcome outcome;
			try
			{
				it->second.get(&outcome);
			}
			catch (const std::exception& e)
			{
				outcome = rp::AssetOutcome{it->first, {}, {string::format("{}: {}", it->first, e.what())}};
			}
			rp::addAsset(assets, std::move(outcome));
			any = true;
			it = m_jobs.erase(it);
		}
		return any;
	}

	void AssetJobs::post(const rp::RigPlan& plan, const rp::SceneAssets& assets, bool changed, bool gesture)
	{
		m_owed = m_owed || changed;
		if (!m_owed || gesture)
			return;
		for (rp::AssetJob& job : rp::missingAssets(plan, assets))
		{
			if (m_jobs.count(job.key) == 0)
				m_jobs.emplace(job.key, task::async(std::move(job.run)));
		}
		m_owed = false;
	}

	void AssetJobs::wait()
	{
		// Each job reads only what it captured, but what it makes is kept by whoever collects it.
		for (auto& [key, job] : m_jobs)
		{
			(void)key;
			task::waitAll(job);
		}
		m_jobs.clear();
	}
} // namespace rigplan
