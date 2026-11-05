// The calibration fallback as a graph (ADR-0016): calibrationMethod switches a Gate in front of each
// method's node, nextAttempt switches the targetless gate from the board's report, calibrationAttempts
// combines what was attempted, and cameraModel takes the accepted attempt's model. Driven through the
// production nodes and scheduler over one retained Evaluation, as a host runs a document.
//
// Both stand-in worlds, in their own executable: the board method module's (syntheticboard.h: a
// detector, an estimator and a pose solver answering from a known truth) and the targetless method
// module's (syntheticscene.h: an extractor, a matcher and a geometry solver answering from a known
// moving camera, with a refiner handing back its start, passthroughrefiner.h). One footage serves
// both: the scene's frames at the board camera's geometry. The board stand-ins never read a pixel, and
// the scene's are a pinhole of that geometry, since its fundamental matrices ignore distortion.
//
// What ran is read from the stand-ins' own logs: the board estimator's calls, and the refinements the
// targetless method asked for and its matcher's searches. A method that is not needed must not run.

#include "passthroughrefiner.h"
#include "syntheticboard.h"
#include "syntheticscene.h"

#include <lain/camera/calibration/attempts.h>
#include <lain/camera/flow/boardspecificationnode.h>
#include <lain/camera/flow/calibratecameranode.h>
#include <lain/camera/flow/calibratecameratargetlessnode.h>
#include <lain/camera/flow/calibrationattemptsnode.h>
#include <lain/camera/flow/calibrationmethodnode.h>
#include <lain/camera/flow/cameramodelnode.h>
#include <lain/camera/flow/nextattemptnode.h>
#include <lain/camera/flow/register.h>
#include <lain/flow/boundary.h>
#include <lain/flow/edit.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/graph.h>
#include <lain/flow/nodes/gate.h>
#include <lain/flow/scheduler.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <mutex>
#include <string>
#include <variant>

using namespace lain;
namespace scene = lain::camera::testing::scene;
namespace standin = lain::camera::testing;
using camera::CameraModel;
using camera::calibration::Attempts;
using camera::calibration::Method;
using camera::calibration::Report;
using camera::calibration::Verdict;

namespace
{
	// Frames of footage, and landmarks the scene's camera wanders through. Enough for a targetless
	// attempt to be Ready from an imported model the refiner hands back, with room: measured, 13 frames
	// fitted against Ready's 10, the image covered whole against its 0.6, none unplaced, and the
	// held-out residuals exact, since the model is the truth. At 24 frames it fits 19, at three times
	// the cost.
	constexpr std::size_t kFrames = 16;
	constexpr std::size_t kLandmarks = 240;

	// The board camera's geometry and intrinsics, as a pinhole: what the scene's camera sees through.
	CameraModel pinhole()
	{
		camera::CameraModelParameters p = standin::trueCamera();
		p.distortion = camera::NoDistortion{};
		return *CameraModel::create(p).model;
	}

	void setUp()
	{
		standin::resetScene(); // the board world, its estimator answering the true camera
		// The board detector poses frame k at boardPose(k, frames), so its count is the footage's.
		standin::boardScene().frames = kFrames;
		scene::registerStandIns();
		standin::registerPassThroughRefiner();
		scene::resetWandering(kFrames, kLandmarks, 1, pinhole());
		camera::registerCameraPortTypes(); // so a report pin can be added by its key
		std::lock_guard<std::mutex> lock(standin::refinerScript().mutex);
		standin::refinerScript().asked.clear();
		standin::refinerScript().estimates = {camera::DistortionModel::None};
		standin::refinerScript().finalCost = nullptr;
	}

	template <typename T>
	void setParam(flow::Graph& graph, flow::NodeId id, const std::string& name, T value)
	{
		flow::Node& node = graph.node(id);
		for (std::size_t i = 0; i < node.paramCount(); ++i)
		{
			if (node.param(i).name() == name)
			{
				REQUIRE(node.setParam(node.param(i).id(), value));
				return;
			}
		}
		FAIL("no parameter named " << name);
	}

	flow::PortAddress output(const flow::Graph& graph, flow::NodeId id, std::size_t index = 0)
	{
		return {id, graph.node(id).output(index).id()};
	}

	flow::PortAddress input(const flow::Graph& graph, flow::NodeId id, std::size_t index)
	{
		return {id, graph.node(id).input(index).id()};
	}

