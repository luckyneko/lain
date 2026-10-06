#include "common.h"

#include <lain/string/format.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <numeric>
#include <random>
#include <utility>
#include <variant>

namespace lain::camera::registration::detail
{
	double rotationBetween(const math::RigidTransformd& a, const math::RigidTransformd& b)
	{
		const math::Quatd relative = math::conjugate(a.rotation()) * b.rotation();
		const double v = std::sqrt(relative.x * relative.x + relative.y * relative.y + relative.z * relative.z);
		return 2.0 * std::atan2(v, std::abs(relative.w));
	}

	double median(std::vector<double> values)
	{
		if (values.empty())
			return std::numeric_limits<double>::infinity();
		const std::size_t half = values.size() / 2;
		std::nth_element(values.begin(), values.begin() + std::ptrdiff_t(half), values.end());
		const double upper = values[half];
		if (values.size() % 2 == 1)
			return upper;
		const double lower = *std::max_element(values.begin(), values.begin() + std::ptrdiff_t(half));
		return (lower + upper) / 2;
	}

	std::uint32_t root(std::vector<std::uint32_t>& parent, std::uint32_t x)
	{
		while (parent[x] != x)
		{
			parent[x] = parent[parent[x]];
			x = parent[x];
		}
		return x;
	}

	Report failed(Report report, core::Time start, Failure failure, std::string detail)
	{
		report.status = RegistrationStatus::Failed;
		report.cameras.clear();
		report.failures.push_back({failure, std::move(detail)});
		report.elapsed = core::Time::now() - start;
		return report;
	}

	std::vector<bool> holdOut(std::size_t cameras, const std::vector<std::vector<std::uint32_t>>& unitCameras,
							  double fraction, std::size_t minimum)
	{
		std::vector<bool> held(unitCameras.size(), false);
		if (!(fraction > 0) || unitCameras.size() < minimum)
			return held;

		std::vector<std::uint32_t> shared(cameras * cameras, 0);
		const auto pairsOf = [&](std::size_t u)
		{
			std::vector<std::size_t> pairs;
			const std::vector<std::uint32_t>& seen = unitCameras[u];
			for (std::size_t i = 0; i < seen.size(); ++i)
			{
				for (std::size_t j = i + 1; j < seen.size(); ++j)
					pairs.push_back(std::min(seen[i], seen[j]) * cameras + std::max(seen[i], seen[j]));
			}
			return pairs;
		};
		for (std::size_t u = 0; u < unitCameras.size(); ++u)
		{
			for (const std::size_t pair : pairsOf(u))
				++shared[pair];
		}
		const auto componentCount = [&]()
		{
			std::vector<std::uint32_t> parent(cameras);
			std::iota(parent.begin(), parent.end(), 0u);
			std::size_t components = cameras;
			for (std::size_t pair = 0; pair < shared.size(); ++pair)
			{
				if (shared[pair] == 0)
					continue;
				const std::uint32_t a = root(parent, std::uint32_t(pair / cameras));
				const std::uint32_t b = root(parent, std::uint32_t(pair % cameras));
				if (a != b)
				{
					parent[a] = b;
					--components;
				}
			}
			return components;
		};
		const std::size_t components = componentCount();
		const std::size_t stride = std::max<std::size_t>(2, std::size_t(std::lround(1.0 / fraction)));
		for (std::size_t u = stride / 2; u < unitCameras.size(); u += stride)
		{
			const std::vector<std::size_t> pairs = pairsOf(u);
			bool cuts = false;
			for (const std::size_t pair : pairs)
			{
				if (--shared[pair] == 0)
					cuts = true;
			}
			if (cuts && componentCount() > components)
			{
				for (const std::size_t pair : pairs)
					++shared[pair];
				continue;
			}
			held[u] = true;
		}
		return held;
	}

	std::uint32_t chooseReference(const std::vector<std::uint32_t>& sharedPerCamera, std::optional<std::uint32_t> requested)
	{
		if (requested)
			return *requested;
		std::uint32_t reference = 0;
		for (std::uint32_t c = 1; c < sharedPerCamera.size(); ++c)
		{
			if (sharedPerCamera[c] > sharedPerCamera[reference])
				reference = c;
		}
		return reference;
	}

