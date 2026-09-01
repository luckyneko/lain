#include "runmode.h"

#include "clibinders.h"
#include "dump.h"
#include "framepattern.h"
#include "graphio.h"
#include "scene.h"

#include <lain/flow/boundary.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/graph.h>
#include <lain/flow/scheduler.h>
#include <lain/image/image.h>
#include <lain/io/data/save.h>
#include <lain/io/image/save.h>
#include <lain/log/log.h>
#include <lain/media/frameposition.h>
#include <lain/media/framesequence.h>
#include <lain/media/serialize/manifest.h>
#include <lain/meta/typenames.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace flowview
{
	using namespace lain;

	// --- file-local helpers ---------------------------------------------------

	// Build the example scene when `graphPath` is empty, else load it. Returns whether a usable
	// graph resulted (a load that produced no nodes is a failure).
	static bool buildOrLoad(flow::Graph& graph, const std::string& graphPath, const core::Factory<flow::Node>& factory)
	{
		if (graphPath.empty())
		{
			buildExampleScene(graph, factory);
			return true;
		}
		// A cache for this one load: a headless run never re-opens a document, but a graph that links
		// one template several times still gets one definition instead of N copies of it.
		flow::serialize::TemplateCache templates;
		flow::serialize::LoadResult result = loadGraph(graphPath, factory, &templates);
		if (result.graph.nodeCount() == 0)
		{
			log::error("flowview: nothing loaded from {}", graphPath);
			return false;
		}
		graph = std::move(result.graph);
		return true;
	}

	// Parse "--name value --name2 value2" tokens into a name->value map, warning on malformed input.
	static std::map<std::string, std::string> parseBindings(const std::vector<std::string>& tokens)
	{
		std::map<std::string, std::string> out;
		for (std::size_t i = 0; i < tokens.size();)
		{
			const std::string& flag = tokens[i];
			if (flag.rfind("--", 0) != 0)
			{
				log::warn("flowview: ignoring stray argument '{}'", flag);
				++i;
				continue;
			}
			const std::string name = flag.substr(2);
			if (i + 1 >= tokens.size() || tokens[i + 1].rfind("--", 0) == 0)
			{
				log::warn("flowview: boundary '{}' given no value", name);
				++i;
				continue;
			}
			out[name] = tokens[i + 1];
			i += 2;
		}
		return out;
	}

	// Warn about boundary names a caller cannot actually address from the command line.
	static void warnBoundaryCollisions(const flow::Graph& graph)
	{
		// `run`'s own registered options. CLI11 consumes these before the extras are collected, so a
		// boundary pin named after one is simply unreachable — and silently so, which is why it is
		// worth saying out loud rather than leaving to be discovered.
		static const std::set<std::string> reserved{"graph", "g", "save", "frame", "on-missing-frame"};

		std::set<std::string> inputs;
		for (const flow::BoundaryInput& in : graph.boundaryInputs())
		{
			inputs.insert(in.name);
			if (reserved.count(in.name) != 0)
				log::warn("flowview: boundary '{}' shares a name with one of run's own options — --{} cannot reach it", in.name, in.name);
		}
		for (const flow::BoundaryOutput& out : graph.boundaryOutputs())
		{
			if (inputs.count(out.name) != 0)
				log::warn("flowview: boundary '{}' is both an input and an output — --{} is ambiguous", out.name, out.name);
			if (reserved.count(out.name) != 0)
				log::warn("flowview: boundary '{}' shares a name with one of run's own options — --{} cannot reach it", out.name, out.name);
		}
	}

	// Bind one boundary input from a cli value via the type's registered binder. A failed parse and
	// an unbindable type are distinct diagnostics.
	static bool bindBoundaryInput(flow::Evaluation& evaluation, const flow::BoundaryInput& in,
								  const std::string& value, const BoundaryBinders& binders)
	{
		if (auto bound = binders.bind(in.type, value))
		{
			evaluation.bind(in, std::move(*bound));
			return true;
		}
		if (binders.has(in.type))
			log::error("flowview: could not parse '{}' for --{} as {}", value, in.name, in.typeName);
		else
			log::error("flowview: --{} has type {}, which is not cli-bindable", in.name, in.typeName);
		return false;
	}

	// The boundary inputs a sweep drives. Found BY TYPE, never by pin name: a magic name breaks the
	// moment a linked group renames its interface, and being able to find the loop counter without
	// one is the reason FramePosition is a distinct type at all (ADR-0018).
	static std::vector<flow::BoundaryInput> framePositionInputs(const flow::Graph& graph)
	{
		std::vector<flow::BoundaryInput> found;
		for (const flow::BoundaryInput& in : graph.boundaryInputs())
		{
			if (in.type == typeid(media::FramePosition))
				found.push_back(in);
		}
		return found;
	}

	// Whether this output has nothing usable for the current frame.
	//
	// Two shapes mean the same thing to a render, and the policy has to cover both: the graph
	// SUPPRESSED the output (no value at all — a gate turned off, a node whose input never arrived,
	// ADR-0007), or it delivered an image that did not decode (a position past the end of the
	// sequence, a frame the codec refused). Only the first is suppression in flow's sense, but from
	// the outside both are "frame N did not render", and treating the second as a write error would
	// report the wrong cause and bypass --on-missing-frame entirely.
	static bool producedNothing(const flow::BoundaryOutput& out, const flow::Evaluation& evaluation)
	{
		const flow::PortValue& delivered = evaluation.value(out);
		if (delivered.empty())
			return true;
		if (delivered.holds<image::Image>())
			return !delivered.get<image::Image>().valid();
		return false;
	}

	// Write one delivered boundary value to `path`. Returns false when it could not be written;
	// an EMPTY value is not this function's business — the caller decides what suppression means.
	static bool writeBoundaryValue(const flow::BoundaryOutput& out, const std::string& path,
								   const flow::Evaluation& evaluation)
	{
		const flow::PortValue& delivered = evaluation.value(out);

		if (delivered.holds<image::Image>())
		{
			if (io::image::save(path, delivered.get<image::Image>()))
				return true;
			log::error("flowview: could not write --{} to {}", out.name, path);
			return false;
		}

		if (delivered.holds<media::FrameSequence>())
		{
			// A sequence is written as a MANIFEST, not as pixels: a sequence-valued output is
			// necessarily a selection, and what it selected is the thing worth recording.
			if (io::data::save(path, data::toValue(media::manifestOf(delivered.get<media::FrameSequence>()))))
				return true;
			log::error("flowview: could not write --{} manifest to {}", out.name, path);
			return false;
		}

		// A scalar / text output: write its value as text, symmetric with an image to a file.
		std::ofstream file(path);
		if (!file)
		{
			log::error("flowview: could not write --{} to {}", out.name, path);
			return false;
		}
		file << evaluation.describe(out.port) << '\n';
		return true;
	}

	// Refuse a sweep whose image output could never be encoded, ONCE, against the first frame —
	// the way the gui's save panel pre-filters formats. Discovering it per frame would emit the
	// same error N times and still leave nothing written.
	static bool encodableHere(const flow::BoundaryOutput& out, const std::string& path,
							  const flow::Evaluation& evaluation)
	{
		const flow::PortValue& delivered = evaluation.value(out);
		if (!delivered.holds<image::Image>())
			return true;

		const std::string key = io::image::formatKeyOf(path);
		if (io::image::canEncode(key, delivered.get<image::Image>()))
			return true;

		log::error("flowview: --{} cannot be written as '{}' without loss — refusing the whole render rather than {} failed frames",
				   out.name, key, out.name);
		return false;
	}

	// Run the graph once per frame over `range`, rebinding the frame position each time.
	//
	// This is the RENDER of ADR-0018: a fold, not a map. It consumes frames one at a time and
	// produces files, so memory is one frame's worth at any range length — which is the whole reason
	// the host owns the loop instead of a map materialising 500 decoded frames.
	static int sweep(const core::Range& range, const RunOptions& options, const flow::Graph& graph,
					 flow::Evaluation& evaluation, flow::Scheduler& scheduler,
					 const std::vector<flow::BoundaryInput>& positions,
					 const std::vector<std::pair<flow::BoundaryOutput, std::string>>& writes)
	{
		// The range arrived already parsed: `--frame` is a typed option, so a malformed one was
		// refused by the command line before a graph was ever loaded.
		if (positions.empty())
		{
			// Nothing to drive. Sweeping anyway would run the same graph N times and write N
			// identical files, which looks like success and is not.
			log::error("flowview: --frame was given but the graph has no {} input to drive",
					   meta::typeName<media::FramePosition>());
			return 1;
		}

		// An output must be able to name a frame whenever there is more than one, or the render
		// silently overwrites one file per iteration and exits reporting success — destroying the
		// correspondence between input and output frames, which is what the missing-frame policy
		// exists to prevent. A ONE-frame range is exempt: `--frame 5 --result out.png` is
		// unambiguous, and demanding a #### field there would be ceremony.
		if (range.count() > 1)
		{
			for (const auto& [out, path] : writes)
			{
				if (!isFramePattern(path))
				{
					log::error("flowview: --{} is '{}', which has no #### field — a {}-frame render needs one per frame",
							   out.name, path, range.count());
					return 1;
				}
			}
		}

		int status = 0;
		bool preflighted = false;
		std::size_t written = 0;

		for (std::size_t frame = range.first; frame <= range.last; frame += range.step)
		{
			for (const flow::BoundaryInput& pin : positions)
			{
				flow::PortValue value;
				value.set<media::FramePosition>(media::FramePosition{frame});
				// bind() already marks the boundary node for recompute, so there is no
				// requestRecompute here — and deliberately no requestRecomputeAll, which would make
				// every node stale every frame and re-open the sequence each iteration.
				evaluation.bind(pin, std::move(value));
			}

			scheduler.run(graph, evaluation);

			// Only the first frame is dumped. The dump is the "it actually ran" evidence, and it is
			// the whole graph per frame — useful once, unreadable 500 times.
			if (frame == range.first)
				dumpGraph(std::cout, graph, evaluation);

			for (const auto& [out, path] : writes)
			{
				if (producedNothing(out, evaluation))
				{
					// A suppressed frame. The policy belongs to the HOST, not to the writer: the
					// default stops, reports the ordinal and exits non-zero, because a truncated
					// render must be visibly truncated. Skipping is opt-in, and for numbered stills
					// the gap in the numbering is itself the record — which is why a video, which
					// cannot represent a hole, will need the same flag to mean something stricter.
					if (options.skipMissingFrames)
					{
						log::warn("flowview: frame {} produced no --{} — skipped, leaving a gap", frame, out.name);
						continue;
					}
					log::error("flowview: frame {} produced no --{} — stopping after {} frame(s). Use --on-missing-frame skip to continue.",
							   frame, out.name, written);
					return 1;
				}

				if (!preflighted && !encodableHere(out, frameOutputPath(path, frame), evaluation))
					return 1;

				if (!writeBoundaryValue(out, frameOutputPath(path, frame), evaluation))
					return 1;
			}
			preflighted = true;
			++written;
		}

		log::info("flowview: rendered {} frame(s)", written);
		return status;
	}

	// --- subcommands ----------------------------------------------------------

	int runGraph(const RunOptions& options, const core::Factory<flow::Node>& factory, const BoundaryBinders& binders)
	{
		flow::Graph graph;
		if (!buildOrLoad(graph, options.graphPath, factory))
			return 1;

		// The definition and its runtime state, created together and used together — and for a
		// sweep, ONE Evaluation retained across the whole range. A fresh one per frame would restart
		// every node's recorded version, so nothing would be incremental and the sequence would be
		// re-opened and cold-seeked every iteration (ADR-0018).
		flow::Evaluation evaluation{graph};

		warnBoundaryCollisions(graph);
		std::map<std::string, std::string> args = parseBindings(options.bindings);

		// Hoisted: boundaryInputs()/boundaryOutputs() build their vectors on every call, and a sweep
		// would otherwise rebuild them once per frame.
		const std::vector<flow::BoundaryInput> inputs = graph.boundaryInputs();
		const std::vector<flow::BoundaryOutput> outputs = graph.boundaryOutputs();
		const std::vector<flow::BoundaryInput> positions = framePositionInputs(graph);

		int status = 0;

		// Bind inputs from --<name>; an unbound image input on the example gets a default gradient
		// (so it runs out of the box), on a loaded graph is left empty with a warning.
		for (const flow::BoundaryInput& in : inputs)
		{
			const auto arg = args.find(in.name);
			if (arg != args.end())
			{
				if (!bindBoundaryInput(evaluation, in, arg->second, binders))
					status = 1;
				args.erase(arg);
			}
			else if (options.graphPath.empty() && bindDefaultInput(in, evaluation, options.exampleSize))
			{
				// bound a stand-in gradient
			}
			else if (in.type == typeid(media::FramePosition) && options.frameRange.has_value())
			{
				// The sweep is about to drive this pin; leaving it unbound here is correct.
			}
			else
			{
				log::warn("flowview: input --{} not bound — leaving it empty", in.name);
			}
		}

		// Collect the output bindings once — which boundary outputs were asked for, and where.
		std::vector<std::pair<flow::BoundaryOutput, std::string>> writes;
		for (const flow::BoundaryOutput& out : outputs)
		{
			const auto arg = args.find(out.name);
			if (arg == args.end())
				continue;
			writes.emplace_back(out, arg->second);
			args.erase(arg);
		}
		for (const auto& [name, value] : args)
			log::warn("flowview: --{} matched no boundary (value '{}')", name, value);

		flow::SerialScheduler scheduler;

		if (!options.frameRange.has_value())
		{
			// A single run: unchanged behaviour, and still the common case.
			scheduler.run(graph, evaluation);
			dumpGraph(std::cout, graph, evaluation);

			for (const auto& [out, path] : writes)
			{
				if (evaluation.value(out).empty())
				{
					// Its producer was gated off / suppressed (conditional eval, ADR-0007): no value
					// this run. A legitimate outcome, not an error — report it and write nothing.
					log::info("flowview: --{} produced no output this run — nothing written", out.name);
				}
				else if (writeBoundaryValue(out, path, evaluation))
				{
					log::info("flowview: wrote --{} to {}", out.name, path);
				}
				else
				{
					status = 1;
				}
			}
		}
		else
		{
			status = std::max(status, sweep(*options.frameRange, options, graph, evaluation, scheduler, positions, writes));
		}

		if (!options.savePath.empty())
		{
			if (saveGraph(options.savePath, graph, factory))
				log::info("flowview: wrote graph to {}", options.savePath);
			else
				status = 1;
		}
		return status;
	}

	int listGraph(const std::string& graphPath, const core::Factory<flow::Node>& factory, const BoundaryBinders& binders)
	{
		flow::Graph graph;
		if (!buildOrLoad(graph, graphPath, factory))
			return 1;

		warnBoundaryCollisions(graph);
		std::cout << "inputs:\n";
		for (const flow::BoundaryInput& in : graph.boundaryInputs())
		{
			std::cout << "  --" << in.name << " : " << in.typeName;
			if (!binders.has(in.type))
				std::cout << "  (not cli-bindable)";
			std::cout << "\n";
		}
		std::cout << "outputs:\n";
		for (const flow::BoundaryOutput& out : graph.boundaryOutputs())
			std::cout << "  --" << out.name << " : " << out.typeName << "\n";

		// Reported by TYPE, which is what a distinct FramePosition buys: the cli can say a graph is
		// renderable without knowing anything about how its pins are named.
		const std::vector<flow::BoundaryInput> positions = framePositionInputs(graph);
		if (!positions.empty())
			std::cout << "renderable: yes (--frame drives " << positions.size() << " frame-position input(s))\n";
		return 0;
	}
} // namespace flowview