	void connect(flow::Graph& graph, flow::PortAddress from, flow::PortAddress to)
	{
		REQUIRE(graph.connect(from, to) == flow::Connection::Ok);
	}

	// A report pin on an attempts node, added as the canvas "+" adds one.
	flow::PortAddress reportPin(flow::Graph& graph, flow::NodeId id, const std::string& name)
	{
		const flow::PortId pin = flow::edit::addPort(graph, id, "CalibrationReport", name);
		REQUIRE(pin != flow::PortId{});
		return {id, pin};
	}

	// The fallback, wired as a document would wire it:
	//
	//   footage -> Gate(method.board) -> calibrateCamera (board: a 24 mm specification) -> report
	//   footage -> Gate(nextAttempt.run) -> calibrateCameraTargetless (imported) -> report
	//   nextAttempt: included = method.targetless, its one pin the board's report
	//   calibrationAttempts: the board's report, then the targetless one -> accepted -> cameraModel
	struct Fallback
	{
		flow::Graph graph;
		flow::NodeId method, board, targetless, next, attempts, model;
		flow::PortAddress footage, imported;

		Fallback()
		{
			method = graph.add<camera::CalibrationMethodNode>();
			const flow::NodeId boardGate = graph.add<flow::GateNode>(flow::portType<media::FrameSequence>());
			const flow::NodeId targetlessGate = graph.add<flow::GateNode>(flow::portType<media::FrameSequence>());
			const flow::NodeId spec = graph.add<camera::BoardSpecificationNode>();
			setParam(graph, spec, "squareLengthMm", 24.0f);
			board = graph.add<camera::CalibrateCameraNode>();
			targetless = graph.add<camera::CalibrateCameraTargetlessNode>();
			next = graph.add<camera::NextAttemptNode>();
			attempts = graph.add<camera::CalibrationAttemptsNode>();
			model = graph.add<camera::CameraModelNode>();

			flow::GroupInputNode& in = graph.boundaryInputNode();
			footage = {in.id(), in.addBoundary<media::FrameSequence>("footage")};
			imported = {in.id(), in.addBoundary<CameraModel>("imported")};

			connect(graph, output(graph, method, 0), input(graph, boardGate, 0)); // board -> enable
			connect(graph, footage, input(graph, boardGate, 1));
			connect(graph, output(graph, boardGate), input(graph, board, 0));
			connect(graph, output(graph, spec), input(graph, board, 1));

			connect(graph, output(graph, method, 1), input(graph, next, 0)); // targetless -> included
			connect(graph, output(graph, board), reportPin(graph, next, "in1"));
			connect(graph, output(graph, next), input(graph, targetlessGate, 0)); // run -> enable
			connect(graph, footage, input(graph, targetlessGate, 1));
			connect(graph, output(graph, targetlessGate), input(graph, targetless, 0));
			connect(graph, imported, input(graph, targetless, 1));

			connect(graph, output(graph, board), reportPin(graph, attempts, "in0"));
			connect(graph, output(graph, targetless), reportPin(graph, attempts, "in1"));
			connect(graph, output(graph, attempts, 1), input(graph, model, 0)); // accepted -> report

			// The targetless attempt starts from the pinhole and estimates no distortion, which the
			// refiner hands back unchanged: Ready, when it runs.
			setParam(graph, targetless, "model", camera::DistortionModel::None);
			setParam(graph, targetless, "importedPolicy", camera::calibration::ImportedModelPolicy::Initial);
		}
	};

	// What the stand-ins were asked so far.
	struct Ran
	{
		std::size_t estimates = 0;	 // the board estimator's calls
		std::size_t refinements = 0; // the targetless method's
		std::size_t searches = 0;	 // the targetless extraction's matcher
	};
	Ran ran()
	{
		Ran r;
		{
			std::lock_guard<std::mutex> lock(standin::script().mutex);
			r.estimates = standin::script().estimates;
		}
		{
			std::lock_guard<std::mutex> lock(standin::refinerScript().mutex);
			r.refinements = standin::refinerScript().asked.size();
		}
		std::lock_guard<std::mutex> lock(scene::logMutex());
		r.searches = scene::scene().nearestCalls.size();
		return r;
	}

	// The fallback's evaluation, retained across runs, its footage and imported model bound.
	struct Run
	{
		Fallback& fallback;
		flow::Evaluation evaluation;

