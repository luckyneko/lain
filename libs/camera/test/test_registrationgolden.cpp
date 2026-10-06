// A board registration report, every field of it, printed and compared with a committed golden
// (golden/boardregistration.txt). It holds the registration contracts still while they are made
// method-neutral (M9 slice 3, sub-slices 1 to 3): a change that moves only where a field lives must
// leave this text byte-identical, and a change to what is computed cannot. Timings are left out.
//
// Numbers are snapped to a 1e-9 grid, then printed to nine significant digits. That hides the last
// bits in which two platforms' maths libraries differ (an exact rig's residuals are 1e-13 or so,
// and nothing here is near a grid boundary), while any change of computation moves them visibly.
//
// When the text differs, the test writes what it produced beside the build and says where. To
// accept a deliberate change, copy that file over the golden and commit it with the change.
//
// The labels are the golden's own, and some name fields that have since been renamed (sub-slice 3's
// "groups" for units, "corners" for residuals, "metres" for the absolute translation variation):
// they stay, so the text is byte-identical across the renames.

#include "syntheticrig.h"

#include <lain/camera/registration/board.h>
#include <lain/meta/enums.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <variant>

using namespace lain;
using namespace lain::camera;
using namespace lain::camera::registration;
using namespace lain::camera::testing;
namespace method = lain::camera::registration::board;
namespace cb = lain::camera::board;

namespace
{
	const bool kPassThrough = (rig::registerPassThroughRefiner(), true);

	std::string number(double x)
	{
		if (std::isnan(x))
			return "nan";
		if (std::isinf(x))
			return x > 0 ? "inf" : "-inf";
		double snapped = std::abs(x) < 1e6 ? std::round(x * 1e9) / 1e9 : x;
		if (snapped == 0)
			snapped = 0; // never "-0"
		std::ostringstream text;
		text.imbue(std::locale::classic());
		text << std::setprecision(9) << snapped;
		return text.str();
	}

	template <typename E>
	std::string name(E value)
	{
		return std::string(meta::enums::name(value));
	}

	std::string transform(const math::RigidTransformd& t)
	{
		const math::Quatd q = t.rotation();
		const math::Vec3d p = t.translation();
		return "q(" + number(q.w) + " " + number(q.x) + " " + number(q.y) + " " + number(q.z) + ") t(" + number(p.x) + " " +
			   number(p.y) + " " + number(p.z) + ")";
	}

	std::string provenance(const Provenance& p)
	{
		return p.backend + "/" + p.version;
	}

	std::string scale(const cb::ScalePolicy& policy)
	{
		if (const auto* f = std::get_if<cb::ScaleFactor>(&policy))
			return "factor " + number(f->factor);
		if (const auto* l = std::get_if<cb::LongestSide>(&policy))
			return "longest " + std::to_string(l->pixels);
		return "native";
	}

	std::string detectionRequest(const cb::DetectionRequest& r)
	{
		return scale(r.scale) + ", refine native " + std::to_string(r.refineAtNativeResolution) + ", detail " +
			   name(r.detail) + ", minimum corners " + std::to_string(r.minimumCorners);
	}

	template <typename T>
	std::string optional(const std::optional<T>& value)
	{
		if (!value)
			return "unset";
		if constexpr (std::is_floating_point_v<T>)
			return number(*value);
		else
			return std::to_string(*value);
	}

	std::string thresholds(const FitnessThresholds& t)
	{
		return std::to_string(t.minimumSharedPerCamera) + " " + std::to_string(t.minimumBridgeShared) + " " +
			   number(t.maximumHeldOutAngle) + " " + number(t.maximumRotationVariation) + " " +
			   number(t.maximumTranslationVariation) + " " + number(t.maximumOutlierFraction);
	}

	std::string edge(const GraphEdgeReport& e)
	{
		return e.a.value + "-" + e.b.value + " shared " + std::to_string(e.shared) + " bridge " + std::to_string(e.bridge);
	}

