// The runner (M14 slice 4, ADR-0025): graph work on a coordinator thread of its own, over a CLONE of
// the document, with the frame loop reading a published copy — so the frame loop never waits on it.
//
// What each case pins:
//   * start() hands the run over and returns while it is still computing; it publishes when it ends;
//   * a superseded run KEEPS what it finished, so the next one — over a fresh clone — does not redo it;
//   * a throwing compute becomes an outcome, not a terminated process, and stays stale;
//   * a pending binding is applied AFTER prepare, so a pin added since the last run still receives it;
//   * a run in several stages publishes between them, while it is still in flight;
//   * a document swap drops what the old run would still publish;
//   * stop() joins a run that is still computing;
//   * a binding stays shown over a publication that cannot contain it yet.
// And node by node (M14 slice 7):
//   * a run publishes its starting point, and each result as it lands, before it ends;
//   * a node computing is Computing, what it feeds is Queued and reads as stale, what finished has
//     landed; a cancel stops the Queued, not the Computing;
//   * a publication takes in what landed before it, and what lands after it survives it;
//   * a loop's next pass owes a node whose last pass has landed — both reach the host;
//   * a binding queued after a run started is still shown once that run's boundary node lands.
//
// Every node here is CLONE-SAFE, because the runner clones: what a test watches — a call count, a
// latch, an armed flag — is held behind a shared_ptr, so a clone counts into and waits on the same
// place as the node the test built.

#include "runner.h"

#include <lain/flow/edit.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/graph.h>
#include <lain/flow/group.h>
#include <lain/flow/node.h>
#include <lain/flow/porttyperegistry.h>
#include <lain/flow/runcontrol.h>
#include <lain/flow/scheduler.h>
#include <lain/flow/staleness.h>
#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace lain;
using flowview::Activity;
using flowview::Binding;
using flowview::Fold;
using flowview::land;
using flowview::NodeAt;
using flowview::PendingBindings;
using flowview::RunJob;
using flowview::Runner;
using flowview::RunOutcome;
using flowview::RunReport;
using flowview::RunStrategy;

namespace flowview::test::runner
{
	using Ints = std::vector<int>;

	// Where a held compute waits: the test opens it, and can wait for a compute to be inside it.
	struct Latch
	{
		std::mutex mutex;
		std::condition_variable changed;
		bool open = false;
		int entered = 0;
		std::atomic<int> calls{0};

		void release()
		{
			{
				const std::lock_guard<std::mutex> lock(mutex);
				open = true;
			}
			changed.notify_all();
		}

		// Until `count` computes have reached the latch — which is how a test knows the coordinator
		// is inside a run, rather than guessing with a sleep. Bounded, so a broken runner fails the
		// case instead of hanging it.
		void waitEntered(int count = 1)
		{
			std::unique_lock<std::mutex> lock(mutex);
			const bool reached = changed.wait_for(lock, std::chrono::seconds(10), [&]()
												  { return entered >= count; });
			REQUIRE(reached);
		}
	};

	// How long a held compute waits for the test to open its latch. Bounded, like waitEntered, so a case
	// that FAILS before releasing — a REQUIRE throws past the release — fails instead of hanging: the
	// runner's destructor joins the coordinator, which is still inside this compute.
	constexpr std::chrono::seconds kHeldAtMost{10};

	// An int passthrough that waits at the latch before it produces. COOPERATIVE ones also give up
	// when the run is cancelled — a long compute polling NodeEvaluation::cancelled().
	struct Held : flow::Node
	{
		std::shared_ptr<Latch> latch;
		bool cooperative;
		flow::PortId in, out;
		Held(std::shared_ptr<Latch> l, bool coop = false)
			: flow::Node("Held")
			, latch(std::move(l))
			, cooperative(coop)
		{
			in = addInput<int>("in");
			out = addOutput<int>("out");
		}
		std::unique_ptr<flow::Node> clone() const override { return std::make_unique<Held>(*this); }
		void compute(flow::NodeEvaluation& evaluation) const override
		{
			++latch->calls;
			{
				std::unique_lock<std::mutex> lock(latch->mutex);
				++latch->entered;
				latch->changed.notify_all();
				const auto deadline = std::chrono::steady_clock::now() + kHeldAtMost;
				while (!latch->open && std::chrono::steady_clock::now() < deadline)
				{
					if (cooperative && evaluation.cancelled())
						return; // given up: nothing written, so the node stays stale
					latch->changed.wait_for(lock, std::chrono::milliseconds(1));
				}
			}
			evaluation.output(out).set(evaluation.input(in).get<int>());
		}
	};

	// An int source whose value is a param.
	struct IntSource : flow::Node
	{
		std::shared_ptr<std::atomic<int>> calls;
		flow::PortId value, out;
		IntSource(std::shared_ptr<std::atomic<int>> c, int initial)
			: flow::Node("IntSource")
			, calls(std::move(c))
		{
			value = addParam<int>("value", initial);
			out = addOutput<int>("out");
		}
		std::unique_ptr<flow::Node> clone() const override { return std::make_unique<IntSource>(*this); }
		void compute(flow::NodeEvaluation& evaluation) const override
		{
			++*calls;
			evaluation.output(out).set(param(value).get<int>());
		}
	};

