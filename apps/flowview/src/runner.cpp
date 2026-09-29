#include "runner.h"

#include <algorithm>
#include <cassert>
#include <exception>

namespace flowview
{
	using namespace lain;

	Runner::~Runner()
	{
		stop();
	}

	bool Runner::busy() const
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		return m_busy;
	}

	std::uint64_t Runner::nextJob() const
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		return m_lastJob + 1;
	}

	std::uint64_t Runner::start(RunJob job)
	{
		std::uint64_t serial = 0;
		{
			const std::lock_guard<std::mutex> lock(m_mutex);
			assert(!m_busy && "flowview::Runner: start() while a job is in flight — cancel and wait for it");
			assert(!m_stopping && "flowview::Runner: start() after stop()");
			if (m_busy || m_stopping)
				return 0;

			serial = ++m_lastJob;
			m_job = std::move(job);
			m_control = std::make_shared<flow::RunControl>(); // single-use: a fresh one per run
			m_busy = true;
			m_abandoned = false;
			if (!m_thread.joinable())
				m_thread = std::thread([this]()
									   { coordinate(); });
		}
		m_wake.notify_one();
		return serial;
	}

	// Everything the run owes but has not started: a cancelled run starts nothing more, so none of it
	// is Queued any longer. A compute already running is left Computing — it finishes, or gives up,
	// and says so. Called with m_mutex held.
	static void dropQueued(RunActivity& activity)
	{
		for (auto it = activity.nodes.begin(); it != activity.nodes.end();)
		{
			if (it->second == Activity::Queued)
				it = activity.nodes.erase(it);
			else
				++it;
		}
	}

	void Runner::cancel()
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		if (m_control)
			m_control->cancel();
		dropQueued(m_activity);
	}

	void Runner::abandon()
	{
		// What is dropped here is destroyed outside the lock: a publication holds a whole
		// evaluation's worth of payloads.
		std::optional<flow::PublishedEvaluation> dropped;
		std::map<NodeAt, Fold> droppedFolds;
		std::optional<RunJob> unstarted;
		{
			const std::lock_guard<std::mutex> lock(m_mutex);
			m_abandoned = true;
			if (m_control)
				m_control->cancel();
			dropped = std::move(m_published);
			m_published.reset();
			droppedFolds.swap(m_folds);
			m_activity.nodes.clear(); // what it does now belongs to a document the panes no longer show
			m_outcome.reset();
			m_failure.clear();
			m_failedNodes = 0;
			// A job not yet picked up has nothing to drain, so it never starts at all.
			if (m_job)
			{
				unstarted = std::move(m_job);
				m_job.reset();
				m_busy = false;
			}
		}
	}

	std::size_t Runner::planned() const
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		return m_control ? m_control->planned() : 0;
	}

	std::size_t Runner::finished() const
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		return m_control ? m_control->finished() : 0;
	}

	RunReport Runner::take()
	{
		RunReport report;
		const std::lock_guard<std::mutex> lock(m_mutex);
		report.published = std::move(m_published);
		m_published.reset();
		report.publishedBy = m_publishedBy;
		report.folds.reserve(m_folds.size());
		for (auto& entry : m_folds)
			report.folds.push_back(std::move(entry.second));
		m_folds.clear();
		report.activity = m_activity;
		report.outcome = m_outcome;
		m_outcome.reset();
		report.partial = m_partial;
		report.failure = std::move(m_failure);
		m_failure.clear();
		report.failedNodes = m_failedNodes;
		m_failedNodes = 0;
		return report;
	}

	void Runner::stop()
	{
		abandon();
		{
			const std::lock_guard<std::mutex> lock(m_mutex);
			m_stopping = true;
		}
		m_wake.notify_all();
		if (m_thread.joinable())
			m_thread.join();
	}

	void Runner::coordinate()
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		for (;;)
		{
			m_wake.wait(lock, [this]()
						{ return m_stopping || m_job.has_value(); });
			if (m_stopping)
				return; // stop() already dropped anything not yet started

			std::optional<RunJob> job = std::move(m_job);
			m_job.reset();
			const std::uint64_t serial = m_lastJob;
			const std::shared_ptr<flow::RunControl> control = m_control;
			lock.unlock();

			std::string failure;
			const RunOutcome outcome = execute(*job, serial, *control, failure);
			const std::size_t failedNodes = control->failed(); // read before anything else can start a run
			const bool partial = !job->scope.whole();
			{
				// The run is over, so it is doing nothing to any node: whatever it owed and never
				// started (a cancel, a throw) is simply stale now, which the publication below says.
				const std::lock_guard<std::mutex> ended(m_mutex);
				m_activity.nodes.clear();
			}
			// Publish at the end of EVERY run: a superseded run's finished work is kept, so it is
			// worth showing, and a failed one's is exactly what the user needs to look at.
			publish(*job, serial, Publication::End);

			// Dropped outside the lock, and here rather than on the UI thread: if the host has since
			// replaced its document, this is the last reference to the old clone and evaluation, and
			// their payloads go with them (ADR-0025's cost: a superseded run's payloads are destroyed on
			// the coordinator).
			job.reset();

			lock.lock();
			if (!m_abandoned)
			{
				m_outcome = outcome;
				m_partial = partial;
				m_failure = std::move(failure);
				m_failedNodes = failedNodes;
			}
			m_busy = false;
		}
	}

	// Every report lands under the runner's lock, since the per-node ones arrive from pool workers
	// several at a time. None does anything once the job is abandoned: its document is gone.
	struct Runner::JobObserver final : flow::RunObserver
	{
		Runner& runner;
		const RunJob& job;
		std::uint64_t serial;
		const flow::RunControl& control;

		JobObserver(Runner& r, const RunJob& j, std::uint64_t s, const flow::RunControl& c)
			: runner(r)
			, job(j)
			, serial(s)
			, control(c)
		{
		}

		// Between stages, where the evaluation is quiescent.
		void stageFinished() override { runner.publish(job, serial, Publication::Stage); }

		// The run owes it: marked in the host's copy, so what it shows reads as stale until its record
		// lands — and Queued, unless the run has been cancelled and will not start it after all. (The
		// mark still goes in then: the request really is in the working evaluation.)
		void owed(const flow::EvalPath& path, flow::NodeId node) noexcept override
		{
			const std::lock_guard<std::mutex> lock(runner.m_mutex);
			if (runner.m_abandoned)
				return;
			Fold& fold = foldAt(path, node);
			fold.owed = true;
			if (!control.cancelled())
				runner.m_activity.nodes.emplace(NodeAt{path, node}, Activity::Queued); // never demotes Computing
		}

		// A Run Selection leaves it owed: marked in the host's copy for the reason owed() marks one — so
		// a node fed the new value past the selection does not read as current once its upstream lands
		// — and NOT Queued, because this run will never compute it. It is simply Stale.
		void leftOwed(const flow::EvalPath& path, flow::NodeId node) noexcept override
		{
			const std::lock_guard<std::mutex> lock(runner.m_mutex);
			if (runner.m_abandoned)
				return;
			foldAt(path, node).owed = true;
		}

		void started(const flow::EvalPath& path, flow::NodeId node) noexcept override
		{
			const std::lock_guard<std::mutex> lock(runner.m_mutex);
			if (runner.m_abandoned)
				return;
			runner.m_activity.nodes[NodeAt{path, node}] = Activity::Computing;
		}

		// It is no longer Computing (or Queued, for a crossing), and its record — when it has one to
		// show — replaces whatever was waiting to land for it. That record settles its own request, so
		// an owed mark from before it is dropped; one made after it (a loop's next pass) is kept.
		void finished(const flow::EvalPath& path, flow::NodeId node, const flow::NodeRecord* record) noexcept override
		{
			const std::lock_guard<std::mutex> lock(runner.m_mutex);
			if (runner.m_abandoned)
				return;
			runner.m_activity.nodes.erase(NodeAt{path, node});
			if (record == nullptr)
				return; // it gave up: nothing to show, and the owed mark already says it is stale
			Fold& fold = foldAt(path, node);
			fold.record = *record;
			fold.owed = false;
		}

	private:
		Fold& foldAt(const flow::EvalPath& path, flow::NodeId node)
		{
			const auto inserted = runner.m_folds.try_emplace(NodeAt{path, node});
			Fold& fold = inserted.first->second;
			if (inserted.second)
			{
				fold.path = path;
				fold.node = node;
			}
			return fold;
		}
	};

	RunOutcome Runner::execute(RunJob& job, std::uint64_t serial, flow::RunControl& control, std::string& failure)
	{
		// Set before the run, on this thread, which is the one that reads it — and let go of when
		// this returns, since the control outlives the job (progress is still read from it until the
		// next start) and the observer does not.
		JobObserver observer{*this, job, serial, control};
		control.setObserver(&observer);
		const RunOutcome outcome = runJob(job, serial, control, failure);
		control.setObserver(nullptr);
		return outcome;
	}

	RunOutcome Runner::runJob(RunJob& job, std::uint64_t serial, flow::RunControl& control, std::string& failure)
	{
		try
		{
			// Prepare FIRST, then bind: a pin added since the last run has no slot until prepare makes
			// one, and Evaluation::bind ignores a pin it has no slot for. run() prepares again, against
			// the same clone, which finds nothing to do. No run lease is needed around this: once a job
			// holds the working evaluation, this thread is the only thing that touches it.
			job.evaluation->prepare(*job.definition);
			for (Binding& binding : job.bindings)
				job.evaluation->bind(binding.first, std::move(binding.second));

			// The run's starting point, published before anything computes: its records fold onto a
			// copy of THIS clone. Without it the first run after New or Open would have nothing to
			// fold into, and a run started while the last one's end publication was still untaken
			// would fold into a copy of that run's clone.
			publish(job, serial, Publication::Start);

			flow::Scheduler& scheduler = (job.strategy == RunStrategy::Serial)
											 ? static_cast<flow::Scheduler&>(m_serial)
											 : static_cast<flow::Scheduler&>(m_parallel);
			if (job.scope.whole())
				scheduler.run(*job.definition, *job.evaluation, control);
			else
				scheduler.evaluate(*job.definition, *job.evaluation, *job.scope.targets, control);
			return control.cancelled() ? RunOutcome::Cancelled : RunOutcome::Completed;
		}
		// Nothing may escape this thread — an exception leaving a std::thread's body terminates the
		// process, which is what a throwing compute() did to gui-mode before this existed. The node
		// that threw stays stale (flow re-requests it on the way out), so the next run retries it — and
		// flow has recorded what it threw against it, in the evaluation this publishes.
		catch (const std::exception& e)
		{
			failure = e.what();
		}
		catch (...)
		{
			failure = "an exception that is not a std::exception";
		}
		return RunOutcome::Failed;
	}

	void Runner::publish(const RunJob& job, std::uint64_t serial, Publication point)
	{
		// A run that failed in prepare() never paired the evaluation with this clone, so there is no
		// consistent view to copy — what the panes already have stands.
		if (job.evaluation->definition() != job.definition.get())
			return;

		{
			const std::lock_guard<std::mutex> lock(m_mutex);
			if (m_abandoned)
				return;
			if (point == Publication::Stage && m_published.has_value())
				return; // the frame loop has not taken the last one yet: no point copying another
		}

		// The copy is taken OUTSIDE the lock — it is a refcount bump per port, but a whole evaluation's
		// worth of them — and only this thread writes the evaluation, so it cannot change underneath.
		flow::PublishedEvaluation copy{job.definition, *job.evaluation};

		// What is replaced is destroyed outside the lock. So are the folds: this copy was taken after
		// every one of them, with no step running meanwhile, so it already shows all of them.
		std::optional<flow::PublishedEvaluation> replaced;
		std::map<NodeAt, Fold> contained;
		{
			const std::lock_guard<std::mutex> lock(m_mutex);
			if (m_abandoned)
				return;
			replaced = std::move(m_published);
			m_published = std::move(copy);
			m_publishedBy = serial;
			contained.swap(m_folds);
		}
	}

	bool land(RunReport& report, flow::PublishedEvaluation& published, std::uint64_t& publishedBy,
			  PendingBindings& bindings)
	{
		bool landed = false;
		if (report.published)
		{
			published = std::move(*report.published);
			publishedBy = report.publishedBy;
			landed = true;
		}
		bool marked = false;
		for (const Fold& fold : report.folds)
		{
			if (fold.record && published.fold(fold.path, fold.node, *fold.record))
				landed = true;
			if (fold.owed && published.owe(fold.path, fold.node))
				marked = true;
		}
		if (landed || marked)
			bindings.showOn(published, publishedBy);
		return landed;
	}

	//=========================================================================
	// PendingBindings
	//=========================================================================
	void PendingBindings::set(flow::PortAddress input, flow::PortValue value)
	{
		// Replaced only while still QUEUED: once a job has taken a value, a newer one is a newer
		// binding, and must outlive the old one's entry until a publication contains it.
		for (Entry& entry : m_entries)
		{
			if (entry.job == 0 && entry.binding.first == input)
			{
				entry.binding.second = std::move(value);
				return;
			}
		}
		m_entries.push_back(Entry{Binding{input, std::move(value)}, 0});
	}

	std::vector<Binding> PendingBindings::handOver(std::uint64_t job)
	{
		std::vector<Binding> taken;
		for (Entry& entry : m_entries)
		{
			if (entry.job != 0)
				continue;
			entry.job = job;
			taken.push_back(entry.binding); // a refcount bump: the entry keeps showing it meanwhile
		}
		return taken;
	}

	void PendingBindings::showOn(flow::PublishedEvaluation& published, std::uint64_t publishedBy)
	{
		// A job applies its bindings before it computes anything, so everything it — or an earlier
		// job — took is in whatever it publishes.
		const auto contained = [&](const Entry& entry)
		{ return entry.job != 0 && entry.job <= publishedBy; };
		m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(), contained), m_entries.end());

		for (const Entry& entry : m_entries)
			published.bind(entry.binding.first, entry.binding.second);
	}
} // namespace flowview
