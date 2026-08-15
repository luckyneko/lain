#pragma once

#include <lain/flow/evaluation.h>
#include <lain/flow/node.h>

#include <filesystem>
#include <string>
#include <vector>

namespace lain::flow::example
{
	// A COLLECTION source: lists a directory and emits its files as one std::vector<path> value.
	//
	// This is the node that makes a map worth having (M8 / ADR-0014). A map's arity comes from a
	// collection computed DURING the run, and this is where that collection comes from — which is why
	// the scheduler has to plan in stages rather than once: nothing can know how many files are in a
	// folder until this node has run.
	//
	// Deliberately a plain vector-valued output, not a stream: whole-asset listing is what
	// io::read already assumes, and a Stream transport for video is a separate, deferred piece.
	//
	// Entries are SORTED, so a run is reproducible and element 3 means the same file twice running —
	// which matters because a map's children are identified positionally.
	class ListDirNode : public Node
	{
	public:
		explicit ListDirNode(std::string directory = {});

		void compute(NodeEvaluation& evaluation) const override;

	private:
		// Inputs WITH DEFAULTS (Node::addInput(name, Default{…})): wire a boundary input or a Constant
		// node to drive them from the graph, or leave them unconnected and set them in the inspector.
		// Configuration alone would not do — every element of a map shares one definition, so a folder
		// that must vary has to arrive as a value.
		PortId m_dir;	 // std::filesystem::path — the folder to list
		PortId m_filter; // std::string — "" lists everything
		PortId m_out;	 // std::vector<std::filesystem::path>
	};
} // namespace lain::flow::example
