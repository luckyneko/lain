// A targetless registration report, every field of it, printed and compared with a committed golden
// (golden/targetlessregistration.txt). It holds targetless registration still while its incremental
// reconstruction moves to a module calibration can share (M9 slice 4, sub-slice 6): a change that only
// moves code must leave this text byte-identical. Timings are left out. How it prints and compares is
// goldentext.h's, shared with the other method goldens.
//
// It runs over the stand-ins this executable registers and the pass-through refiner, so every result
// is initialisation's own, which is exactly what moves. Seven scenes, each for what it reaches: the
// seed and the placement, the bootstrap's path through them, a chain the closure cannot place, a
// component estimate, the cheirality filter, the parallax floor, and the hold-out guard's reading of
// the closure. The labels are the method-neutral names; there is no earlier text to stay equal to.

#include "goldentext.h"
#include "passthroughrefiner.h"
#include "syntheticscene.h"

#include <lain/camera/registration/targetless.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

using namespace lain;
using namespace lain::camera;
using namespace lain::camera::registration;
using namespace lain::camera::testing::golden;
namespace scene = lain::camera::testing::scene;
namespace standin = lain::camera::testing;

namespace
{
	std::string names(const std::vector<capture::CameraIdentity>& ids)
	{
		std::string out;
		for (const capture::CameraIdentity& id : ids)
			out += " " + id.value;
		return out;
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
		out << "thresholds " << r.thresholds.name << " unit " << name(r.thresholds.unit) << " ready("
			<< thresholds(r.thresholds.ready) << ") exploratory(" << thresholds(r.thresholds.exploratory) << ")\n";

		if (const auto* h = std::get_if<HeldOutEvidence>(&r.heldOut))
		{
			out << "heldOut units " << h->units << " predictions " << h->predictions << " unpredicted " << h->unpredicted
				<< " residuals " << h->residuals << " epipolar " << h->epipolar << " rmsAngle " << number(h->rmsAngle)
				<< " rmsPixels " << number(h->rmsPixels) << " worstPixels " << number(h->worstPixels) << "\n";
			out << "heldOut robustAngle " << number(h->robustAngle) << "\n";
			for (const HeldOutCamera& c : h->perCamera)
				out << "heldOut camera " << heldOutCamera(c) << "\n";
		}
		else
			out << "heldOut unavailable: " << std::get<Unavailable>(r.heldOut).reason << "\n";
		if (const auto* s = std::get_if<ResamplingEvidence>(&r.resampling))
			out << "resampling " << s->resamples << " rotation " << number(s->rotationVariation) << " translation "
				<< number(s->translationVariation) << " absolute " << number(s->translationVariationAbsolute) << " worst "
				<< worstCamera(s->rotationVariation, s->worstCamera.value) << "\n";
		else
			out << "resampling unavailable: " << std::get<Unavailable>(r.resampling).reason << "\n";

		const Diagnostics& d = r.diagnostics;
		for (const GraphEdgeReport& e : d.edges)
			out << "edge " << edge(e) << "\n";
		for (const auto& component : d.components)
			out << "component" << names(component) << "\n";
		for (const GraphEdgeReport& e : d.weakBridges)
			out << "weakBridge " << edge(e) << "\n";
		for (const ComponentEstimate& estimate : d.componentEstimates)
		{
			out << "componentEstimate " << estimate.reference.value << "\n";
			for (const RegisteredCamera& c : estimate.cameras)
				out << "componentEstimate camera " << c.camera.value << " " << transform(c.referenceFromCamera) << "\n";
		}
		for (const Outlier& o : d.outliers)
			out << "outlier " << o.unit.substr(0, 12) << " " << o.camera.value << " " << number(o.rmsWhitened) << "\n";
		if (d.refinement)
		{
			const RefinementSummary& s = *d.refinement;
			out << "refinement " << name(s.status) << " (" << s.detail << ") iterations " << s.iterations << " cost "
				<< number(s.initialCost) << " -> " << number(s.finalCost) << " residuals " << s.residuals << " bodies "
				<< s.bodies << " landmarks " << s.landmarks << " noise " << number(s.noise.pixelSigma) << " loss "
				<< name(s.loss.family) << " " << number(s.loss.scale) << "\n";
		}
		else
			out << "refinement none\n";
		for (const CameraEvidence& c : d.cameras)
			out << "cameraEvidence " << c.camera.value << " " << name(c.applicability) << " shared " << c.shared
				<< " residuals " << c.residuals << " outliers " << c.outliers << " rmsPixels " << number(c.rmsPixels)
				<< " rmsAngle " << number(c.rmsAngle) << "\n";
		for (const std::string& u : d.heldOutUnits)
			out << "heldOutUnit " << u.substr(0, 12) << "\n";
		out << "medianDepth " << number(d.medianDepth) << "\n";

		// Over a track set nothing was extracted, so both optionals print as unset; no scene here
		// extracts, so how a set one would print is not settled by this golden.
		const TargetlessDiagnostics& t = std::get<TargetlessDiagnostics>(d.method);
		out << "targetless tracks " << t.tracks << " usable " << t.tracksUsable << " landmarks " << t.landmarks
			<< " untriangulated " << t.untriangulated << " behind " << t.behind << "\n";
		out << "targetless seed" << names(t.seed) << " angle " << number(t.seedAngle) << " belowFloor " << t.seedBelowFloor
			<< "\n";
		out << "targetless placement" << names(t.placement) << "\n";
		out << "targetless unplaceable" << names(t.unplaceable) << "\n";
		out << "targetless extraction " << (t.extraction ? "set" : "unset") << " trackSet " << (t.trackSet ? "set" : "unset")
			<< "\n";

		const Reproducibility& p = r.reproducibility;
		const TargetlessRecord& record = std::get<TargetlessRecord>(p.method);
		for (const std::string& source : p.sources)
			out << "source " << source << "\n";
		out << "frames " << p.frames << " geometry " << provenance(record.geometry) << " extractor "
			<< provenance(record.extractor) << " matcher " << provenance(record.matcher) << " refiner "
			<< provenance(p.refiner) << " extraction " << (record.extraction ? "set" : "unset") << "\n";
		const Request& q = p.request;
		out << "request reference " << (q.reference ? q.reference->value : "unset") << " unknownApplicability "
			<< name(q.unknownApplicability) << " noise " << number(q.noise.pixelSigma) << " loss " << name(q.loss.family)
			<< " " << number(q.loss.scale) << " profile "
			<< (q.fitnessProfile.empty() ? std::string("(method's)") : q.fitnessProfile) << "\n";
		const FitnessOverrides& o = q.overrides;
		out << "request overrides " << optional(o.minimumSharedPerCamera) << " " << optional(o.minimumBridgeShared) << " "
			<< optional(o.maximumHeldOutAngle) << " " << optional(o.maximumRotationVariation) << " "
			<< optional(o.maximumTranslationVariation) << " " << optional(o.maximumOutlierFraction) << " "
			<< optional(o.minimumHeldOutPerCamera) << "\n";
		out << "request heldOutFraction " << number(q.heldOutFraction) << " resamples " << q.resamples << " seed " << q.seed
			<< " maximumIterations " << q.maximumIterations << " execution " << name(q.execution) << "\n";
		return out.str();
	}

