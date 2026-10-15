#include "common.h"

#include <cmath>
#include <utility>

namespace lain::camera::calibration::detail
{
	// Sample standard deviation.
	static double deviation(const std::vector<double>& values)
	{
		double mean = 0;
		for (const double v : values)
			mean += v / double(values.size());
		double sum = 0;
		for (const double v : values)
			sum += (v - mean) * (v - mean);
		return std::sqrt(sum / double(values.size() - 1));
	}

	static double average(const std::vector<double>& values)
	{
		double mean = 0;
		for (const double v : values)
			mean += v / double(values.size());
		return mean;
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
		evidence.focalVariation = std::max(deviation(fx) / average(fx), deviation(fy) / average(fy));
		if (principalPointHeld)
			evidence.principalPointVariation = Held{};
		else
		{
			const double focal = (average(fx) + average(fy)) / 2;
			evidence.principalPointVariation = std::max(deviation(cx), deviation(cy)) / focal;
		}
		return evidence;
	}
} // namespace lain::camera::calibration::detail
