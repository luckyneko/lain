#pragma once

// A targetless calibration report as text, every field but its timings, for tests that ask whether two
// calibrations gave one report: over stand-ins (test_targetlesscalibration.cpp) and through Ceres
// (plugins/camera/ceres/test). One spelling, so "the same report" means the same thing in both.
//
// Numbers print to 17 significant digits, so two reports compare equal only when every double is
// bit-identical: a comparison here is of one computation run twice, never of two platforms, which
// the goldens' coarser printing (goldentext.h) is for.

#include <lain/camera/calibration/report.h>
#include <lain/meta/enums.h>

#include <catch2/catch_test_macros.hpp>

#include <iomanip>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace lain::camera::testing::targetlesstext
{
	inline std::string number(double x)
	{
		std::ostringstream out;
		out << std::setprecision(17) << x;
		return out.str();
	}

	template <typename E>
	std::string name(E value)
	{
		return std::string(meta::enums::name(value));
	}

	inline std::string model(const CameraModel& m)
	{
		const Intrinsics& k = m.intrinsics();
		std::string text = "f(" + number(k.fx) + " " + number(k.fy) + ") c(" + number(k.cx) + " " + number(k.cy) + ") " +
						   std::string(displayName(m.distortion())) + " (";
		for (const double c : coefficients(m.distortion()))
			text += " " + number(c);
		return text + ")";
	}

	inline std::string positions(const std::vector<std::uint32_t>& list)
	{
		std::string text;
		for (const std::uint32_t p : list)
			text += " " + std::to_string(p);
		return text;
	}

	// Every field of a targetless calibration report but its timings, its execution policy, and what
	// only extracting adds (the extraction's report and the backends that extracted), so two reports
	// of one calibration compare equal however it ran and whichever entry point it came through.
	inline std::string describe(const calibration::Report& r)
	{
		using namespace calibration;
		std::ostringstream out;
		out << "status " << name(r.status) << "\n";
		for (const FailureReason& f : r.failures)
			out << "failure " << name(f.failure) << ": " << f.detail << "\n";
		out << "model " << (r.model ? model(*r.model) : "unset") << "\n";
		out << "verdict " << name(r.verdict) << "\n";
		for (const std::string& note : r.fitnessNotes)
			out << "note " << note << "\n";
		out << "thresholds " << r.thresholds.name << "\n";
		if (const auto* h = std::get_if<HeldOutEvidence>(&r.heldOut))
			out << "heldOut " << h->views << " " << h->residuals << " " << h->viewsWithoutPose << " " << number(h->rmsAngle)
				<< " " << number(h->rmsPixels) << " " << number(h->worstPixels) << " " << h->outliers << " "
				<< (h->rmsWhitened ? number(*h->rmsWhitened) : std::string("-")) << "\n";
		else
			out << "heldOut unavailable: " << std::get<Unavailable>(r.heldOut).reason << "\n";
		if (const auto* s = std::get_if<ResamplingEvidence>(&r.resampling))
		{
			const double* principalPoint = std::get_if<double>(&s->principalPointVariation);
			out << "resampling " << s->resamples << " " << number(s->focalVariation) << " "
				<< (principalPoint ? number(*principalPoint) : std::string("held")) << "\n";
		}
		else
			out << "resampling unavailable: " << std::get<Unavailable>(r.resampling).reason << "\n";
		out << "seeded";
		for (const std::string& field : r.seededFields)
			out << " " << field;
		out << "\n";

		const Diagnostics& d = r.diagnostics;
		out << "views " << d.viewsSelected << " " << d.viewsHeldOut << " " << number(d.coverage) << " " << number(d.fitRmsPixels)
			<< " " << d.fitResiduals << "\nselected" << positions(d.selected) << "\nheldOutViews" << positions(d.heldOut) << "\n";
		const TargetlessDiagnostics& t = std::get<TargetlessDiagnostics>(d.method);
		for (const media::FrameRef& f : t.frames)
			out << "frame " << f.toString() << "\n";
		out << "tracks " << t.tracks << " " << t.tracksUsable << "\nseedPairs " << t.seedPairs << " " << t.seedPairsRefused
			<< "\n";
		for (const SweepStart& tried : t.sweep)
		{
			out << "start " << number(tried.focalLength) << " " << tried.placed << " ";
			if (const double* cost = std::get_if<double>(&tried.outcome))
				out << number(*cost) << "\n";
			else
			{
				const FailureReason& failure = std::get<FailureReason>(tried.outcome);
				out << name(failure.failure) << ": " << failure.detail << "\n";
			}
		}
		out << "swept " << (t.swept ? std::to_string(*t.swept) : "unset") << "\n";
		out << "seed" << positions(t.seed) << " " << number(t.seedAngle) << " " << t.seedInliers << " " << t.seedAtInfinity
			<< " " << t.seedBelowFloor << "\n";
		out << "placement" << positions(t.placement) << "\nunplaced" << positions(t.unplaced) << "\n";
		out << "behind " << t.behind << " landmarks " << t.landmarks << " untriangulated " << t.untriangulated << " pruned "
			<< t.pruned << "\n";
		if (t.refinement)
			out << "refinement " << name(t.refinement->status) << " " << t.refinement->iterations << " "
				<< number(t.refinement->initialCost) << " " << number(t.refinement->finalCost) << " " << t.refinement->residuals
				<< " " << t.refinement->landmarks << "\n";
		for (const Outlier& o : t.outliers)
			out << "outlier " << o.track << " " << o.frame << " " << number(o.rmsWhitened) << "\n";

		const Reproducibility& p = r.reproducibility;
		for (const std::string& source : p.sources)
			out << "source " << source << "\n";
		const TargetlessRecord& record = std::get<TargetlessRecord>(p.method);
		out << "frames " << p.frames << " estimator " << p.estimator.backend << " geometry " << record.geometry.backend
			<< " refiner " << record.refiner.backend << "\n";
		out << "request " << name(p.request.model) << " " << name(p.request.importedPolicy) << " "
			<< p.request.fitnessProfile << " " << number(p.request.heldOutFraction) << " " << p.request.resamples << " "
			<< p.request.seed << "\n";
		out << "settings " << record.settings.freePrincipalPoint << " " << number(record.settings.loss.scale) << "\n";
		out << "toString " << r.toString() << "\n";
		return out.str();
	}

	inline void requireSameReport(const calibration::Report& a, const calibration::Report& b)
	{
		const std::string x = describe(a), y = describe(b);
		INFO(x);
		INFO(y);
		REQUIRE(x == y);
	}
} // namespace lain::camera::testing::targetlesstext
