#include "common.h"

#include <lain/string/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <numeric>
#include <utility>
#include <variant>

namespace lain::camera::registration::detail
{
	std::string_view unitsWord(EvidenceUnit unit)
	{
		switch (unit)
		{
			case EvidenceUnit::CaptureGroup:
				return "groups";
			case EvidenceUnit::Track:
				return "tracks";
		}
		return "units";
	}

	Report failed(Report report, core::Time start, Failure failure, std::string detail)
	{
		report.status = RegistrationStatus::Failed;
		report.cameras.clear();
		report.failures.push_back({failure, std::move(detail)});
		report.elapsed = core::Time::now() - start;
		return report;
	}

	std::optional<Report> resolveProfile(Report& report, const Request& request, EvidenceUnit unit,
										 std::string_view ownProfile, core::Time start)
	{
		const std::string name = request.fitnessProfile.empty() ? std::string(ownProfile) : request.fitnessProfile;
		const std::optional<FitnessProfile> profile = fitnessProfile(name);
		if (!profile)
			return failed(report, start, Failure::UnknownFitnessProfile, "no fitness profile is named \"" + name + "\"");
		if (profile->unit != unit)
			return failed(report, start, Failure::IncompatibleFitnessProfile,
						  lain::string::format("fitness profile \"{}\" counts {}, and this method's evidence is {}", name,
											   unitsWord(profile->unit), unitsWord(unit)));
		report.thresholds = resolve(*profile, request.overrides);
		return std::nullopt;
	}

	std::optional<std::uint32_t> CanonicalCameras::indexOf(const capture::CameraIdentity& camera) const
	{
		const auto found = std::lower_bound(cameras.begin(), cameras.end(), camera,
											[](const RigCamera& c, const capture::CameraIdentity& id)
											{ return c.camera < id; });
		if (found == cameras.end() || found->camera != camera)
			return std::nullopt;
		return std::uint32_t(found - cameras.begin());
	}

