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

	void Runner::cancel()
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		if (m_control)
			m_control->cancel();
	}

	void Runner::abandon()
	{
		// What is dropped here is destroyed outside the lock: a publication holds a whole
		// evaluation's worth of payloads.
		std::optional<flow::PublishedEvaluation> dropped;
		std::optional<RunJob> unstarted;
		{
			const std::lock_guard<std::mutex> lock(m_mutex);
			m_abandoned = true;
			if (m_control)
				m_control->cancel();
			dropped = std::move(m_published);
			m_published.reset();
			m_outcome.reset();
			m_failure.clear();
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
		report.outcome = m_outcome;
		m_outcome.reset();
		report.failure = std::move(m_failure);
		m_failure.clear();
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
			// The observer captured this job by reference, and the control outlives it (progress is
			// still read from it until the next start) — so it lets go of it here.
			control->setStageObserver({});
			// Publish at the end of EVERY run: a superseded run's finished work is kept, so it is
			// worth showing, and a failed one's is exactly what the user needs to look at.
			publish(*job, serial, true);

			// Dropped outside the lock, and here rather than on the UI thread: if the host has since
			// replaced its document, this is the last reference to the old clone and evaluation, and
			// their payloads go with them (ADR-0025's cost: a superseded run's payloads are destroyed on
			// the coordinator).
			job.reset();

			lock.lock();
			if (!m_abandoned)
			{
				m_outcome = outcome;
				m_failure = std::move(failure);
			}
			m_busy = false;
		}
	}

	RunOutcome Runner::execute(RunJob& job, std::uint64_t serial, flow::RunControl& control, std::string& failure)
	{
		// Between stages, where the evaluation is quiescent (RunControl::setStageObserver). Set before
		// the run, on this thread, which is the one that reads it.
		control.setStageObserver([this, &job, serial]()
								 { publish(job, serial, false); });

		try
		{
			// Prepare FIRST, then bind: a pin added since the last run has no slot until prepare makes
			// one, and Evaluation::bind ignores a pin it has no slot for. run() prepares again, against
			// the same clone, which finds nothing to do. No run lease is needed around this: once a job
			// holds the working evaluation, this thread is the only thing that touches it.
			job.evaluation->prepare(*job.definition);
			for (Binding& binding : job.bindings)
				job.evaluation->bind(binding.first, std::move(binding.second));

			flow::Scheduler& scheduler = (job.strategy == RunStrategy::Serial)
											 ? static_cast<flow::Scheduler&>(m_serial)
											 : static_cast<flow::Scheduler&>(m_parallel);
			scheduler.run(*job.definition, *job.evaluation, control);
			return control.cancelled() ? RunOutcome::Cancelled : RunOutcome::Completed;
		}
		// Nothing may escape this thread — an exception leaving a std::thread's body terminates the
		// process, which is what a throwing compute() did to gui-mode before this existed. The node
		// that threw stays stale (flow re-requests it on the way out), so the next run retries it.
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

	void Runner::publish(const RunJob& job, std::uint64_t serial, bool final)
	{
		// A run that failed in prepare() never paired the evaluation with this clone, so there is no
		// consistent view to copy — what the panes already have stands.
		if (job.evaluation->definition() != job.definition.get())
			return;

		{
			const std::lock_guard<std::mutex> lock(m_mutex);
			if (m_abandoned)
				return;
			if (!final && m_published.has_value())
				return; // the frame loop has not taken the last one yet: no point copying another
		}

		// The copy is taken OUTSIDE the lock — it is a refcount bump per port, but a whole evaluation's
		// worth of them — and only this thread writes the evaluation, so it cannot change underneath.
		flow::PublishedEvaluation copy{job.definition, *job.evaluation};

		std::optional<flow::PublishedEvaluation> replaced;
		{
			const std::lock_guard<std::mutex> lock(m_mutex);
			if (m_abandoned)
				return;
			replaced = std::move(m_published);
			m_published = std::move(copy);
			m_publishedBy = serial;
		}
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