	void setUp(std::size_t cameras, std::size_t landmarks)
	{
		scene::registerStandIns();
		standin::registerPassThroughRefiner();
		scene::reset(cameras, landmarks);
	}

	std::vector<RigCamera> rig()
	{
		std::vector<RigCamera> out;
		for (std::size_t c = 0; c < scene::scene().referenceFromCamera.size(); ++c)
			out.push_back({capture::CameraIdentity{scene::identityOf(c)}, scene::model()});
		return out;
	}

	// Every landmark from `from` to `to` seen only by `cameras`.
	void seenOnlyBy(std::size_t from, std::size_t to, const std::set<std::size_t>& cameras)
	{
		for (std::size_t l = from; l < to; ++l)
			scene::scene().landmarks[l].cameras = cameras;
	}

	std::string run(const char* title, const feature::TrackSet& set, const Request& request = {})
	{
		return "== " + std::string(title) + "\n" + describe(registration::targetless::registerCameras(rig(), set, request));
	}

	std::string produce()
	{
		std::string text;

		// 1. Four cameras on a static scene, exact.
		setUp(4, 300);
		text += run("connected", scene::trackSetOf());

		// 2. The same through noise, with a requested reference, Huber's loss, a quarter held out and a
		// few resamples, so the bootstrap's path through initialisation is printed too.
		setUp(4, 300);
		Request noisy;
		noisy.reference = capture::CameraIdentity{"cam02"};
		noisy.loss.family = LossFamily::Huber;
		noisy.heldOutFraction = 0.25;
		noisy.resamples = 6;
		noisy.seed = 17;
		text += run("noisy", scene::trackSetOf(0.3, 5), noisy);

		// 3. A chain of pairs: connected, but no camera beyond the seed's pair can be placed.
		setUp(4, 150);
		seenOnlyBy(0, 50, {0, 1});
		seenOnlyBy(50, 100, {1, 2});
		seenOnlyBy(100, 150, {2, 3});
		text += run("chain", scene::trackSetOf());

		// 4. Two parts that never shared a landmark: each gets an estimate of its own.
		setUp(4, 200);
		seenOnlyBy(0, 100, {0, 1});
		seenOnlyBy(100, 200, {2, 3});
		text += run("disconnected", scene::trackSetOf());

		// 5. Camera 3 inside the scene facing away, with a wrong match behind it: the cheirality filter.
		setUp(4, 200);
		scene::scene().referenceFromCamera[3] = scene::lookingAt(math::Vec3d{0.05, -0.1, -0.2}, math::Vec3d{0.0, 0.0, 3.0});
		scene::scene().renderedFrom = scene::scene().referenceFromCamera;
		feature::TrackSet behind = scene::trackSetOf();
		feature::Track* wrong = nullptr;
		for (std::size_t u = 0; u < behind.tracks.size() && !wrong; ++u)
		{
			const std::size_t l = std::stoul(behind.tracks[u].identity.substr(5));
			const math::Vec3d inThree =
				scene::scene().referenceFromCamera[3].inverse().apply(scene::scene().landmarks[l].point);
			if (inThree.z < 0 && u % 5 != 2 && behind.tracks[u].observations.back().view != 3)
				wrong = &behind.tracks[u];
		}
		REQUIRE(wrong != nullptr);
		wrong->observations.push_back({3, math::Vec2d{320.0, 240.0}, 1, wrong->observations.front().covariance});
		text += run("behind", behind);

		// 6. Three cameras 3 cm apart, 3 m from the scene: no pair clears the parallax floor.
		setUp(3, 200);
		for (std::size_t c = 0; c < 3; ++c)
			scene::scene().referenceFromCamera[c] =
				scene::lookingAt(math::Vec3d{0.03 * double(c), -0.2, -3.0}, math::Vec3d{0.0, 0.0, 0.0});
		scene::scene().renderedFrom = scene::scene().referenceFromCamera;
		text += run("narrow", scene::trackSetOf());

		// 7. The design review's 38 tracks: cameras 0 and 1 share 20 and place nobody else, and the 8
		// tracks that place them from 2 and 3 sit where the hold-out stride lands, kept only by the guard.
		setUp(4, 38);
		const std::vector<std::size_t> withZero{2, 7, 12, 17}, withOne{22, 27, 32, 37};
		for (std::size_t l = 0; l < 38; ++l)
			scene::scene().landmarks[l].cameras = { 0,
													1 };
		std::size_t xy = 0;
		for (std::size_t l = 0; l < 38 && xy < 10; ++l)
		{
			if (std::count(withZero.begin(), withZero.end(), l) == 0 && std::count(withOne.begin(), withOne.end(), l) == 0)
			{
				scene::scene().landmarks[l].cameras = { 2,
														3 };
				++xy;
			}
		}
		for (const std::size_t l : withZero)
			scene::scene().landmarks[l].cameras = { 0,
													2,
													3 };
		for (const std::size_t l : withOne)
			scene::scene().landmarks[l].cameras = { 1,
													2,
													3 };
		text += run("spare", scene::trackSetOf());

		return text;
	}
} // namespace

TEST_CASE("a targetless registration report is exactly the committed golden", "[camera][registration][targetless][golden]")
{
	requireGolden("targetlessregistration.txt", produce());
}