	// Passes an int through, counted.
	struct Relay : flow::Node
	{
		std::shared_ptr<std::atomic<int>> calls;
		flow::PortId in, out;
		explicit Relay(std::shared_ptr<std::atomic<int>> c)
			: flow::Node("Relay")
			, calls(std::move(c))
		{
			in = addInput<int>("in");
			out = addOutput<int>("out");
		}
		std::unique_ptr<flow::Node> clone() const override { return std::make_unique<Relay>(*this); }
		void compute(flow::NodeEvaluation& evaluation) const override
		{
			++*calls;
			evaluation.output(out).set(evaluation.input(in).get<int>());
		}
	};

	// An int passthrough that throws while armed.
	struct Thrower : flow::Node
	{
		std::shared_ptr<std::atomic<bool>> armed;
		std::shared_ptr<std::atomic<int>> calls;
		flow::PortId in, out;
		Thrower(std::shared_ptr<std::atomic<bool>> a, std::shared_ptr<std::atomic<int>> c)
			: flow::Node("Thrower")
			, armed(std::move(a))
			, calls(std::move(c))
		{
			in = addInput<int>("in");
			out = addOutput<int>("out");
		}
		std::unique_ptr<flow::Node> clone() const override { return std::make_unique<Thrower>(*this); }
		void compute(flow::NodeEvaluation& evaluation) const override
		{
			++*calls;
			if (*armed)
				throw std::runtime_error("boom");
			evaluation.output(out).set(evaluation.input(in).get<int>());
		}
	};

	// int + 1 that passes freely for its first `free` computes, then waits at the latch like Held —
	// the body of a loop whose SECOND pass a test wants to catch in flight.
	struct HeldAfter : flow::Node
	{
		std::shared_ptr<Latch> latch;
		int free;
		flow::PortId in, out;
		HeldAfter(std::shared_ptr<Latch> l, int f)
			: flow::Node("HeldAfter")
			, latch(std::move(l))
			, free(f)
		{
			in = addInput<int>("x");
			out = addOutput<int>("x");
		}
		std::unique_ptr<flow::Node> clone() const override { return std::make_unique<HeldAfter>(*this); }
		void compute(flow::NodeEvaluation& evaluation) const override
		{
			if (++latch->calls > free)
			{
				std::unique_lock<std::mutex> lock(latch->mutex);
				++latch->entered;
				latch->changed.notify_all();
				latch->changed.wait_for(lock, kHeldAtMost, [&]()
										{ return latch->open; });
			}
			evaluation.output(out).set(evaluation.input(in).get<int>() + 1);
		}
	};

	// A whole list of ints — what a map maps over.
	struct MakeInts : flow::Node
	{
		flow::PortId out;
		Ints values;
		explicit MakeInts(Ints v)
			: flow::Node("MakeInts")
			, values(std::move(v))
		{
			out = addOutput<Ints>("items");
		}
		std::unique_ptr<flow::Node> clone() const override { return std::make_unique<MakeInts>(*this); }
		void compute(flow::NodeEvaluation& evaluation) const override { evaluation.output(out).set(values); }
	};

	std::shared_ptr<std::atomic<int>> counter() { return std::make_shared<std::atomic<int>>(0); }

	// What the host does at the end of a frame: clone the document, and hand the clone, the working
	// evaluation and the bindings to the runner.
	RunJob jobFor(const flow::Graph& document, const std::shared_ptr<flow::Evaluation>& evaluation,
				  RunStrategy strategy, std::vector<Binding> bindings = {})
	{
		RunJob job;
		job.definition = std::make_shared<const flow::Graph>(document.clone());
		job.evaluation = evaluation;
		job.bindings = std::move(bindings);
		job.strategy = strategy;
		return job;
	}

