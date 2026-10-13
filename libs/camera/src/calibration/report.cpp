#include "lain/camera/calibration/report.h"

#include <lain/meta/enums.h>
#include <lain/string/format.h>

#include <string>
#include <utility>

namespace lain::camera::calibration
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// What a method alone adds to a report's line, after the clauses every method shares. One overload
	// per alternative of Diagnostics::method, so a method without one does not compile.
	static std::string methodSummary(const BoardDiagnostics& d)
	{
		return lain::string::format(", {} of {} frames usable", d.framesUsable, d.framesExamined);
	}

	// Whether the evidence meets one tier. `complete` demands every criterion's evidence be present
	// (Ready); otherwise a criterion whose evidence is unavailable is not held against it. Notes say
	// what fell short.
	static bool meets(const FitnessThresholds& t, const char* tier, bool complete, std::uint32_t views, double covered,
					  const std::variant<HeldOutEvidence, Unavailable>& heldOut,
					  const std::variant<ResamplingEvidence, Unavailable>& resampling, std::vector<std::string>& notes)
	{
		bool ok = true;
		const auto note = [&](std::string text)
		{
			notes.push_back(std::string(tier) + ": " + std::move(text));
			ok = false;
		};
		if (views < t.minimumViews)
			note(std::to_string(views) + " views, fewer than " + std::to_string(t.minimumViews));
		if (covered < t.minimumCoverage)
			note("coverage " + std::to_string(covered) + " is below " + std::to_string(t.minimumCoverage));
		if (const auto* e = std::get_if<HeldOutEvidence>(&heldOut))
		{
			if (e->rmsAngle > t.maximumHeldOutAngle)
				note("held-out RMS angle " + std::to_string(e->rmsAngle) + " rad exceeds " + std::to_string(t.maximumHeldOutAngle));
		}
		else if (complete)
			note("no held-out evidence: " + std::get<Unavailable>(heldOut).reason);
		if (const auto* e = std::get_if<ResamplingEvidence>(&resampling))
		{
			if (e->focalVariation > t.maximumFocalVariation)
				note("focal variation " + std::to_string(e->focalVariation) + " exceeds " + std::to_string(t.maximumFocalVariation));
			// A held principal point was not estimated, so it has no variation to hold against a bound:
			// its criterion does not apply, and it is not missing evidence either.
			if (const double* variation = std::get_if<double>(&e->principalPointVariation))
			{
				if (*variation > t.maximumPrincipalPointVariation)
					note("principal point variation " + std::to_string(*variation) + " rad exceeds " +
						 std::to_string(t.maximumPrincipalPointVariation));
			}
		}
		else if (complete)
			note("no resampling evidence: " + std::get<Unavailable>(resampling).reason);
		return ok;
	}

	// --- Report ---------------------------------------------------------------------

	std::string Report::toString() const
	{
		if (!model)
		{
			if (failures.empty())
				return "Failed";
			return lain::string::format("Failed: {} ({})", meta::enums::name(failures.front().failure),
										failures.front().detail);
		}

		const ImageGeometry& image = model->image();
		std::string text = lain::string::format("{}: {} at {}x{}", meta::enums::name(verdict),
												displayName(model->distortion()), image.width, image.height);
		if (const auto* held = std::get_if<HeldOutEvidence>(&heldOut))
			text += lain::string::format(", held out {:.3g} px RMS over {} views", held->rmsPixels, held->views);
		else
			text += lain::string::format(", no held-out evidence ({})", std::get<Unavailable>(heldOut).reason);
		// A held model selected no calibration views, so there is no selection to describe.
		if (diagnostics.viewsSelected > 0)
			text += lain::string::format(", {} views covering {:.0f}% of the image", diagnostics.viewsSelected,
										 diagnostics.coverage * 100.0);
		text += std::visit([](const auto& d)
						   { return methodSummary(d); }, diagnostics.method);
		// What stood between this verdict and a better one, which is the first thing a person with a
		// Rejected or Exploratory report wants to know.
		if (verdict != Verdict::Ready && !fitnessNotes.empty())
			text += lain::string::format("; short of the next verdict: {}", fitnessNotes.front());
		return text;
	}

	// --- judge ----------------------------------------------------------------------

	Judgement judge(const FitnessProfile& profile, std::uint32_t views, double coverage,
					const std::variant<HeldOutEvidence, Unavailable>& heldOut,
					const std::variant<ResamplingEvidence, Unavailable>& resampling)
	{
		Judgement out;
		std::vector<std::string> readyNotes, exploratoryNotes;
		if (meets(profile.ready, "Ready", true, views, coverage, heldOut, resampling, readyNotes))
			out.verdict = Verdict::Ready;
		else if (meets(profile.exploratory, "Exploratory", false, views, coverage, heldOut, resampling, exploratoryNotes))
			out.verdict = Verdict::Exploratory;
		else
			out.verdict = Verdict::Rejected;
		out.notes = std::move(readyNotes);
		out.notes.insert(out.notes.end(), exploratoryNotes.begin(), exploratoryNotes.end());
		return out;
	}
} // namespace lain::camera::calibration