	// One detection, compactly: its outcome, and its corners as sums, which change if any corner does.
	std::string detection(const cb::DetectionReport& d)
	{
		std::string text = name(d.status) + " " + name(d.refinement) + " by " + provenance(d.provenance);
		text += " stats(" + std::to_string(d.stats.markersFound) + " " + std::to_string(d.stats.markersRejected) + " " +
				std::to_string(d.stats.cornersFound) + " " + std::to_string(d.stats.cornersExpected) + ")";
		text += " transform(" + number(d.transform.scaleX) + " " + number(d.transform.scaleY) + ")";
		text += " request(" + detectionRequest(d.request) + ")";
		for (const cb::RejectionReason& r : d.rejections)
			text += " rejected(" + name(r.reason) + ": " + r.detail + ")";
		text += " evidence " + std::to_string(d.evidence.has_value());
		if (d.observation)
		{
			const cb::Observation& o = *d.observation;
			double ids = 0, u = 0, v = 0, cov = 0;
			for (const cb::FeatureObservation& f : o.features)
			{
				ids += f.id;
				u += f.pixel.x;
				v += f.pixel.y;
				if (f.covariance)
					cov += (*f.covariance)[0] + (*f.covariance)[1] + (*f.covariance)[2];
			}
			text += " observation(" + o.frame.toString() + ", " + std::to_string(o.image.width) + "x" +
					std::to_string(o.image.height) + ", pattern " + o.pattern.toString().substr(0, 12) + ", " +
					std::to_string(o.features.size()) + " corners, ids " + number(ids) + ", u " + number(u) + ", v " +
					number(v) + ", covariance " + number(cov) + ")";
		}
		return text;
	}