	// What the frame loop does, frame after frame, until the runner is idle — without sleeping, and
	// bounded, so a runner that never finishes fails the case rather than hanging it.
	void waitIdle(const Runner& runner)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (runner.busy())
		{
			if (std::chrono::steady_clock::now() >= deadline)
				FAIL("the runner never went idle");
			std::this_thread::yield();
		}
	}

	// A port's value in a published evaluation, as an int — addressed through the DOCUMENT, whose ids
	// every clone shares.
	flow::PortValue valueOf(const RunReport& report, const flow::Graph& document, flow::NodeId node)
	{
		REQUIRE(report.published.has_value());
		return report.published->evaluation().value(flow::PortAddress{node, document.node(node).output(0).id()});
	}

	flow::PortValue intValue(int v)
	{
		flow::PortValue value;
		value.set(v);
		return value;
	}

	// A node's first output in a copy the host shows.
	flow::PortValue shownValue(const flow::PublishedEvaluation& shown, const flow::Graph& document, flow::NodeId node)
	{
		return shown.evaluation().value(flow::PortAddress{node, document.node(node).output(0).id()});
	}

	// What the run in flight is doing to a node, as a report says — nothing if it is idle there.
	std::optional<Activity> activityOf(const RunReport& report, const flow::EvalPath& path, flow::NodeId node)
	{
		const auto it = report.activity.nodes.find(NodeAt{path, node});
		if (it == report.activity.nodes.end())
			return std::nullopt;
		return it->second;
	}

	// The fold a report carries for a node, or nullptr.
	const Fold* foldOf(const RunReport& report, const flow::EvalPath& path, flow::NodeId node)
	{
		for (const Fold& fold : report.folds)
		{
			if (fold.path == path && fold.node == node)
				return &fold;
		}
		return nullptr;
	}

	// source(7) -> held -> sink: a run caught with `held` in flight.
	struct Chain
	{
		std::shared_ptr<Latch> latch = std::make_shared<Latch>();
		flow::Graph document;
		flow::NodeId source, held, sink;

		explicit Chain(bool cooperative = false)
		{
			source = document.add<IntSource>(counter(), 7);
			held = document.add<Held>(latch, cooperative);
			sink = document.add<Relay>(counter());
			REQUIRE(document.connect(source, 0, held, 0) == flow::Connection::Ok);
			REQUIRE(document.connect(held, 0, sink, 0) == flow::Connection::Ok);
		}
		Chain(const Chain&) = delete;
	};
} // namespace flowview::test::runner

using namespace flowview::test::runner;

// Each check runs under both strategies: the coordinator runs the plan itself under Serial, and
// participates in the pool under Parallel — the "+1" ADR-0024 sizes it for.
static void forEachStrategy(void (*check)(RunStrategy))
{
	SECTION("serial")
	{
		check(RunStrategy::Serial);
	}
	SECTION("parallel")
	{
		lain::testing::ThreadPool pool;
		check(RunStrategy::Parallel);
	}
}

static void checkStartReturns(RunStrategy strategy)
{
	auto latch = std::make_shared<Latch>();
	flow::Graph document;
	const flow::NodeId source = document.add<IntSource>(counter(), 7);
	const flow::NodeId held = document.add<Held>(latch);
	REQUIRE(document.connect(source, 0, held, 0) == flow::Connection::Ok);
	auto evaluation = std::make_shared<flow::Evaluation>(document);

	Runner runner;
	const std::uint64_t serial = runner.start(jobFor(document, evaluation, strategy));
	// start() has returned, and the compute is still waiting: the caller did not.
	latch->waitEntered();
	REQUIRE(runner.busy());

	// One stage, and not over — yet the frame loop already has something: the run's STARTING POINT,
	// published before anything computed, and the source's result, which landed as it finished.
	RunReport midway = runner.take();
	REQUIRE(midway.published.has_value());
	REQUIRE(midway.publishedBy == serial);
	flow::PublishedEvaluation shown;
	std::uint64_t shownBy = 0;
	PendingBindings none;
	REQUIRE(land(midway, shown, shownBy, none));
	REQUIRE(shownValue(shown, document, source).get<int>() == 7);
	REQUIRE(shownValue(shown, document, held).empty());

	latch->release();
	waitIdle(runner);
	const RunReport report = runner.take();
	REQUIRE(report.outcome == RunOutcome::Completed);
	REQUIRE(report.publishedBy == serial);
	REQUIRE(valueOf(report, document, held).get<int>() == 7);
	REQUIRE(runner.finished() == runner.planned());
}

TEST_CASE("start hands the run over and returns while it computes", "[flowview][runner]")
{
	forEachStrategy(checkStartReturns);
}

static void checkSupersedeKeeps(RunStrategy strategy)
{
	auto latch = std::make_shared<Latch>();
	auto sinkCalls = counter();
	flow::Graph document;
	const flow::NodeId source = document.add<IntSource>(counter(), 7);
	const flow::NodeId slow = document.add<Held>(latch);
	const flow::NodeId sink = document.add<Relay>(sinkCalls);
	REQUIRE(document.connect(source, 0, slow, 0) == flow::Connection::Ok);
	REQUIRE(document.connect(slow, 0, sink, 0) == flow::Connection::Ok);
	auto evaluation = std::make_shared<flow::Evaluation>(document);

	Runner runner;
	runner.start(jobFor(document, evaluation, strategy));
	latch->waitEntered();
	runner.cancel();
	latch->release();
	waitIdle(runner);

	const RunReport first = runner.take();
	REQUIRE(first.outcome == RunOutcome::Cancelled);
	// Published even though it was cancelled: what it finished is worth showing.
	REQUIRE(valueOf(first, document, slow).get<int>() == 7);
	REQUIRE(valueOf(first, document, sink).empty()); // never reached
	REQUIRE(*sinkCalls == 0);

	runner.start(jobFor(document, evaluation, strategy));
	waitIdle(runner);
	const RunReport second = runner.take();
	REQUIRE(second.outcome == RunOutcome::Completed);
	REQUIRE(latch->calls == 1); // kept, not recomputed
	REQUIRE(*sinkCalls == 1);
	REQUIRE(valueOf(second, document, sink).get<int>() == 7);
}

