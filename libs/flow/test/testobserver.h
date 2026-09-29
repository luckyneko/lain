#pragma once

// A RunObserver that records every report a run makes, in the order it arrives, from any thread —
// shared by the suites that watch a run (M14 slice 7, ADR-0025): what a run reports, and what a
// host can rebuild from those reports alone.
//
// Kept beside testscene.h, so a suite that watches a run includes it rather than growing a copy.

#include "lain/flow/evaluation.h"
#include "lain/flow/graph.h"
#include "lain/flow/runcontrol.h"
#include "lain/flow/types.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lain::flow::test
{
	// One report, as the observer received it.
	struct Report
	{
		enum class Kind
		{
			Stage,	  // stageFinished()
			Owed,	  // owed()
			Started,  // started()
			Finished, // finished() with a record
			GaveUp,	  // finished() with no record
		};

		Kind kind;
		EvalPath path;
		NodeId node;
		std::optional<NodeRecord> record; // Finished only
	};

	// Records every report. Several workers report at once under ParallelScheduler, so each one takes
	// the lock — which also orders them: a report made after another in the run is recorded after it.
	struct Recorder : RunObserver
	{
		std::mutex mutex;
		std::vector<Report> reports;

		void stageFinished() override { add(Report{Report::Kind::Stage, EvalPath{}, NodeId{}, std::nullopt}); }
		void owed(const EvalPath& path, NodeId node) noexcept override
		{
			add(Report{Report::Kind::Owed, path, node, std::nullopt});
		}
		void started(const EvalPath& path, NodeId node) noexcept override
		{
			add(Report{Report::Kind::Started, path, node, std::nullopt});
		}
		void finished(const EvalPath& path, NodeId node, const NodeRecord* record) noexcept override
		{
			if (record != nullptr)
				add(Report{Report::Kind::Finished, path, node, *record});
			else
				add(Report{Report::Kind::GaveUp, path, node, std::nullopt});
		}

		// How many reports of `kind` name (path, node).
		std::size_t count(Report::Kind kind, const EvalPath& path, NodeId node) const
		{
			std::size_t matching = 0;
			for (const Report& report : reports)
			{
				if (report.kind == kind && report.path == path && report.node == node)
					++matching;
			}
			return matching;
		}

		// The index of the first report of `kind` naming (path, node), or reports.size() if none.
		std::size_t first(Report::Kind kind, const EvalPath& path, NodeId node) const
		{
			for (std::size_t i = 0; i < reports.size(); ++i)
			{
				const Report& report = reports[i];
				if (report.kind == kind && report.path == path && report.node == node)
					return i;
			}
			return reports.size();
		}

		// The last Finished record naming (path, node), or nullptr.
		const NodeRecord* lastRecord(const EvalPath& path, NodeId node) const
		{
			const NodeRecord* found = nullptr;
			for (const Report& report : reports)
			{
				if (report.kind == Report::Kind::Finished && report.path == path && report.node == node)
					found = &*report.record;
			}
			return found;
		}

	private:
		void add(Report report)
		{
			const std::lock_guard<std::mutex> lock(mutex);
			reports.push_back(std::move(report));
		}
	};

	// What a host does with the reports: replay reports [begin, end) onto a copy, in the order they
	// were made — each owed node marked, each record folded in. Every mark and fold is asserted to
	// land, since one that silently missed would make a comparison built on this prove nothing.
	inline void replay(PublishedEvaluation& copy, const std::vector<Report>& reports, std::size_t begin,
					   std::size_t end)
	{
		for (std::size_t i = begin; i < end; ++i)
		{
			const Report& report = reports[i];
			if (report.kind == Report::Kind::Owed)
				REQUIRE(copy.owe(report.path, report.node));
			else if (report.kind == Report::Kind::Finished)
				REQUIRE(copy.fold(report.path, report.node, *report.record));
		}
	}

	inline void replay(PublishedEvaluation& copy, const std::vector<Report>& reports)
	{
		replay(copy, reports, 0, reports.size());
	}

	// Whether two evaluations of one definition SHOW the same thing, at every level: the same payload
	// on every port (the same object — a fold copies a pointer, never a value), the same answer to
	// "does this node need recomputing?", the same failure, and the same children. A weaker check
	// (equal ints somewhere) passes just as well when a fold landed the wrong record.
	inline void requireSameShown(const Graph& definition, const Evaluation& a, const Evaluation& b)
	{
		for (const NodeId id : definition.nodeIds())
		{
			const Node& node = definition.node(id);
			REQUIRE(a.needsRecompute(id) == b.needsRecompute(id));
			const std::string* failureA = a.failure(id);
			const std::string* failureB = b.failure(id);
			REQUIRE((failureA == nullptr) == (failureB == nullptr));
			for (std::size_t i = 0; i < node.inputCount(); ++i)
			{
				const PortAddress port{id, node.input(i).id()};
				REQUIRE(a.value(port).samePayload(b.value(port)));
			}
			for (std::size_t o = 0; o < node.outputCount(); ++o)
			{
				const PortAddress port{id, node.output(o).id()};
				REQUIRE(a.value(port).samePayload(b.value(port)));
			}

			REQUIRE(a.childCount(id) == b.childCount(id));
			if (const Graph* inner = node.innerGraph())
			{
				for (std::size_t c = 0; c < a.childCount(id); ++c)
					requireSameShown(*inner, a.child(id, c), b.child(id, c));
			}
		}
	}
} // namespace lain::flow::test