	std::optional<Report> canonicalCameras(Report& report, CanonicalCameras& out, const std::vector<RigCamera>& cameras,
										   const Request& request, core::Time start)
	{
		std::vector<std::size_t> order(cameras.size());
		std::iota(order.begin(), order.end(), std::size_t{0});
		std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b)
						 { return cameras[a].camera < cameras[b].camera; });
		for (const std::size_t i : order)
		{
			if (cameras[i].camera.empty())
				return failed(report, start, Failure::InvalidDataset, "a camera has no identity");
			if (!out.cameras.empty() && out.cameras.back().camera == cameras[i].camera)
				return failed(report, start, Failure::InvalidDataset,
							  "two cameras have the identity \"" + cameras[i].camera.value + "\"");
			out.cameras.push_back(cameras[i]);
			out.given.push_back(i);
		}
		if (out.cameras.size() < 2)
			return failed(report, start, Failure::TooFewCameras,
						  lain::string::format("{} cameras; a registration needs at least two", out.cameras.size()));
		if (request.reference)
		{
			out.reference = out.indexOf(*request.reference);
			if (!out.reference)
				return failed(report, start, Failure::UnknownReference,
							  "the requested reference \"" + request.reference->value + "\" is not a camera of the dataset");
		}
		return std::nullopt;
	}

	std::optional<Report> checkApplicability(Report& report, std::vector<Applicability>& out,
											 const std::vector<RigCamera>& cameras,
											 const std::vector<std::optional<ImageGeometry>>& geometry,
											 const Request& request, core::Time start)
	{
		for (std::size_t c = 0; c < cameras.size(); ++c)
		{
			const RigCamera& camera = cameras[c];
			const ImageGeometry measured = geometry[c] ? *geometry[c] : camera.model.image();
			const Applicability applies = applicability(camera.model, measured);
			out.push_back(applies);
			if (applies == Applicability::Incompatible)
				return failed(report, start, Failure::IncompatibleModel,
							  lain::string::format("camera \"{}\"'s model is {}x{} and its footage is {}x{}", camera.camera.value,
												   camera.model.image().width, camera.model.image().height, measured.width,
												   measured.height));
			if (applies == Applicability::Unknown && request.unknownApplicability == ApplicabilityPolicy::Refuse)
				return failed(report, start, Failure::UnknownApplicability,
							  "camera \"" + camera.camera.value +
								  "\"'s model is of unknown applicability to its footage, and the request refuses that");
		}
		return std::nullopt;
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

	// The mean of a kind's squared residual per point over its median, when its errors are normal, so a
	// median taken robustly can stand in for the mean an RMS takes: a 2-D error's squared length is a
	// chi-squared of two degrees of freedom (1 / ln 2), and a 1-D one's of one; a view's mean over its
	// dozens of corners is concentrated enough that its median is its mean.
	static double meanOverMedian(HeldOutKind kind)
	{
		switch (kind)
		{
			case HeldOutKind::Transfer:
				return 1.4426950408889634; // 1 / ln 2
			case HeldOutKind::Epipolar:
				return 2.1981093383177324; // 1 / 0.45493642311957283, the median of chi-squared(1)
			case HeldOutKind::View:
				return 1.0;
		}
		return 1.0;
	}

	// The median-based held-out residual of `members`, radians: per kind, the median of its members' mean
	// squared residual per point times the kind's mean-over-median, the kinds weighted by their points.
	static double robustAngleOf(const std::vector<const HeldOutMember*>& members)
	{
		std::array<std::vector<double>, 3> perPoint;
		std::array<double, 3> points{};
		double total = 0;
		for (const HeldOutMember* m : members)
		{
			perPoint[std::size_t(m->kind)].push_back(m->sumSquaredAngle / m->residuals);
			points[std::size_t(m->kind)] += m->residuals;
			total += m->residuals;
		}
		double meanSquare = 0;
		for (const HeldOutKind kind : {HeldOutKind::Transfer, HeldOutKind::Epipolar, HeldOutKind::View})
		{
			const std::size_t k = std::size_t(kind);
			if (!perPoint[k].empty())
				meanSquare += points[k] / total * meanOverMedian(kind) * median(perPoint[k]);
		}
		return std::sqrt(meanSquare);
	}

	std::variant<HeldOutEvidence, Unavailable> heldOutEvidence(std::uint32_t units, std::uint32_t unpredicted,
															   const std::vector<HeldOutMember>& members,
															   const std::vector<capture::CameraIdentity>& cameras)
	{
		HeldOutEvidence evidence;
		evidence.units = units;
		evidence.unpredicted = unpredicted;
		std::vector<const HeldOutMember*> predicted;
		std::vector<std::vector<const HeldOutMember*>> ofCamera(cameras.size());
		double angles = 0, pixels = 0;
		std::uint32_t pixelPoints = 0;
		for (const HeldOutMember& m : members)
		{
			if (m.residuals == 0)
				continue;
			predicted.push_back(&m);
			ofCamera[m.camera].push_back(&m);
			++evidence.predictions;
			evidence.residuals += m.residuals;
			if (m.kind == HeldOutKind::Epipolar)
				++evidence.epipolar;
			angles += m.sumSquaredAngle;
			pixels += m.sumSquaredPixels;
			pixelPoints += m.pixelPoints;
			evidence.worstPixels = std::max(evidence.worstPixels, m.worstPixels);
		}
		if (evidence.residuals == 0)
			return Unavailable{"no held-out member could be predicted from the others"};
		evidence.robustAngle = robustAngleOf(predicted);
		evidence.rmsAngle = std::sqrt(angles / evidence.residuals);
		evidence.rmsPixels = pixelPoints > 0 ? std::sqrt(pixels / pixelPoints) : 0.0;
		for (std::size_t c = 0; c < cameras.size(); ++c)
		{
			if (ofCamera[c].empty())
				continue;
			HeldOutCamera camera;
			camera.camera = cameras[c];
			double cameraAngles = 0;
			for (const HeldOutMember* m : ofCamera[c])
			{
				++camera.predictions;
				camera.residuals += m->residuals;
				cameraAngles += m->sumSquaredAngle;
			}
			camera.robustAngle = robustAngleOf(ofCamera[c]);
			camera.rmsAngle = std::sqrt(cameraAngles / camera.residuals);
			evidence.perCamera.push_back(camera);
		}
		std::sort(evidence.perCamera.begin(), evidence.perCamera.end(),
				  [](const HeldOutCamera& a, const HeldOutCamera& b)
				  { return a.camera < b.camera; });
		return evidence;
	}

	double resampledSpread(std::vector<double> distances)
	{
		// sqrt(3) / 1.5381722544550522, the median of a chi distribution of three degrees of freedom.
		constexpr double toRms = 1.1260447603006027;
		return toRms * median(std::move(distances));
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
		const std::string_view units = unitsWord(report.thresholds.unit);

		std::uint32_t fewest = std::numeric_limits<std::uint32_t>::max(), short_ = 0;
		const CameraEvidence* weakest = nullptr;
		for (const CameraEvidence& c : d.cameras)
		{
			if (c.shared < t.minimumSharedPerCamera)
				++short_;
			if (c.shared < fewest)
			{
				fewest = c.shared;
				weakest = &c;
			}
		}
		if (short_ > 0 && weakest)
			note(lain::string::format("{} of {} cameras in fewer than {} shared {} (fewest: {}, in {})", short_,
									  d.cameras.size(), t.minimumSharedPerCamera, units, weakest->camera.value, fewest));

		const std::vector<GraphEdge> weak = weakBridges(graph, t.minimumBridgeShared);
		if (!weak.empty())
			note(lain::string::format("weak bridges: {} under {} shared {} (first: {}-{}, on {})", weak.size(),
									  t.minimumBridgeShared, units, d.cameras[weak[0].a].camera.value,
									  d.cameras[weak[0].b].camera.value, weak[0].shared));

		if (const auto* e = std::get_if<HeldOutEvidence>(&report.heldOut))
		{
			if (e->robustAngle > t.maximumHeldOutAngle)
				note(lain::string::format("held-out transfer {:.3g} mrad exceeds {:.3g}", e->robustAngle * 1000,
										  t.maximumHeldOutAngle * 1000));

			// Each camera on its own, once it has enough points predicted: how many are over the bound, and
			// the worst.
			std::uint32_t over = 0, judged = 0;
			const HeldOutCamera* worst = nullptr;
			for (const HeldOutCamera& c : e->perCamera)
			{
				if (c.residuals < t.minimumHeldOutPerCamera)
					continue;
				++judged;
				if (c.robustAngle > t.maximumHeldOutAngle)
				{
					++over;
					if (!worst || c.robustAngle > worst->robustAngle)
						worst = &c;
				}
			}
			if (worst)
				note(lain::string::format("{} of {} cameras' held-out transfer exceeds {:.3g} mrad (worst: {}, {:.3g})",
										  over, judged, t.maximumHeldOutAngle * 1000, worst->camera.value,
										  worst->robustAngle * 1000));
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