TEST_CASE("a superseded run keeps what it finished", "[flowview][runner]")
{
	// source -> slow -> sink. The run is cancelled while `slow` computes — what a Live edit does to
	// the run in flight — and `slow` then finishes normally. It is KEPT: the next run, over a fresh
	// clone of an unchanged document, finds it clean and computes only what the cancel stopped.
	forEachStrategy(checkSupersedeKeeps);
}

static void checkThrowIsAnOutcome(RunStrategy strategy)
{
	auto armed = std::make_shared<std::atomic<bool>>(true);
	auto throwerCalls = counter();
	flow::Graph document;
	const flow::NodeId source = document.add<IntSource>(counter(), 7);
	const flow::NodeId thrower = document.add<Thrower>(armed, throwerCalls);
	const flow::NodeId after = document.add<Relay>(counter());
	REQUIRE(document.connect(source, 0, thrower, 0) == flow::Connection::Ok);
	REQUIRE(document.connect(thrower, 0, after, 0) == flow::Connection::Ok);
	auto evaluation = std::make_shared<flow::Evaluation>(document);

	Runner runner;
	runner.start(jobFor(document, evaluation, strategy));
	waitIdle(runner);
	const RunReport failed = runner.take();
	REQUIRE(failed.outcome == RunOutcome::Failed);
	// The throw left `after` owed and never started. The run is over, so it is not Queued: it waits,
	// Stale, for a run that will start it.
	REQUIRE(failed.activity.nodes.empty());
	REQUIRE(failed.failure.find("boom") != std::string::npos);
	REQUIRE(valueOf(failed, document, thrower).empty());
	// A node OWNED this throw, and the publication says which: the host shows it on that node.
	REQUIRE(failed.failedNodes == 1);
	REQUIRE(failed.published->evaluation().failure(thrower) != nullptr);

	// Disarmed WITHOUT an edit — nothing about the recipe changed, so only its staleness can bring
	// the node back into the next run.
	*armed = false;
	runner.start(jobFor(document, evaluation, strategy));
	waitIdle(runner);
	const RunReport retried = runner.take();
	REQUIRE(retried.outcome == RunOutcome::Completed);
	REQUIRE(*throwerCalls == 2);
	REQUIRE(valueOf(retried, document, thrower).get<int>() == 7);
	REQUIRE(retried.failedNodes == 0);
	REQUIRE(retried.published->evaluation().failure(thrower) == nullptr);
}

TEST_CASE("a throwing compute is an outcome not a crash and is retried", "[flowview][runner]")
{
	// Before the runner, nothing in gui-mode caught a compute's exception. On a coordinator thread
	// it would terminate the process — so the runner reports it, and the node that threw stays stale.
	forEachStrategy(checkThrowIsAnOutcome);
}

TEST_CASE("a throw no node owned is reported with no node count", "[flowview][runner]")
{
	// The one kind of failure a host must report for the run as a whole: here, a job pairing an
	// evaluation with a definition of ANOTHER document, which prepare refuses before any node runs.
	// No node threw, so none is counted — and there is no publication to show one on.
	flow::Graph first;
	first.add<IntSource>(counter(), 1);
	flow::Graph second;
	second.add<IntSource>(counter(), 2);
	auto evaluation = std::make_shared<flow::Evaluation>(first);

	Runner runner;
	runner.start(jobFor(second, evaluation, RunStrategy::Serial));
	waitIdle(runner);
	const RunReport report = runner.take();
	REQUIRE(report.outcome == RunOutcome::Failed);
	REQUIRE(report.failedNodes == 0);
	REQUIRE_FALSE(report.failure.empty());
	REQUIRE_FALSE(report.published.has_value());
}

TEST_CASE("a pending binding reaches a pin added since the last run", "[flowview][runner]")
{
	// The working evaluation has never seen the new pin, so it has no slot for it until prepare makes
	// one — and Evaluation::bind ignores a pin it has no slot for. The runner prepares FIRST.
	flow::Graph document;
	auto evaluation = std::make_shared<flow::Evaluation>(document);
	Runner runner;
	runner.start(jobFor(document, evaluation, RunStrategy::Serial));
	waitIdle(runner);
	(void)runner.take();

	const flow::PortId pin = document.boundaryInputNode().addBoundary<int>("added");
	const flow::PortAddress bound{document.boundaryInputNode().id(), pin};
	const flow::NodeId relay = document.add<Relay>(counter());
	REQUIRE(document.connect(bound, flow::PortAddress{relay, document.node(relay).input(0).id()}) == flow::Connection::Ok);

	runner.start(jobFor(document, evaluation, RunStrategy::Serial, {Binding{bound, intValue(5)}}));
	waitIdle(runner);
	const RunReport report = runner.take();
	REQUIRE(report.outcome == RunOutcome::Completed);
	REQUIRE(valueOf(report, document, relay).get<int>() == 5);
}