		explicit Run(Fallback& f, bool bindFootage = true)
			: fallback(f)
			, evaluation(f.graph)
		{
			if (bindFootage)
			{
				flow::PortValue footage;
				footage.set(scene::sequenceFootage().footage);
				evaluation.bind(f.footage, std::move(footage));
			}
			flow::PortValue imported;
			imported.set(pinhole());
			evaluation.bind(f.imported, std::move(imported));
		}

		void operator()() { flow::SerialScheduler{}.run(fallback.graph, evaluation); }

		const flow::PortValue& value(flow::NodeId id, std::size_t index = 0) const
		{
			return evaluation.value(output(fallback.graph, id, index));
		}
		const Attempts& attempts() const
		{
			const flow::PortValue& v = value(fallback.attempts, 0);
			REQUIRE(v.holds<Attempts>());
			return v.get<Attempts>();
		}
		const flow::PortValue& accepted() const { return value(fallback.attempts, 1); }
		const flow::PortValue& model() const { return value(fallback.model); }
	};

	bool isTargetlessReport(const Report& report)
	{
		return std::holds_alternative<camera::calibration::TargetlessRecord>(report.reproducibility.method);
	}

	bool failedWith(const Report& report, camera::calibration::Failure failure)
	{
		return report.status == camera::calibration::CalibrationStatus::Failed && !report.failures.empty() &&
			   report.failures.front().failure == failure;
	}
} // namespace

TEST_CASE("the fallback's attempts are the ones its method includes", "[camera][flow][fallback]")
{
	setUp();
	Fallback fallback;
	Run run{fallback};

	// Board alone: the targetless method is not included, so never runs.
	setParam(fallback.graph, fallback.method, "method", Method::Board);
	run();
	Ran before = ran();
	CHECK(before.estimates > 0);
	CHECK(before.refinements == 0);
	CHECK(before.searches == 0);
	REQUIRE(run.attempts().reports.size() == 1);
	CHECK_FALSE(isTargetlessReport(run.attempts().reports[0]));

	// Targetless alone: the board's gate is off, so its node does not run again, and the attempts hold
	// the targetless one alone. The board's report went with its node's readiness, so nextAttempt saw
	// no earlier attempt.
	setParam(fallback.graph, fallback.method, "method", Method::Targetless);
	run();
	Ran after = ran();
	CHECK(after.estimates == before.estimates);
	CHECK(after.refinements > before.refinements);
	CHECK(after.searches > before.searches);
	REQUIRE(run.attempts().reports.size() == 1);
	CHECK(isTargetlessReport(run.attempts().reports[0]));
	INFO(run.attempts().toString());
	CHECK(run.attempts().accepted() == std::optional<std::size_t>{0});

	// Board then targetless: the board runs, is accepted, and the targetless attempt is skipped, so it
	// is not listed.
	before = after;
	setParam(fallback.graph, fallback.method, "method", Method::BoardThenTargetless);
	run();
	after = ran();
	CHECK(after.estimates > before.estimates);
	CHECK(after.refinements == before.refinements);
	REQUIRE(run.attempts().reports.size() == 1);
	CHECK_FALSE(isTargetlessReport(run.attempts().reports[0]));
}

TEST_CASE("an accepted board attempt leaves the targetless method unrun", "[camera][flow][fallback]")
{
	setUp();
	Fallback fallback;
	Run run{fallback};
	run(); // BoardThenTargetless, calibrationMethod's default

	const Ran r = ran();
	CHECK(r.estimates > 0);
	CHECK(r.refinements == 0);
	CHECK(r.searches == 0);
	const Attempts& attempts = run.attempts();
	INFO(attempts.toString());
	REQUIRE(attempts.reports.size() == 1);
	CHECK(attempts.reports[0].verdict == Verdict::Ready);
	CHECK(attempts.accepted() == std::optional<std::size_t>{0});
	REQUIRE(run.model().holds<CameraModel>());
	// The board's model, which has the true camera's distortion, not the pinhole the scene sees through.
	CHECK(std::holds_alternative<camera::BrownConrady5>(run.model().get<CameraModel>().distortion()));
}