	std::string describe(const Report& r)
	{
		std::ostringstream out;
		out << "status " << name(r.status) << "\n";
		for (const FailureReason& f : r.failures)
			out << "failure " << name(f.failure) << ": " << f.detail << "\n";
		out << "reference " << (r.reference ? r.reference->value : "unset") << " requested " << r.referenceRequested << "\n";
		for (const RegisteredCamera& c : r.cameras)
			out << "camera " << c.camera.value << " " << transform(c.referenceFromCamera) << "\n";
		out << "scale " << name(r.scale.scale) << (r.scale.evidence.empty() ? "" : ": " + r.scale.evidence) << "\n";
		out << "verdict " << name(r.verdict) << "\n";
		for (const std::string& note : r.fitnessNotes)
			out << "note " << note << "\n";
		out << "thresholds " << r.thresholds.name << " ready(" << thresholds(r.thresholds.ready) << ") exploratory("
			<< thresholds(r.thresholds.exploratory) << ")\n";

		if (const auto* h = std::get_if<HeldOutEvidence>(&r.heldOut))
		{
			out << "heldOut groups " << h->units << " predictions " << h->predictions << " unpredicted " << h->unpredicted
				<< " corners " << h->residuals << " rmsAngle " << number(h->rmsAngle) << " rmsPixels " << number(h->rmsPixels)
				<< " worstPixels " << number(h->worstPixels) << "\n";
			for (const auto& [camera, angle] : h->perCamera)
				out << "heldOut camera " << camera.value << " " << number(angle) << "\n";
		}
		else
			out << "heldOut unavailable: " << std::get<Unavailable>(r.heldOut).reason << "\n";
		if (const auto* s = std::get_if<ResamplingEvidence>(&r.resampling))
			out << "resampling " << s->resamples << " rotation " << number(s->rotationVariation) << " translation "
				<< number(s->translationVariation) << " metres " << number(s->translationVariationAbsolute) << " worst "
				<< s->worstCamera.value << "\n";
		else
			out << "resampling unavailable: " << std::get<Unavailable>(r.resampling).reason << "\n";

		const Diagnostics& d = r.diagnostics;
		const BoardDiagnostics& b = std::get<BoardDiagnostics>(d.method);
		out << "diagnostics.groupsExamined " << b.groupsExamined << " groupsUsable " << b.groupsUsable << " observations "
			<< b.observations << " observationsWithoutPose " << b.withoutPose << "\n";
		for (const GraphEdgeReport& e : d.edges)
			out << "edge " << edge(e) << "\n";
		for (const auto& component : d.components)
		{
			out << "component";
			for (const capture::CameraIdentity& c : component)
				out << " " << c.value;
			out << "\n";
		}
		for (const GraphEdgeReport& e : d.weakBridges)
			out << "weakBridge " << edge(e) << "\n";
		for (const ComponentEstimate& estimate : d.componentEstimates)
		{
			out << "componentEstimate " << estimate.reference.value << "\n";
			for (const RegisteredCamera& c : estimate.cameras)
				out << "componentEstimate camera " << c.camera.value << " " << transform(c.referenceFromCamera) << "\n";
		}
		for (const FlipChoice& f : b.flips)
			out << "flip " << f.group.substr(0, 12) << " " << f.camera.value << "\n";
		for (const Outlier& o : d.outliers)
			out << "outlier " << o.unit.substr(0, 12) << " " << o.camera.value << " " << number(o.rmsWhitened) << "\n";
		if (d.refinement)
		{
			const RefinementSummary& s = *d.refinement;
			out << "refinement " << name(s.status) << " (" << s.detail << ") iterations " << s.iterations << " cost "
				<< number(s.initialCost) << " -> " << number(s.finalCost) << " corners " << s.residuals << " bodies "
				<< s.bodies << " noise " << number(s.noise.pixelSigma) << " loss " << name(s.loss.family) << " "
				<< number(s.loss.scale) << "\n";
		}
		else
			out << "refinement none\n";
		for (const CameraEvidence& c : d.cameras)
			out << "cameraEvidence " << c.camera.value << " " << name(c.applicability) << " groups " << c.shared << " corners "
				<< c.residuals << " outliers " << c.outliers << " rmsPixels " << number(c.rmsPixels) << " rmsAngle "
				<< number(c.rmsAngle) << "\n";
		for (const std::string& g : d.heldOutUnits)
			out << "heldOutGroup " << g.substr(0, 12) << "\n";
		out << "medianDepth " << number(d.medianDepth) << "\n";
		for (const GroupDetections& g : b.detections)
		{
			for (const auto& [camera, report] : g.members)
				out << "detection " << g.group.substr(0, 12) << " " << camera.value << " " << detection(report) << "\n";
		}

		const Reproducibility& p = r.reproducibility;
		const BoardRecord& record = std::get<BoardRecord>(p.method);
		for (const std::string& source : p.sources)
			out << "source " << source << "\n";
		out << "frames " << p.frames << " detector " << provenance(record.detector) << " poseSolver "
			<< provenance(record.poseSolver) << " refiner " << provenance(p.refiner) << "\n";
		const Request& q = p.request;
		out << "request reference " << (q.reference ? q.reference->value : "unset") << " unknownApplicability "
			<< name(q.unknownApplicability) << " noise " << number(q.noise.pixelSigma) << " loss " << name(q.loss.family)
			<< " " << number(q.loss.scale) << " profile " << q.fitnessProfile << "\n";
		const FitnessOverrides& o = q.overrides;
		out << "request overrides " << optional(o.minimumSharedPerCamera) << " " << optional(o.minimumBridgeShared) << " "
			<< optional(o.maximumHeldOutAngle) << " " << optional(o.maximumRotationVariation) << " "
			<< optional(o.maximumTranslationVariation) << " " << optional(o.maximumOutlierFraction) << "\n";
		// Printed where it was when the request held it (sub-slice 2 moved it to the board record).
		out << "request detection " << (record.detection ? detectionRequest(*record.detection) : "unset") << "\n";
		out << "request heldOutFraction " << number(q.heldOutFraction) << " resamples " << q.resamples << " seed " << q.seed
			<< " maximumIterations " << q.maximumIterations << " execution " << name(q.execution) << "\n";
		return out.str();
	}