static void checkStagePublication(RunStrategy strategy)
{
	flow::registerPortType<int>("Int");
	flow::registerPortType<Ints>("ListOfInt"); // the map lifts an inner Int through its list form

	auto latch = std::make_shared<Latch>();
	flow::Graph document;
	const flow::NodeId list = document.add<MakeInts>(Ints{1, 2, 3});
	const flow::NodeId map = document.add<flow::MapNode>();
	flow::Graph& inner = static_cast<flow::MapNode&>(document.node(map)).inner();
	const flow::PortId x = inner.boundaryInputNode().addBoundary<int>("x");
	const flow::PortId y = inner.boundaryOutputNode().addBoundary<int>("y");
	const flow::NodeId body = inner.add<Held>(latch);
	REQUIRE(inner.connect(flow::PortAddress{inner.boundaryInputNode().id(), x},
						  flow::PortAddress{body, inner.node(body).input(0).id()}) == flow::Connection::Ok);
	REQUIRE(inner.connect(flow::PortAddress{body, inner.node(body).output(0).id()},
						  flow::PortAddress{inner.boundaryOutputNode().id(), y}) == flow::Connection::Ok);
	flow::edit::syncGroupPorts(document, map);
	REQUIRE(document.connect(list, 0, map, 0) == flow::Connection::Ok);
	auto evaluation = std::make_shared<flow::Evaluation>(document);

	Runner runner;
	runner.start(jobFor(document, evaluation, strategy));
	latch->waitEntered(); // an element is computing: the first stage is behind us

	// What the frame loop has by now: the run's starting point, with everything that landed since
	// folded on top — the first stage's list among it.
	RunReport midway = runner.take();
	REQUIRE(runner.busy());
	flow::PublishedEvaluation shown;
	std::uint64_t shownBy = 0;
	PendingBindings none;
	land(midway, shown, shownBy, none);
	REQUIRE(shownValue(shown, document, list).get<Ints>() == Ints{1, 2, 3});
	REQUIRE(shownValue(shown, document, map).empty()); // gathered only once the elements ran

	latch->release();
	waitIdle(runner);
	const RunReport done = runner.take();
	REQUIRE(done.outcome == RunOutcome::Completed);
	REQUIRE(valueOf(done, document, map).get<Ints>() == Ints{1, 2, 3});
}

TEST_CASE("a run in several stages publishes between them", "[flowview][runner][map]")
{
	// list -> map(held): the first stage computes the list, the second the map's elements — which
	// wait at the latch. So while the run is still in flight, the frame loop already has a copy of
	// the first stage to show.
	forEachStrategy(checkStagePublication);
}

TEST_CASE("abandoning a run drops what it would still publish", "[flowview][runner]")
{
	// A document swap mid-run: the run drains on its own, and nothing of it reaches the frame loop —
	// its values belong to a lineage the panes are no longer showing.
	auto latch = std::make_shared<Latch>();
	flow::Graph document;
	const flow::NodeId source = document.add<IntSource>(counter(), 7);
	const flow::NodeId held = document.add<Held>(latch);
	REQUIRE(document.connect(source, 0, held, 0) == flow::Connection::Ok);
	auto evaluation = std::make_shared<flow::Evaluation>(document);

	Runner runner;
	runner.start(jobFor(document, evaluation, RunStrategy::Serial));
	latch->waitEntered();
	runner.abandon();
	evaluation.reset(); // the host lets go of its share; the job still holds one
	latch->release();
	waitIdle(runner);

	const RunReport report = runner.take();
	REQUIRE_FALSE(report.published.has_value());
	REQUIRE_FALSE(report.outcome.has_value());
	REQUIRE(report.folds.empty()); // nothing it did, node by node, either
	REQUIRE(report.activity.nodes.empty());
}

static void checkStopJoins(RunStrategy strategy)
{
	auto latch = std::make_shared<Latch>();
	flow::Graph document;
	const flow::NodeId source = document.add<IntSource>(counter(), 7);
	const flow::NodeId held = document.add<Held>(latch, true);
	REQUIRE(document.connect(source, 0, held, 0) == flow::Connection::Ok);
	auto evaluation = std::make_shared<flow::Evaluation>(document);

	Runner runner;
	runner.start(jobFor(document, evaluation, strategy));
	latch->waitEntered();
	runner.stop(); // never released: only the cancel lets the compute go
	REQUIRE_FALSE(runner.busy());

	// It gave up, so it is still owed. Asked after preparing against the document: the clone the run
	// read died with its job, and an evaluation reads staleness through what it was prepared against.
	evaluation->prepare(document);
	REQUIRE(evaluation->needsRecompute(held));
}

TEST_CASE("stop joins a run still in flight", "[flowview][runner]")
{
	// What onStop does before the task pool stops: the compute in progress asks whether it was
	// cancelled, hears yes, and gives up — and stop() returns once the coordinator has drained.
	forEachStrategy(checkStopJoins);
}

