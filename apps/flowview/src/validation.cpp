#include "validation.h"

#include <lain/flow/boundary.h> // the inner GroupOutput a map element delivers to
#include <lain/flow/evaluation.h>
#include <lain/flow/graph.h>
#include <lain/flow/node.h>
#include <lain/flow/nodes/cast.h> // CastNode — an unconvertible pair is a row, not a silent empty
#include <lain/flow/port.h>
#include <lain/string/format.h>

#include <cstddef>
#include <string>
#include <unordered_set>
#include <vector>

namespace flowview
{
	using namespace lain;

	// Live validation of the current graph: a required input with no incoming edge (can't run), and an
	// active node's output with no outgoing edge (a dead end). Recomputed each frame — cheap at
	// prototyping scale, and self-clearing as the graph is fixed.
	std::vector<Issue> collectIssues(const flow::Graph& graph, const flow::Evaluation& evaluation,
									 const GraphPath& activePath)
	{
		std::vector<Issue> issues;
		// The ports an edge touches, by their durable addresses — the same key an edge stores.
		std::unordered_set<flow::PortAddress> connectedIn;
		std::unordered_set<flow::PortAddress> connectedOut;
		for (const flow::Graph::Edge& e : graph.edges())
		{
			connectedIn.insert(e.to);
			connectedOut.insert(e.from);
		}
		for (const flow::NodeId id : graph.topoOrder())
		{
			const flow::Node& node = graph.node(id);
			// A row names the node by title plus a truncated id — enough to tell two same-titled
			// nodes apart, where the full uuid would bury the message.
			const std::string label = id.shortString();
			for (std::size_t i = 0; i < node.inputCount(); ++i)
			{
				const flow::Port& in = node.input(i);
				// A DEFAULTED input is Required BY DESIGN — that is what keeps a default from swallowing
				// suppression (ADR-0007: a Gate wired in and turned off must leave this slot empty, not
				// fall back) — and populateInputs seeds it from its param whenever nothing is wired. So it
				// always carries a value and can never be why a node cannot run. Reporting one made every
				// Blur, Gate, Select and Loop in every document name a problem it does not have.
				//
				// Asked of the NODE, because that is where the fact lives: a Port does not know what
				// stands behind it, which is also why `required()` alone cannot answer this.
				if (in.required() && node.defaultOf(in.id()) == nullptr && connectedIn.count({id, in.id()}) == 0)
				{
					issues.push_back(Issue::at(Issue::Severity::Warning,
											   string::format("{} [{}]: required input '{}' is not connected", node.name(), label, in.name()),
											   id));
				}
			}
			if (evaluation.ready(id))
			{
				for (std::size_t o = 0; o < node.outputCount(); ++o)
				{
					const flow::Port& out = node.output(o);
					if (connectedOut.count({id, out.id()}) == 0)
					{
						issues.push_back(Issue::at(Issue::Severity::Info,
												   string::format("{} [{}]: output '{}' is unused", node.name(), label, out.name()),
												   id));
					}
				}
			}

			// A CAST whose two payload types have no conversion between them. It produces nothing
			// and suppresses everything downstream, and without this row the only symptom is an
			// empty result — the same shape the map hole below is reported for.
			//
			// This one asks the CLASS, where the map below asks a structural fact
			// (interiorEvaluation). There is no structural fact to ask: nothing in general says two
			// of a node's payload types must be connected by a conversion, and inventing a virtual
			// for one caller would be a seam with nothing behind it. The adapter already names every
			// node kind — its factory, its palette, its canvas colours — so a cast here is not the
			// layering break the same line would be in the scheduler.
			if (const auto* cast = dynamic_cast<const flow::CastNode*>(&node); cast != nullptr && !cast->canConvert())
			{
				issues.push_back(Issue::at(Issue::Severity::Warning,
										   string::format("{} [{}]: no conversion from {} to {} — it produces nothing",
														  node.name(), label,
														  cast->payloadType(flow::CastNode::kFromPayload)->name,
														  cast->payloadType(flow::CastNode::kToPayload)->name),
										   id));
			}

			// A MAP whose gather found a HOLE. One element producing nothing clears the WHOLE output
			// (ADR-0014: a vector has no hole, and shortening it would break the correspondence with
			// the input), so without this the user sees an empty result and no reason for it.
			//
			// The engine reports no element index itself — it has no channel to, and flow core stays
			// log-free — but the child evaluations are right here, so the host simply looks. One row
			// per output rather than one per failed element: a systematically broken folder would
			// otherwise bury the panel under a row per file.
			if (node.interiorEvaluation() == flow::InteriorEvaluation::PerElement && node.innerGraph() != nullptr)
			{
				const flow::NodeId boundary = node.innerGraph()->boundaryOutputNode().id();
				const std::size_t elements = evaluation.childCount(id);
				for (std::size_t o = 0; o < node.outputCount(); ++o)
				{
					const flow::Port& out = node.output(o);
					const flow::PortId pin = node.innerPin(out.id());
					if (pin == flow::PortId{})
						continue;

					std::size_t missing = 0;
					std::size_t first = 0;
					for (std::size_t element = 0; element < elements; ++element)
					{
						if (!evaluation.child(id, element).value(flow::PortAddress{boundary, pin}).empty())
							continue;
						if (missing == 0)
							first = element;
						++missing;
					}
					if (missing == 0)
						continue;

					GraphPath into = activePath;
					into.push_back(PathStep{id, first});
					// `inside`, not `at`: the subject is the ELEMENT's level, one step down, and there is
					// no node at this level worth centring — the map itself is plainly visible.
					issues.push_back(Issue::inside(Issue::Severity::Warning,
												   string::format("{} [{}]: {} of {} elements produced no '{}' — the whole output is cleared (first: element {})",
																  node.name(), label, missing, elements, out.name(), first + 1),
												   std::move(into)));
				}
			}
		}
		return issues;
	}
} // namespace flowview
