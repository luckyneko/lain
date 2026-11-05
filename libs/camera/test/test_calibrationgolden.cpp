// A board calibration report, every field of it, printed and compared with a committed golden
// (golden/boardcalibration.txt). It holds the calibration contracts still while they are made
// method-neutral (M9 slice 4, sub-slices 1 and 2): a change that moves only where a field lives must
// leave this text byte-identical, and a change to what is computed cannot. Timings are left out. How
// it prints and compares is goldentext.h's, shared with the other method goldens.
//
// The stand-ins answer from a known truth (syntheticboard.h). Where an estimate is asked for, the
// answer depends on which views it was handed, so a different choice of views anywhere (selection,
// hold-out, any resample) moves the numbers here rather than picking an equally exact answer.

#include "goldentext.h"
#include "syntheticboard.h"

#include <lain/camera/calibration/board.h>

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

using namespace lain;
using namespace lain::camera;
using namespace lain::camera::calibration;
using namespace lain::camera::testing;
using namespace lain::camera::testing::golden;
namespace method = lain::camera::calibration::board;
namespace cb = lain::camera::board;

namespace
{
	std::string model(const CameraModel& m)
	{
		const Intrinsics& k = m.intrinsics();
		std::string text = std::to_string(m.image().width) + "x" + std::to_string(m.image().height) + " f(" + number(k.fx) +
						   " " + number(k.fy) + ") c(" + number(k.cx) + " " + number(k.cy) + ") " +
						   std::string(displayName(m.distortion())) + " (";
		const std::vector<double> c = coefficients(m.distortion());
		for (std::size_t i = 0; i < c.size(); ++i)
			text += (i == 0 ? "" : " ") + number(c[i]);
		return text + ") domain(" + number(m.domain().limit) + " " + number(m.domain().limitImage) + ")";
	}

	std::string thresholds(const FitnessThresholds& t)
	{
		return std::to_string(t.minimumViews) + " " + number(t.minimumCoverage) + " " + number(t.maximumHeldOutAngle) +
			   " " + number(t.maximumFocalVariation) + " " + number(t.maximumPrincipalPointVariation) + " " +
			   number(t.maximumHeldOutWhitened);
	}

	std::string positions(const std::vector<std::uint32_t>& list)
	{
		std::string text;
		for (const std::uint32_t p : list)
			text += " " + std::to_string(p);
		return text;
	}