TEST_CASE("a binding stays shown until a publication contains it", "[flowview][runner]")
{
	// The frame loop's side of a pending binding, without a thread: which bindings a publication
	// from a given job can already contain. A run that ends between one frame's poll and its pump
	// publishes after the next job has taken the queue — so a publication from job N must not read a
	// binding taken by job N+1 back as its old value.
	flow::Graph document;
	const flow::PortAddress x{document.boundaryInputNode().id(), document.boundaryInputNode().addBoundary<int>("x")};
	const flow::PortAddress y{document.boundaryInputNode().id(), document.boundaryInputNode().addBoundary<int>("y")};
	auto clone = std::make_shared<const flow::Graph>(document.clone());
	flow::Evaluation working{document};
	working.prepare(*clone);

	// A publication from `job`, whose slots hold what that job applied.
	const auto published = [&](int onX, int onY)
	{
		working.bind(x, intValue(onX));
		working.bind(y, intValue(onY));
		return flow::PublishedEvaluation{clone, working};
	};
	const auto shown = [&](const flow::PublishedEvaluation& p, const flow::PortAddress& at)
	{ return p.evaluation().value(at).get<int>(); };

	PendingBindings bindings;
	bindings.set(x, intValue(2));
	bindings.set(x, intValue(3)); // the latest per pin, while still queued
	const std::vector<Binding> taken = bindings.handOver(2);
	REQUIRE(taken.size() == 1);
	REQUIRE(taken.front().second.get<int>() == 3);
	bindings.set(y, intValue(4)); // bound after job 2 took the queue

	SECTION("a publication from an earlier job shows what later jobs took and what is queued")
	{
		flow::PublishedEvaluation fromJob1 = published(1, 0);
		bindings.showOn(fromJob1, 1);
		REQUIRE(shown(fromJob1, x) == 3); // taken by job 2, which has not published
		REQUIRE(shown(fromJob1, y) == 4); // still queued
	}
	SECTION("a publication from the job that took a binding shows its own value for it")
	{
		flow::PublishedEvaluation fromJob2 = published(3, 0);
		bindings.showOn(fromJob2, 2);
		REQUIRE(shown(fromJob2, x) == 3);
		REQUIRE(shown(fromJob2, y) == 4); // still queued

		// Job 3 takes y. A later publication of job 3 is its OWN word on both pins: nothing it could
		// contain is laid over it — including x, which job 2 took and job 2's publication retired.
		REQUIRE(bindings.handOver(3).size() == 1);
		flow::PublishedEvaluation fromJob3 = published(9, 8);
		bindings.showOn(fromJob3, 3);
		REQUIRE(shown(fromJob3, x) == 9);
		REQUIRE(shown(fromJob3, y) == 8);
	}
}

//=========================================================================
// Node by node (M14 slice 7)
//=========================================================================

static void checkComputingAndQueued(RunStrategy strategy)
{
	Chain chain;
	auto evaluation = std::make_shared<flow::Evaluation>(chain.document);
	Runner runner;
	runner.start(jobFor(chain.document, evaluation, strategy));
	chain.latch->waitEntered();

	RunReport report = runner.take();
	REQUIRE(activityOf(report, flow::EvalPath{}, chain.held) == Activity::Computing);
	REQUIRE(activityOf(report, flow::EvalPath{}, chain.sink) == Activity::Queued);
	REQUIRE_FALSE(activityOf(report, flow::EvalPath{}, chain.source).has_value()); // done

	// Landed as the host lands it, the copy says exactly that: the source current, what is in flight
	// or waiting still stale — the owed marks are what hold the sink, whose own record never changed.
	flow::PublishedEvaluation shown;
	std::uint64_t shownBy = 0;
	PendingBindings none;
	land(report, shown, shownBy, none);
	const flow::StaleClosure midway(chain.document, shown.evaluation());
	REQUIRE_FALSE(midway.contains(chain.source));
	REQUIRE(midway.contains(chain.held));
	REQUIRE(midway.contains(chain.sink));

	chain.latch->release();
	waitIdle(runner);
	RunReport done = runner.take();
	REQUIRE(done.activity.nodes.empty()); // the run is over: it is doing nothing to anything
	land(done, shown, shownBy, none);
	REQUIRE(flow::StaleClosure(chain.document, shown.evaluation()).order().empty());
	REQUIRE(shownValue(shown, chain.document, chain.sink).get<int>() == 7);
}

TEST_CASE("a node computing is Computing and what it feeds is Queued", "[flowview][runner]")
{
	forEachStrategy(checkComputingAndQueued);
}

static void checkCancelDropsQueued(RunStrategy strategy)
{
	Chain chain; // not cooperative: the compute in flight finishes whatever the cancel says
	auto evaluation = std::make_shared<flow::Evaluation>(chain.document);
	Runner runner;
	runner.start(jobFor(chain.document, evaluation, strategy));
	chain.latch->waitEntered();

	runner.cancel();
	const RunReport cancelled = runner.take();
	REQUIRE(activityOf(cancelled, flow::EvalPath{}, chain.held) == Activity::Computing); // still running
	REQUIRE_FALSE(activityOf(cancelled, flow::EvalPath{}, chain.sink).has_value());		 // never will

	chain.latch->release();
	waitIdle(runner);
	REQUIRE(runner.take().activity.nodes.empty());
}

