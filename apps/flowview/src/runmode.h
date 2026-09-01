#pragma once

#include "clibinders.h"

#include <lain/core/factory.h>
#include <lain/core/range.h>
#include <lain/flow/node.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lain::flow
{
	class Graph;
}

namespace flowview
{
	// What a headless `run` was asked to do. A struct rather than a parameter list because the
	// sweep took it past the point where positional arguments read: six was already a pile, and
	// `--frame` plus `--on-missing-frame` would have made it eight.
	struct RunOptions
	{
		std::string graphPath; // graph JSON, or empty for the built-in example
		std::string savePath;  // also serialize the recipe here, if set
		// Set = a render over these frames; unset = a single run. An optional rather than a sentinel
		// range, because "no range given" and "the range 0-0" are different requests.
		std::optional<lain::core::Range> frameRange;
		bool skipMissingFrames = false;	   // --on-missing-frame skip; default is stop
		std::vector<std::string> bindings; // the --<boundary> <value> extras
		std::uint32_t exampleSize = 64;
	};

	// The headless subcommands. Both build the example scene when `graphPath` is empty (so they run
	// with no file), else load it. Assume the caller has already registered the example nodes, the
	// scene serialization (port types + json), and the image codecs.

	// `run`: bind the graph's boundary inputs from the `--<name> <value>` bindings, run it, dump it
	// to stdout, and write bound outputs. With `frameRange` set it becomes a RENDER: the graph runs
	// once per frame over the range, with the frame position rebound each iteration and ONE
	// Evaluation retained across the whole sweep, and each iteration's outputs written through a
	// ####-numbered pattern.
	//
	// Returns a process exit code: 0, or 1 when the graph could not be loaded, a binding was
	// refused, an output could not be written, or a render stopped on a suppressed frame. A
	// truncated render must be distinguishable from a complete one by a caller that only sees the
	// exit status.
	int runGraph(const RunOptions& options, const lain::core::Factory<lain::flow::Node>& factory,
				 const BoundaryBinders& binders);

	// `list`: print the graph's boundary inputs / outputs as `--<name> : <type>` (the flags `run`
	// accepts), marking any input whose type has no cli binder, and reporting whether the graph is
	// RENDERABLE (it has a frame-position input, so `--frame` can drive it). Returns 0, or 1 if the
	// graph couldn't be loaded.
	int listGraph(const std::string& graphPath, const lain::core::Factory<lain::flow::Node>& factory,
				  const BoundaryBinders& binders);
} // namespace flowview