	std::string describe(const Report& r)
	{
		std::ostringstream out;
		out << "status " << name(r.status) << "\n";
		for (const FailureReason& f : r.failures)
			out << "failure " << name(f.failure) << ": " << f.detail << "\n";
		out << "model " << (r.model ? model(*r.model) : "unset") << "\n";
		out << "verdict " << name(r.verdict) << "\n";
		for (const std::string& note : r.fitnessNotes)
			out << "note " << note << "\n";
		out << "thresholds " << r.thresholds.name << " ready(" << thresholds(r.thresholds.ready) << ") exploratory("
			<< thresholds(r.thresholds.exploratory) << ")\n";

		// The labels are the ones the board-shaped report had (sub-slice 2a renamed and moved the
		// fields), so a change that only moves a field leaves the golden byte-identical.
		if (const auto* h = std::get_if<HeldOutEvidence>(&r.heldOut))
			out << "heldOut views " << h->views << " corners " << h->residuals << " withoutPose " << h->viewsWithoutPose
				<< " rmsAngle " << number(h->rmsAngle) << " rmsPixels " << number(h->rmsPixels) << " worstPixels "
				<< number(h->worstPixels) << " rmsWhitened " << optional(h->rmsWhitened) << "\n";
		else
			out << "heldOut unavailable: " << std::get<Unavailable>(r.heldOut).reason << "\n";
		if (const auto* s = std::get_if<ResamplingEvidence>(&r.resampling))
		{
			const double* principalPoint = std::get_if<double>(&s->principalPointVariation);
			out << "resampling " << s->resamples << " focal " << number(s->focalVariation) << " principalPoint "
				<< (principalPoint ? number(*principalPoint) : std::string("held")) << "\n";
		}
		else
			out << "resampling unavailable: " << std::get<Unavailable>(r.resampling).reason << "\n";
		if (r.uncertainty)
		{
			const ParameterUncertainty& u = *r.uncertainty;
			out << "uncertainty " << number(u.fx) << " " << number(u.fy) << " " << number(u.cx) << " " << number(u.cy)
				<< " (";
			for (std::size_t i = 0; i < u.coefficients.size(); ++i)
				out << (i == 0 ? "" : " ") << number(u.coefficients[i]);
			out << ")\n";
		}
		else
			out << "uncertainty unset\n";
		out << "seeded";
		for (const std::string& field : r.seededFields)
			out << " " << field;
		out << "\n";

		const Diagnostics& d = r.diagnostics;
		const BoardDiagnostics& examined = std::get<BoardDiagnostics>(d.method);
		out << "diagnostics.framesExamined " << examined.framesExamined << " framesUsable " << examined.framesUsable
			<< " viewsSelected "
			<< d.viewsSelected << " viewsHeldOut " << d.viewsHeldOut << " coverage " << number(d.coverage)
			<< " fitRmsPixels " << number(d.fitRmsPixels) << " fitResiduals " << d.fitResiduals << "\n";
		out << "selected" << positions(d.selected) << "\n";
		out << "heldOutViews" << positions(d.heldOut) << "\n";
		for (std::size_t i = 0; i < examined.detections.size(); ++i)
			out << "detection " << i << " " << detection(examined.detections[i]) << "\n";

		const Reproducibility& p = r.reproducibility;
		const BoardRecord& record = std::get<BoardRecord>(p.method);
		for (const std::string& source : p.sources)
			out << "source " << source << "\n";
		out << "frames " << p.frames << " detector " << provenance(record.detector) << " estimator "
			<< provenance(p.estimator) << " poseSolver " << provenance(record.poseSolver) << "\n";
		const Request& q = p.request;
		out << "request model " << name(q.model) << " imported " << (q.imported ? model(*q.imported) : "unset")
			<< " policy " << name(q.importedPolicy) << " profile "
			<< (q.fitnessProfile.empty() ? std::string("(method's)") : q.fitnessProfile) << "\n";
		const FitnessOverrides& o = q.overrides;
		out << "request overrides " << optional(o.minimumViews) << " " << optional(o.minimumCoverage) << " "
			<< optional(o.maximumHeldOutAngle) << " " << optional(o.maximumFocalVariation) << " "
			<< optional(o.maximumPrincipalPointVariation) << " " << optional(o.maximumHeldOutWhitened) << "\n";
		// Printed where it was when the request held it (sub-slice 1b moved it to the board record).
		out << "request detection " << (record.detection ? detectionRequest(*record.detection) : "unset") << "\n";
		out << "request maximumViews " << q.maximumViews << " heldOutFraction " << number(q.heldOutFraction)
			<< " resamples " << q.resamples << " seed " << q.seed << " execution " << name(q.execution) << "\n";
		out << "toString " << r.toString() << "\n";
		return out.str();
	}

	std::string section(const char* title, const Report& report)
	{
		return "== " + std::string(title) + "\n" + describe(report);
	}

	// A search unlike the default in every field, so a calibration that searched with the default
	// instead shows in every detection.
	cb::DetectionRequest unusualSearch()
	{
		cb::DetectionRequest search;
		search.scale = cb::LongestSide{480};
		search.refineAtNativeResolution = false;
		search.detail = cb::DetailLevel::Detailed;
		search.minimumCorners = 6;
		return search;
	}