TEST_CASE("a cancel ends Queued but not Computing", "[flowview][runner]")
{
	forEachStrategy(checkCancelDropsQueued);
}

static void checkPublicationContainsFolds(RunStrategy strategy)
{
	flow::registerPortType<int>("Int");
	flow::registerPortType<Ints>("ListOfInt");

	// source -> first -> sink beside list -> map(second): `first` holds the first stage open, `second`
	// the map's elements in the second.
	auto first = std::make_shared<Latch>();
	auto second = std::make_shared<Latch>();
	flow::Graph document;
	const flow::NodeId source = document.add<IntSource>(counter(), 7);
	const flow::NodeId held = document.add<Held>(first);
	REQUIRE(document.connect(source, 0, held, 0) == flow::Connection::Ok);
	const flow::NodeId list = document.add<MakeInts>(Ints{1, 2, 3});
	const flow::NodeId map = document.add<flow::MapNode>();
	flow::Graph& inner = static_cast<flow::MapNode&>(document.node(map)).inner();
	const flow::PortId x = inner.boundaryInputNode().addBoundary<int>("x");
	const flow::PortId y = inner.boundaryOutputNode().addBoundary<int>("y");
	const flow::NodeId body = inner.add<Held>(second);
	REQUIRE(inner.connect(flow::PortAddress{inner.boundaryInputNode().id(), x},
						  flow::PortAddress{body, inner.node(body).input(0).id()}) == flow::Connection::Ok);
	REQUIRE(inner.connect(flow::PortAddress{body, inner.node(body).output(0).id()},
						  flow::PortAddress{inner.boundaryOutputNode().id(), y}) == flow::Connection::Ok);
	flow::edit::syncGroupPorts(document, map);
	REQUIRE(document.connect(list, 0, map, 0) == flow::Connection::Ok);
	auto evaluation = std::make_shared<flow::Evaluation>(document);

	Runner runner;
	const std::uint64_t serial = runner.start(jobFor(document, evaluation, strategy));
	first->waitEntered();
	(void)runner.take(); // the start publication, taken — so the stage publication is not skipped

	first->release();
	second->waitEntered(); // the second stage is running, so the first has published
	RunReport report = runner.take();
	REQUIRE(report.published.has_value());
	REQUIRE(report.publishedBy == serial);
	REQUIRE(shownValue(*report.published, document, held).get<int>() == 7);

	// Everything the first stage did is IN that publication, so none of it is left to fold: every
	// fold still waiting is the second stage's, inside the map.
	REQUIRE_FALSE(report.folds.empty());
	for (const Fold& fold : report.folds)
		REQUIRE_FALSE(fold.path.empty());
	const Fold* owedBody = foldOf(report, flow::EvalPath{{map, 0}}, body);
	REQUIRE(owedBody != nullptr);
	REQUIRE(owedBody->owed);

	// ... and what landed after it survives it: the body reads stale in the copy the host lands.
	flow::PublishedEvaluation shown;
	std::uint64_t shownBy = 0;
	PendingBindings none;
	land(report, shown, shownBy, none);
	REQUIRE(shown.evaluation().childCount(map) == 3); // sized by the run after the copy was taken
	REQUIRE(shown.evaluation().child(map, 0).needsRecompute(body));

	second->release();
	waitIdle(runner);
}

TEST_CASE("a publication takes in what landed before it, and what lands after survives", "[flowview][runner][map]")
{
	forEachStrategy(checkPublicationContainsFolds);
}

