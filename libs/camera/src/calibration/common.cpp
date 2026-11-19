#include "common.h"

#include <cmath>
#include <utility>
#include <vector>

namespace lain::camera::calibration::detail
{
	// How far resampled estimates spread, which one far resample cannot decide: the median absolute
	// deviation from the median, scaled by 1.4826 (one over the standard normal's third quartile) so
	// it is the standard deviation of normally spread estimates, and the profiles' bounds keep their
	// meaning. A bootstrap draw can leave too few views to condition the model, and its estimate then
	// lies far from every other; as a standard deviation, one such draw of twenty decided board
	// calibration's verdict (measured in M9 slice 4, sub-slice 10e: Rejected at 89% and 32% on two
	// seeds, while the model was 0.01% and 0.03% out). Fewer than half the resamples can lie anywhere
	// without moving it, so it measures the typical resample, not the worst one.
	static double spread(const std::vector<double>& values)
	{
		const double centre = median(values);
		std::vector<double> deviations;
		deviations.reserve(values.size());
		for (const double v : values)
			deviations.push_back(std::abs(v - centre));
		return 1.482602218505602 * median(deviations);
	}

	Report failed(Report report, core::Time start, Failure failure, std::string detail)
	{
		report.status = CalibrationStatus::Failed;
		report.failures.push_back({failure, std::move(detail)});
		report.elapsed = core::Time::now() - start;
		return report;
	}

	std::optional<Report> resolveProfile(Report& report, const Request& request, std::string_view ownProfile, core::Time start)
	{
		const std::string name = request.fitnessProfile.empty() ? std::string(ownProfile) : request.fitnessProfile;
		const std::optional<FitnessProfile> profile = fitnessProfile(name);
		if (!profile)
			return failed(report, start, Failure::UnknownFitnessProfile, "no fitness profile is named \"" + name + "\"");
		report.thresholds = resolve(*profile, request.overrides);
		return std::nullopt;
	}

	std::optional<Report> checkImported(Report& report, const Request& request, const ImageGeometry& image, core::Time start)
	{
		if (request.importedPolicy == ImportedModelPolicy::HoldAndValidate && !request.imported)
			return failed(report, start, Failure::NoImportedModel, "HoldAndValidate needs an imported model to hold");
		if (request.imported && applicability(*request.imported, image) == Applicability::Incompatible)
			return failed(report, start, Failure::IncompatibleImportedModel,
						  "the imported model is " + std::to_string(request.imported->image().width) + "x" +
							  std::to_string(request.imported->image().height) + " and the footage is " +
							  std::to_string(image.width) + "x" + std::to_string(image.height));
		return std::nullopt;
	}

	std::optional<CameraModelParameters> initialFrom(Report& report, const Request& request, const ImageGeometry& image)
	{
		if (!request.imported || request.importedPolicy != ImportedModelPolicy::Initial)
			return std::nullopt;
		CameraModelParameters initial{image, request.imported->intrinsics(), neutral(request.model)};
		report.seededFields = {"fx", "fy", "cx", "cy"};
		if (modelOf(request.imported->distortion()) == request.model)
		{
			initial.distortion = request.imported->distortion();
			for (const std::string_view name : coefficientNames(request.model))
				report.seededFields.emplace_back(name);
		}
		return initial;
	}

	std::variant<ResamplingEvidence, Unavailable> stability(const std::vector<std::optional<CameraModelParameters>>& estimates,
															bool principalPointHeld)
	{
		std::vector<double> fx, fy, cx, cy;
		for (const std::optional<CameraModelParameters>& e : estimates)
		{
			if (!e)
				continue;
			fx.push_back(e->intrinsics.fx);
			fy.push_back(e->intrinsics.fy);
			cx.push_back(e->intrinsics.cx);
			cy.push_back(e->intrinsics.cy);
		}
		if (fx.size() < 2)
			return Unavailable{"fewer than two resamples produced an estimate"};

		ResamplingEvidence evidence;
		evidence.resamples = std::uint32_t(fx.size());
		evidence.focalVariation = std::max(spread(fx) / median(fx), spread(fy) / median(fy));
		if (principalPointHeld)
			evidence.principalPointVariation = Held{};
		else
		{
			const double focal = (median(fx) + median(fy)) / 2;
			evidence.principalPointVariation = std::max(spread(cx), spread(cy)) / focal;
		}
		return evidence;
	}
} // namespace lain::camera::calibration::detail