	std::vector<std::vector<std::uint32_t>> bootstrapDraws(const std::vector<std::uint32_t>& units,
														   std::uint32_t resamples, std::uint64_t seed)
	{
		std::mt19937_64 engine(seed);
		std::vector<std::vector<std::uint32_t>> draws(resamples);
		for (std::vector<std::uint32_t>& draw : draws)
		{
			for (std::size_t i = 0; i < units.size(); ++i)
				draw.push_back(units[std::size_t(engine() % units.size())]);
		}
		return draws;
	}

	bool meets(const FitnessThresholds& t, const char* tier, bool complete, const Report& report,
			   const ObservationGraph& graph, std::uint32_t outliers, std::uint32_t fitted,
			   std::vector<std::string>& notes)
	{
		bool ok = true;
		const auto note = [&](std::string text)
		{
			notes.push_back(std::string(tier) + ": " + std::move(text));
			ok = false;
		};
		const Diagnostics& d = report.diagnostics;

		std::uint32_t fewest = std::numeric_limits<std::uint32_t>::max(), short_ = 0;
		const CameraEvidence* weakest = nullptr;
		for (const CameraEvidence& c : d.cameras)
		{
			if (c.groups < t.minimumGroupsPerCamera)
				++short_;
			if (c.groups < fewest)
			{
				fewest = c.groups;
				weakest = &c;
			}
		}
		if (short_ > 0 && weakest)
			note(lain::string::format("{} of {} cameras in fewer than {} shared groups (fewest: {}, in {})", short_,
									  d.cameras.size(), t.minimumGroupsPerCamera, weakest->camera.value, fewest));

		const std::vector<GraphEdge> weak = weakBridges(graph, t.minimumBridgeGroups);
		if (!weak.empty())
			note(lain::string::format("weak bridges: {} under {} shared groups (first: {}-{}, on {})", weak.size(),
									  t.minimumBridgeGroups, d.cameras[weak[0].a].camera.value,
									  d.cameras[weak[0].b].camera.value, weak[0].sharedGroups));

		if (const auto* e = std::get_if<HeldOutEvidence>(&report.heldOut))
		{
			if (e->rmsAngle > t.maximumHeldOutAngle)
				note(lain::string::format("held-out transfer {:.3g} mrad exceeds {:.3g}", e->rmsAngle * 1000,
										  t.maximumHeldOutAngle * 1000));
		}
		else if (complete)
			note("no held-out evidence: " + std::get<Unavailable>(report.heldOut).reason);

		if (const auto* e = std::get_if<ResamplingEvidence>(&report.resampling))
		{
			if (e->rotationVariation > t.maximumRotationVariation)
				note(lain::string::format("rotation variation {:.3g} mrad exceeds {:.3g} ({})", e->rotationVariation * 1000,
										  t.maximumRotationVariation * 1000, e->worstCamera.value));
			if (e->translationVariation > t.maximumTranslationVariation)
				note(lain::string::format("translation variation {:.3g} of the median depth exceeds {:.3g} ({})",
										  e->translationVariation, t.maximumTranslationVariation, e->worstCamera.value));
		}
		else if (complete)
			note("no resampling evidence: " + std::get<Unavailable>(report.resampling).reason);

		const double outlierFraction = fitted == 0 ? 0.0 : double(outliers) / double(fitted);
		if (outlierFraction > t.maximumOutlierFraction)
			note(lain::string::format("{} of {} observations are outliers, above {:.3g}", outliers, fitted,
									  t.maximumOutlierFraction));
		return ok;
	}

	void judge(Report& report, const ObservationGraph& graph, std::uint32_t outliers, std::uint32_t fitted)
	{
		std::vector<std::string> readyNotes, exploratoryNotes;
		if (meets(report.thresholds.ready, "Ready", true, report, graph, outliers, fitted, readyNotes))
			report.verdict = Verdict::Ready;
		else if (meets(report.thresholds.exploratory, "Exploratory", false, report, graph, outliers, fitted, exploratoryNotes))
			report.verdict = Verdict::Exploratory;
		else
			report.verdict = Verdict::Rejected;
		report.fitnessNotes = readyNotes;
		report.fitnessNotes.insert(report.fitnessNotes.end(), exploratoryNotes.begin(), exploratoryNotes.end());
	}
} // namespace lain::camera::registration::detail