static void checkLoopOwesAgain(RunStrategy strategy)
{
	// seed(1) -> loop(value: body = +1, three passes). The body passes freely once, then waits: so
	// the second pass is caught in flight, having been owed after the first pass's result landed.
	auto latch = std::make_shared<Latch>();
	flow::Graph document;
	const flow::NodeId seed = document.add<IntSource>(counter(), 1);
	const flow::NodeId trips = document.add<IntSource>(counter(), 3);
	const flow::NodeId loop = document.add<flow::LoopNode>();
	auto& node = static_cast<flow::LoopNode&>(document.node(loop));
	const flow::LoopNode::Carry carry = node.addCarry<int>("value");
	flow::Graph& inner = node.inner();
	const flow::NodeId body = inner.add<HeldAfter>(latch, 1);
	REQUIRE(inner.connect(flow::PortAddress{inner.boundaryInputNode().id(), carry.innerIn},
						  flow::PortAddress{body, inner.node(body).input(0).id()}) == flow::Connection::Ok);
	REQUIRE(inner.connect(flow::PortAddress{body, inner.node(body).output(0).id()},
						  flow::PortAddress{inner.boundaryOutputNode().id(), carry.innerOut}) == flow::Connection::Ok);
	flow::edit::syncGroupPorts(document, loop);
	flow::PortId value;
	for (std::size_t i = 0; i < node.inputCount(); ++i)
	{
		if (node.input(i).name() == "value")
			value = node.input(i).id();
	}
	REQUIRE(document.connect(flow::PortAddress{seed, document.node(seed).output(0).id()},
							 flow::PortAddress{loop, value}) == flow::Connection::Ok);
	REQUIRE(document.connect(flow::PortAddress{trips, document.node(trips).output(0).id()},
							 flow::PortAddress{loop, node.countPort()}) == flow::Connection::Ok);
	auto evaluation = std::make_shared<flow::Evaluation>(document);

	Runner runner;
	runner.start(jobFor(document, evaluation, strategy));
	latch->waitEntered(); // the SECOND pass. Nothing taken yet, so no stage publication was made.

	RunReport report = runner.take();
	const flow::EvalPath interior{{loop, 0}};
	const Fold* fold = foldOf(report, interior, body);
	REQUIRE(fold != nullptr);
	REQUIRE(fold->record.has_value()); // the first pass's result ...
	REQUIRE(fold->owed);			   // ... and the second pass owing it again
	REQUIRE(activityOf(report, interior, body) == Activity::Computing);

	flow::PublishedEvaluation shown;
	std::uint64_t shownBy = 0;
	PendingBindings none;
	land(report, shown, shownBy, none);
	const flow::Evaluation& shownInterior = shown.evaluation().child(loop);
	REQUIRE(shownInterior.value(flow::PortAddress{body, inner.node(body).output(0).id()}).get<int>() == 2);
	REQUIRE(shownInterior.needsRecompute(body)); // showing pass one, owed pass two

	latch->release();
	waitIdle(runner);
}

TEST_CASE("a loop's next pass owes a node whose last pass has landed", "[flowview][runner][loop]")
{
	forEachStrategy(checkLoopOwesAgain);
}

TEST_CASE("a binding queued after a run started stays shown when its boundary lands", "[flowview][runner]")
{
	// The run bound x = 1 and published that, and its boundary node's record — which carries x = 1 —
	// lands node by node. The user has since bound x = 2, queued for the next run. The frame loop must
	// go on showing 2: a fold that overwrote it would snap a drag back until the next run caught up.
	auto latch = std::make_shared<Latch>();
	flow::Graph document;
	const flow::PortAddress x{document.boundaryInputNode().id(), document.boundaryInputNode().addBoundary<int>("x")};
	const flow::NodeId held = document.add<Held>(latch);
	REQUIRE(document.connect(x, flow::PortAddress{held, document.node(held).input(0).id()}) == flow::Connection::Ok);
	auto evaluation = std::make_shared<flow::Evaluation>(document);

	PendingBindings bindings;
	bindings.set(x, intValue(1));
	Runner runner;
	const std::uint64_t job = runner.nextJob();
	runner.start(jobFor(document, evaluation, RunStrategy::Serial, bindings.handOver(job)));
	latch->waitEntered();
	bindings.set(x, intValue(2)); // after the run took the queue

	RunReport report = runner.take();
	REQUIRE(foldOf(report, flow::EvalPath{}, x.node) != nullptr); // the boundary node's record, x = 1
	flow::PublishedEvaluation shown;
	std::uint64_t shownBy = 0;
	land(report, shown, shownBy, bindings);
	REQUIRE(shown.evaluation().value(x).get<int>() == 2);

	latch->release();
	waitIdle(runner);
}

static void checkStartReplacesUntaken(RunStrategy strategy)
{
	// A run's results must fold onto a copy of ITS clone. Here the last run's end publication is still
	// untaken when the next run starts — what a Live drag does every frame — and the document has
	// gained a node since, which only the new clone has. The start publication replaces the untaken
	// one, so the new node's result has somewhere to land.
	auto latch = std::make_shared<Latch>();
	flow::Graph document;
	const flow::NodeId source = document.add<IntSource>(counter(), 7);
	const flow::NodeId held = document.add<Held>(latch);
	REQUIRE(document.connect(source, 0, held, 0) == flow::Connection::Ok);
	auto evaluation = std::make_shared<flow::Evaluation>(document);

	Runner runner;
	latch->release(); // the first run passes straight through
	runner.start(jobFor(document, evaluation, strategy));
	waitIdle(runner); // ... and its end publication is left untaken

	auto closed = std::make_shared<Latch>();
	const flow::NodeId extra = document.add<IntSource>(counter(), 9);
	const flow::NodeId waits = document.add<Held>(closed);
	REQUIRE(document.connect(extra, 0, waits, 0) == flow::Connection::Ok);
	const std::uint64_t second = runner.start(jobFor(document, evaluation, strategy));
	closed->waitEntered();

	RunReport report = runner.take();
	REQUIRE(report.publishedBy == second);
	flow::PublishedEvaluation shown;
	std::uint64_t shownBy = 0;
	PendingBindings none;
	land(report, shown, shownBy, none);
	REQUIRE(shownValue(shown, document, extra).get<int>() == 9);

	closed->release();
	waitIdle(runner);
}

TEST_CASE("a run's start publication replaces one left untaken", "[flowview][runner]")
{
	forEachStrategy(checkStartReplacesUntaken);
}