	// The ordinals of the distinct views handed to the estimator, and their sum.
	struct Handed
	{
		std::size_t distinct = 0;
		std::size_t sum = 0;
	};
	Handed handed(const std::vector<cb::Observation>& views)
	{
		std::set<std::size_t> ordinals;
		for (const cb::Observation& v : views)
			ordinals.insert(v.frame.ordinal);
		Handed h;
		h.distinct = ordinals.size();
		for (const std::size_t o : ordinals)
			h.sum += o;
		return h;
	}

	std::string produce()
	{
		std::string text;

		// Ready, through the footage: two boardless frames, an unusual search, and an estimate a
		// little off the truth in a way that depends on which views it was handed.
		{
			resetScene();
			boardScene().boardless = {3, 21};
			script().answer = [](const std::vector<cb::Observation>& views)
			{
				const Handed h = handed(views);
				CameraModelParameters p = trueCamera();
				p.intrinsics.fx += 0.01 * double(h.distinct);
				p.intrinsics.fy += 0.008 * double(h.distinct);
				p.intrinsics.cx += 0.0005 * double(h.sum % 23);
				p.intrinsics.cy -= 0.0005 * double(h.sum % 19);
				return std::optional<CameraModelParameters>{p};
			};
			script().rmsPixels = 0.0421;
			script().uncertainty = ParameterUncertainty{0.5, 0.6, 0.07, 0.08, {0.001, 0.002, 0.0003, 0.0004, 0.005}};
			text += section("ready", method::calibrate(footage(boardScene().frames), specification(), unusualSearch(), Request{}));
		}

		// Rejected, through the footage: an estimate 3% long, seeded from an imported model of
		// another distortion model, with overrides and every count changed from its default.
		{
			resetScene();
			script().answer = [](const std::vector<cb::Observation>& views)
			{
				CameraModelParameters p = trueCamera();
				p.intrinsics.fx = p.intrinsics.fx * 1.03 + 0.05 * double(handed(views).distinct);
				p.intrinsics.fy *= 1.03;
				return std::optional<CameraModelParameters>{p};
			};
			CameraModelParameters inverse = trueCamera();
			inverse.distortion = InverseBrownConrady5{0.1, -0.05, 0.001, -0.0005, 0.0};
			Request request;
			request.imported = *CameraModel::create(inverse).model;
			request.importedPolicy = ImportedModelPolicy::Initial;
			request.overrides.minimumViews = 35;
			request.overrides.maximumHeldOutAngle = 0.001;
			request.maximumViews = 20;
			request.heldOutFraction = 0.25;
			request.resamples = 5;
			request.seed = 9;
			text += section("rejected",
							method::calibrate(footage(boardScene().frames), specification(), cb::DetectionRequest{}, request));
		}

		// A held model, over detections already made with the unusual search: validated, never
		// estimated.
		{
			resetScene();
			boardScene().boardless = {5, 6};
			const media::FrameSequence frames = footage(boardScene().frames);
			std::vector<cb::DetectionReport> detections;
			for (std::size_t i = 0; i < frames.size(); ++i)
				detections.push_back(cb::detect(frames.image(i), frames.frame(i), specification(), unusualSearch()));
			CameraModelParameters held = trueCamera();
			held.intrinsics.fx *= 1.001;
			held.intrinsics.fy *= 1.0005;
			Request request;
			request.imported = *CameraModel::create(held).model;
			request.importedPolicy = ImportedModelPolicy::HoldAndValidate;
			request.execution = ExecutionPolicy::DeterministicDebug;
			text += section("held", method::calibrate(detections, trueCamera().image, specification(), request));
		}

		// Refused before anything is detected: a profile this build does not know.
		{
			resetScene();
			Request request;
			request.fitnessProfile = "reconstruction/99";
			text += section("refused", method::calibrate(footage(boardScene().frames), specification(), unusualSearch(), request));
		}

		return text;
	}
} // namespace

TEST_CASE("a board calibration report is exactly the committed golden", "[camera][calibration][golden]")
{
	requireGolden("boardcalibration.txt", produce());
}