TEST_CASE("a board attempt that is only Exploratory is not accepted, so the targetless one runs",
		  "[camera][flow][fallback]")
{
	setUp();
	Fallback fallback;
	// Eight views where Ready needs ten: the board calibration succeeds, and is Exploratory on that
	// alone (its only note, measured).
	setParam(fallback.graph, fallback.board, "maximumViews", 8);
	Run run{fallback};
	run();

	const Attempts& attempts = run.attempts();
	INFO(attempts.toString());
	REQUIRE(attempts.reports.size() == 2);
	CHECK(attempts.reports[0].status == camera::calibration::CalibrationStatus::Succeeded);
	CHECK(attempts.reports[0].verdict == Verdict::Exploratory);
	CHECK(ran().refinements > 0);
	CHECK(isTargetlessReport(attempts.reports[1]));
	CHECK(attempts.reports[1].verdict == Verdict::Ready);
	CHECK(attempts.accepted() == std::optional<std::size_t>{1});
}

TEST_CASE("a failed board attempt falls back to a Ready targetless one", "[camera][flow][fallback]")
{
	setUp();
	Fallback fallback;
	// A model the board estimator cannot estimate: a failed report, not a substitute.
	setParam(fallback.graph, fallback.board, "model", camera::DistortionModel::KannalaBrandt4);
	Run run{fallback};
	run();

	const Attempts& attempts = run.attempts();
	INFO(attempts.toString());
	REQUIRE(attempts.reports.size() == 2);
	CHECK(failedWith(attempts.reports[0], camera::calibration::Failure::NoEstimator));
	CHECK(isTargetlessReport(attempts.reports[1]));
	CHECK(attempts.reports[1].verdict == Verdict::Ready);
	CHECK(attempts.accepted() == std::optional<std::size_t>{1});

	REQUIRE(run.accepted().holds<Report>());
	CHECK(isTargetlessReport(run.accepted().get<Report>()));
	// cameraModel gives the targetless model: the pinhole it started from and the refiner handed back.
	REQUIRE(run.model().holds<CameraModel>());
	const CameraModel& model = run.model().get<CameraModel>();
	CHECK(std::holds_alternative<camera::NoDistortion>(model.distortion()));
	CHECK(model.intrinsics().fx == pinhole().intrinsics().fx);
}

TEST_CASE("when every attempt fails, nothing is accepted and the attempts say why", "[camera][flow][fallback]")
{
	setUp();
	Fallback fallback;
	// The targetless attempt asks for a model the refiner cannot estimate, so it fails whenever it runs.
	setParam(fallback.graph, fallback.targetless, "model", camera::DistortionModel::BrownConrady5);
	Run run{fallback};

	// First the board is accepted, so a stale acceptance would show below.
	run();
	REQUIRE(run.accepted().holds<Report>());
	REQUIRE(run.model().holds<CameraModel>());

	setParam(fallback.graph, fallback.board, "model", camera::DistortionModel::KannalaBrandt4);
	run();
	const Attempts& attempts = run.attempts();
	INFO(attempts.toString());
	REQUIRE(attempts.reports.size() == 2);
	CHECK(failedWith(attempts.reports[0], camera::calibration::Failure::NoEstimator));
	CHECK(isTargetlessReport(attempts.reports[1]));
	CHECK(failedWith(attempts.reports[1], camera::calibration::Failure::NoEstimator));
	CHECK_FALSE(attempts.accepted().has_value());
	CHECK(run.accepted().empty());
	CHECK(run.model().empty()); // suppressed: no model to give
}

TEST_CASE("an edited targetless setting re-runs the targetless attempt alone", "[camera][flow][fallback]")
{
	setUp();
	Fallback fallback;
	setParam(fallback.graph, fallback.board, "model", camera::DistortionModel::KannalaBrandt4);
	Run run{fallback};
	run();
	const Ran before = ran();
	REQUIRE(before.refinements > 0);
	REQUIRE(run.attempts().reports.size() == 2);

	setParam(fallback.graph, fallback.targetless, "resamples", 3);
	run();
	const Ran after = ran();
	CHECK(after.estimates == before.estimates); // the board attempt did not run again
	CHECK(after.refinements > before.refinements);
	REQUIRE(run.attempts().reports.size() == 2);
	CHECK(run.attempts().reports[1].reproducibility.request.resamples == 3);
}

TEST_CASE("footage missing upstream makes no attempt, and the absence propagates", "[camera][flow][fallback]")
{
	setUp();
	Fallback fallback;
	Run run{fallback, false}; // the footage pin unbound, so empty
	run();

	const Ran r = ran();
	CHECK(r.estimates == 0);
	CHECK(r.refinements == 0);
	// No attempt is not an outcome: neither output says the fallback failed.
	CHECK(run.value(fallback.attempts, 0).empty());
	CHECK(run.accepted().empty());
	CHECK(run.model().empty());
}
