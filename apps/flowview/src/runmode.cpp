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
#include <lain/io/uri.h>
#include <lain/io/video/open.h>
#include <lain/io/video/save.h>
#include <lain/log/log.h>
#include <lain/media/frameposition.h>
#include <lain/media/framesequence.h>
#include <lain/media/serialize/manifest.h>
#include <lain/meta/enums.h>
#include <lain/meta/typenames.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
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
		static const std::set<std::string> reserved{"graph", "g", "save", "frame",
													"on-missing-frame", "codec", "rate"};

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
			// Naming a container is the explicit alternative to the default, and it is a TRANSCODE:
			// the selection's frames, encoded. This is io::video::save's production caller, which
			// is what keeps the one-shot facade from shipping unreachable.
			if (io::video::isVideoUri(path))
			{
				if (io::video::save(path, delivered.get<media::FrameSequence>()))
					return true;
				log::error("flowview: could not transcode --{} to {}", out.name, path);
				return false;
			}

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

	// One requested output, plus everything a sweep must remember about it across the range.
	//
	// A struct rather than the pair it replaces because a VIDEO output is STATEFUL: its encoder is
	// opened on the first frame that produced something and lives until the sweep ends, whereas a
	// still is a complete transaction per frame. That difference is the whole of ADR-0018's second
	// broken parallel, and it is the only reason this type exists.
	struct Write
	{
		flow::BoundaryOutput out;
		std::string path;
		bool video = false;								// io::video::isVideoUri(path), asked once
		std::unique_ptr<io::video::VideoWriter> writer; // video only; null until the first frame arrives
	};

	// The codec family named on the command line, or nullopt if the name is not one.
	//
	// OPTIONAL RATHER THAN A DEFAULT, and the difference is not theoretical: the first version
	// returned Auto for anything it could not parse, and a cli transformer that rewrote the name to
	// a number meant `--codec ffv1` silently rendered h264. Asking for one codec and getting
	// another is exactly what this option exists to prevent, so an unparseable name must REFUSE.
	// The parser validates the same names, so reaching the nullopt arm means the two lists drifted
	// — which is the case worth failing loudly on rather than absorbing.
	static std::optional<io::video::VideoCodec> codecFamily(const std::string& name)
	{
		return meta::enums::fromString<io::video::VideoCodec>(name, /*caseInsensitive=*/true);
	}

	// The rate a video output should declare.
	//
	// In order (ADR-0018): --rate if given; else the one specified rate among the BOUND sequences,
	// found BY TYPE — the same rule that finds the frame position, and for the same reason a magic
	// pin name would break the moment a linked group renames its interface. Two bound sequences
	// with two DIFFERENT specified rates is refused naming both, mirroring media::unify's refusal
	// to reconcile them: choosing one is a retime, and no host should make that call silently.
	static std::optional<media::FrameRate> outputRate(const RunOptions& options, const flow::Graph& graph,
													  const flow::Evaluation& evaluation)
	{
		if (options.outputRate)
			return options.outputRate;

		std::optional<media::FrameRate> found;
		for (const flow::BoundaryInput& in : graph.boundaryInputs())
		{
			if (in.type != typeid(media::FrameSequence))
				continue;

			const flow::PortValue& bound = evaluation.value(in.port);
			if (!bound.holds<media::FrameSequence>())
				continue;

			const media::FrameRate rate = bound.get<media::FrameSequence>().spec().rate;
			if (!rate.specified())
				continue;

			if (found && *found != rate)
			{
				log::error("flowview: bound sequences declare {} and {} — pick one with --rate, "
						   "because reconciling them is a retime",
						   found->toString(), rate.toString());
				return std::nullopt;
			}
			found = rate;
		}

		if (!found)
		{
			// Two different situations, one message, because the fix is the same and the
			// distinction is the caller's to see: there may be no sequence input at all, or there
			// may be one that simply has no rate — a folder of stills genuinely has none, which is
			// the common case and would read as a bug if this claimed the input was missing.
			log::error("flowview: no bound frame sequence declares a rate (a folder of stills has none), "
					   "so --rate is required to write a video");
		}
		return found;
	}

	// Finish every open video writer. THE ONLY PLACE THEY ARE FINISHED, which is why sweep() funnels
	// every exit through it: ADR-0018's default policy is "finalise what exists, report the ordinal,
	// exit non-zero", and a finish() that has to be remembered at each failure path is one that will
	// eventually be forgotten on the path that matters — the failing one. A file that is never
	// finalised is not a video at all, which is worse than the truncation it was meant to report.
	static int finishWrites(std::vector<Write>& writes)
	{
		int status = 0;
		for (Write& write : writes)
		{
			if (!write.writer)
				continue;
			if (!write.writer->finish())
			{
				log::error("flowview: could not finalise --{} at {}", write.out.name, write.path);
				status = 1;
			}
			else
			{
				log::info("flowview: wrote --{} to {}", write.out.name, write.path);
			}
			write.writer.reset();
		}
		return status;
	}

	// One frame's worth of output for one write. Returns false to stop the render.
	static bool writeFrame(Write& write, std::size_t frame, const flow::Evaluation& evaluation,
						   const RunOptions& options, const flow::Graph& graph, bool& preflighted)
	{
		const flow::PortValue& delivered = evaluation.value(write.out);

		if (!write.video)
		{
			const std::string path = frameOutputPath(write.path, frame);
			if (!preflighted && !encodableHere(write.out, path, evaluation))
				return false;
			return writeBoundaryValue(write.out, path, evaluation);
		}

		if (!delivered.holds<image::Image>())
		{
			log::error("flowview: --{} is a video output but delivered {}, which is not an image",
					   write.out.name, evaluation.describe(write.out.port));
			return false;
		}

		if (!write.writer)
		{
			// THE PREFLIGHT, on the first frame that produced something — the video peer of
			// encodableHere, arrived at by a different route because opening an encoder already
			// answers the question. The spec comes from this frame and fixes every later one
			// (ADR-0018); a refusal stops the render before a second frame is computed.
			const std::optional<media::FrameRate> rate = outputRate(options, graph, evaluation);
			if (!rate)
				return false;

			const std::optional<io::video::VideoCodec> family = codecFamily(options.videoCodec);
			if (!family)
			{
				log::error("flowview: '{}' is not a codec family", options.videoCodec);
				return false;
			}

			const media::FrameSpec spec = media::specOf(delivered.get<image::Image>(), *rate);
			io::video::VideoWriterOptions writerOptions;
			writerOptions.codec = *family;

			write.writer = io::video::openWriter(write.path, spec, writerOptions);
			if (!write.writer)
				return false; // openWriter named the reason: the spec, the container or the codec
		}

		return write.writer->write(delivered.get<image::Image>());
	}

	// Run the graph once per frame over `range`, rebinding the frame position each time.
	//
	// This is the RENDER of ADR-0018: a fold, not a map. It consumes frames one at a time and
	// produces files, so memory is one frame's worth at any range length — which is the whole reason
	// the host owns the loop instead of a map materialising 500 decoded frames.
	static int sweepFrames(const core::Range& range, const RunOptions& options, const flow::Graph& graph,
						   flow::Evaluation& evaluation, flow::Scheduler& scheduler,
						   const std::vector<flow::BoundaryInput>& positions, std::vector<Write>& writes,
						   std::size_t& written)
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

		// TWO RULES, ONE HOME, and they are exact inverses. A STILL output must be able to name a
		// frame whenever there is more than one, or the render silently overwrites one file per
		// iteration and exits reporting success. A VIDEO output must NOT carry a #### field: one
		// container holds the whole range, so a numbered pattern asks for one video per frame,
		// which is not a thing. A ONE-frame range exempts the first rule — `--frame 5 --result
		// out.png` is unambiguous, and demanding a #### field there would be ceremony.
		for (const Write& write : writes)
		{
			if (write.video && isFramePattern(write.path))
			{
				log::error("flowview: --{} is '{}', but a video holds the whole range in one file — "
						   "drop the #### field",
						   write.out.name, write.path);
				return 1;
			}
			if (!write.video && range.count() > 1 && !isFramePattern(write.path))
			{
				log::error("flowview: --{} is '{}', which has no #### field — a {}-frame render needs one per frame",
						   write.out.name, write.path, range.count());
				return 1;
			}
		}

		bool preflighted = false;

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

			for (Write& write : writes)
			{
				if (producedNothing(write.out, evaluation))
				{
					// A suppressed frame. The policy belongs to the HOST, not to the writer: the
					// default stops, reports the ordinal and exits non-zero, because a truncated
					// render must be visibly truncated.
					if (options.skipMissingFrames)
					{
						// The asymmetry ADR-0018 ends on. Numbered stills represent a hole as a gap
						// in the numbering, which is itself a record. A video CANNOT, so the gap
						// closes up: the output is one frame shorter and everything after it has
						// moved earlier in time. Said out loud, because the file cannot say it.
						if (write.video)
						{
							log::warn("flowview: frame {} produced no --{} — dropped. A video cannot hold a "
									  "gap, so the output is a frame shorter and no longer lines up with the source.",
									  frame, write.out.name);
						}
						else
						{
							log::warn("flowview: frame {} produced no --{} — skipped, leaving a gap", frame,
									  write.out.name);
						}
						continue;
					}
					log::error("flowview: frame {} produced no --{} — stopping after {} frame(s). Use --on-missing-frame skip to continue.",
							   frame, write.out.name, written);
					return 1;
				}

				if (!writeFrame(write, frame, evaluation, options, graph, preflighted))
					return 1;
			}
			preflighted = true;
			++written;
		}

		return 0;
	}

	// sweep() OWNS the writers; sweepFrames() may return early from anywhere. Deliberate structure
	// rather than a rule at five return sites — see finishWrites.
	static int sweep(const core::Range& range, const RunOptions& options, const flow::Graph& graph,
					 flow::Evaluation& evaluation, flow::Scheduler& scheduler,
					 const std::vector<flow::BoundaryInput>& positions,
					 const std::vector<std::pair<flow::BoundaryOutput, std::string>>& requested)
	{
		std::vector<Write> writes;
		writes.reserve(requested.size());
		for (const auto& [out, path] : requested)
		{
			// isVideoUri is asked ONCE, of the seam that owns the container-name claim — deriving it
			// a second way here would let this decision and the write disagree about what a path
			// means, which is the shape that keeps producing bugs in this repo.
			writes.push_back(Write{out, path, io::video::isVideoUri(path), nullptr});
		}

		std::size_t written = 0;
		const int status = sweepFrames(range, options, graph, evaluation, scheduler, positions, writes, written);
		const int finished = finishWrites(writes);

		log::info("flowview: rendered {} frame(s)", written);
		return std::max(status, finished);
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
