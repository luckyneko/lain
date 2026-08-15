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
		// Both settings are also INPUT PINS, Optional and overriding their params (see setting.h):
		// wire a boundary input or a Constant node to drive them from the graph, or leave them
		// unconnected and set them in the inspector. Without the pins the folder could only ever be
		// configured per node, which a map cannot use — every element shares one definition.
		PortId m_dirIn;	   // optional std::filesystem::path input
		PortId m_filterIn; // optional std::string input
		PortId m_dir;	   // "directory" param (std::filesystem::path)
		PortId m_filter;   // "extension" param — "" lists everything
		PortId m_out;	   // std::vector<std::filesystem::path>
	};
} // namespace lain::flow::example