	std::string run(const char* title, const Request& request)
	{
		const Report report = method::registerCameras(rig::cameras(), rig::groups(), specification(), request);
		return "== " + std::string(title) + "\n" + describe(report);
	}

	// Four rigs, each noisy, so a different choice anywhere in initialisation, seeding, hold-out or
	// resampling changes numbers here rather than picking an equally exact answer.
	std::string produce()
	{
		std::string text;

		// Forty groups, so an edge shares more groups than initialisation samples.
		rig::reset(4, 40);
		rig::scene().poseNoise = 0.002;
		rig::scene().pixelNoiseX = 0.3;
		rig::scene().pixelNoiseY = 0.2;
		text += run("connected", Request{});

		// Camera 3's solver ranks the other pose first everywhere; a requested reference.
		rig::reset();
		rig::scene().poseNoise = 0.002;
		for (std::size_t g = 0; g < 24; ++g)
			rig::scene().flipped.insert({3, g});
		Request flipped;
		flipped.reference = capture::CameraIdentity{"cam02"};
		flipped.overrides.minimumSharedPerCamera = 30;
		text += run("flipped", flipped);

		// A frame from another instant, with measured covariance reported and Huber's loss.
		rig::reset();
		rig::scene().poseNoise = 0.002;
		rig::scene().pixelNoiseX = 0.25;
		rig::scene().pixelNoiseY = 0.25;
		rig::scene().reportCovariance = true;
		const math::RigidTransformd& actual = rig::scene().referenceFromBoard[5];
		rig::scene().elsewhere[{2, 5}] =
			math::RigidTransformd{math::angleAxis(0.2, math::Vec3d{0, 0, 1}), math::Vec3d{0.06, 0, 0}} * actual;
		Request stray;
		stray.loss.family = LossFamily::Huber;
		stray.heldOutFraction = 0.25;
		stray.resamples = 6;
		stray.seed = 17;
		text += run("stray", stray);

		// Two pairs that never saw the board together: each component placed as a diagnostic.
		rig::reset();
		rig::scene().poseNoise = 0.002;
		rig::seenBy(0, 12, {0, 1});
		rig::seenBy(12, 24, {2, 3});
		text += run("disconnected", Request{});

		return text;
	}

	std::string firstDifference(const std::string& expected, const std::string& actual)
	{
		std::istringstream a(expected), b(actual);
		std::string la, lb;
		for (std::size_t line = 1;; ++line)
		{
			const bool ha = static_cast<bool>(std::getline(a, la));
			const bool hb = static_cast<bool>(std::getline(b, lb));
			if (!ha && !hb)
				return "none";
			if (!ha || !hb || la != lb)
				return "line " + std::to_string(line) + ":\n  golden:   " + (ha ? la : "<end>") + "\n  produced: " + (hb ? lb : "<end>");
		}
	}
} // namespace

TEST_CASE("a board registration report is exactly the committed golden", "[camera][registration][golden]")
{
	const std::string produced = produce();

	const std::string golden = std::string(LAIN_CAMERA_GOLDEN_DIR) + "/boardregistration.txt";
	std::ifstream in(golden, std::ios::binary);
	std::ostringstream expected;
	expected << in.rdbuf();

	if (expected.str() != produced)
	{
		const std::string written = std::string(LAIN_CAMERA_GOLDEN_OUT) + "/boardregistration.txt";
		std::ofstream(written, std::ios::binary) << produced;
		FAIL("the report differs from " << golden << " at " << firstDifference(expected.str(), produced)
										<< "\nwhat was produced is in " << written
										<< "; copy it over the golden only for a deliberate change");
	}
}
