#include "runmode.h"

#include "clibinders.h"
#include "dump.h"
#include "graphio.h"
#include "scene.h"

#include <lain/flow/boundary.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/graph.h>
#include <lain/flow/scheduler.h>
#include <lain/image/image.h>
#include <lain/io/image/save.h>
#include <lain/log/log.h>

#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <utility>

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
		flow::serialize::LoadResult result = loadGraph(graphPath, factory);
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

	// Warn if a boundary name is used by both an input and an output — then --<name> is ambiguous.
	static void warnBoundaryCollisions(flow::Graph& graph)
	{
		std::set<std::string> inputs;
		for (const flow::BoundaryInput& in : graph.boundaryInputs())
			inputs.insert(in.name);
		for (const flow::BoundaryOutput& out : graph.boundaryOutputs())
		{
			if (inputs.count(out.name) != 0)
				log::warn("flowview: boundary '{}' is both an input and an output — --{} is ambiguous", out.name, out.name);
		}
	}

	// Bind one boundary input from a cli value via the type's registered binder (scalars + image). A
	// failed parse and an unbindable type are distinct diagnostics.
	static void bindBoundaryInput(flow::Evaluation& evaluation, const flow::BoundaryInput& in,
								  const std::string& value, const BoundaryBinders& binders)
	{
		if (auto bound = binders.bind(in.type, value))
			evaluation.bind(in, std::move(*bound));
		else if (binders.has(in.type))
			log::error("flowview: could not parse '{}' for --{} as {}", value, in.name, in.typeName);
		else
			log::error("flowview: --{} has type {}, which is not cli-bindable", in.name, in.typeName);
	}

	// --- subcommands ----------------------------------------------------------

	int runGraph(const std::string& graphPath, const std::string& savePath,
				 const std::vector<std::string>& bindings,
				 const core::Factory<flow::Node>& factory, const BoundaryBinders& binders,
				 std::uint32_t exampleSize)
	{
		flow::Graph graph;
		if (!buildOrLoad(graph, graphPath, factory))
			return 1;

		// The definition and its runtime state, created together and used together.
		flow::Evaluation evaluation{graph};

		warnBoundaryCollisions(graph);
		std::map<std::string, std::string> args = parseBindings(bindings);

		// Bind inputs from --<name>; an unbound input on the example gets a default gradient (so it
		// runs out of the box), on a loaded graph is left empty with a warning.
		for (const flow::BoundaryInput& in : graph.boundaryInputs())
		{
			const auto arg = args.find(in.name);
			if (arg != args.end())
			{
				bindBoundaryInput(evaluation, in, arg->second, binders);
				args.erase(arg);
			}
			else if (graphPath.empty())
			{
				bindDefaultInput(graph, evaluation, exampleSize); // the example's single "source" input
			}
			else
			{
				log::warn("flowview: input --{} not bound — leaving it empty", in.name);
			}
		}

		flow::SerialScheduler scheduler;
		scheduler.run(graph, evaluation);
		dumpGraph(std::cout, graph, evaluation);

		// Write bound outputs: --<outputName> <path> saves that output boundary's image.
		for (const flow::BoundaryOutput& out : graph.boundaryOutputs())
		{
			const auto arg = args.find(out.name);
			if (arg == args.end())
				continue;
			const flow::PortValue& delivered = evaluation.value(out);
			if (delivered.empty())
			{
				// Its producer was gated off / suppressed (conditional eval, ADR-0007): no value this
				// run. A legitimate outcome, not an error — report it and write nothing.
				log::info("flowview: --{} produced no output this run — nothing written", out.name);
			}
			else if (delivered.holds<image::Image>())
			{
				if (io::image::save(arg->second, delivered.get<image::Image>()))
					log::info("flowview: wrote --{} to {}", out.name, arg->second);
				else
					log::error("flowview: could not write --{} to {}", out.name, arg->second);
			}
			else
			{
				// A scalar / text output: write its value as text (symmetric with an image to a file).
				std::ofstream file(arg->second);
				if (file)
				{
					file << evaluation.describe(out.port) << '\n';
					log::info("flowview: wrote --{} to {}", out.name, arg->second);
				}
				else
				{
					log::error("flowview: could not write --{} to {}", out.name, arg->second);
				}
			}
			args.erase(arg);
		}

		for (const auto& [name, value] : args)
			log::warn("flowview: --{} matched no boundary (value '{}')", name, value);

		if (!savePath.empty())
		{
			if (saveGraph(savePath, graph, factory))
				log::info("flowview: wrote graph to {}", savePath);
			else
				log::error("flowview: could not write graph to {}", savePath);
		}
		return 0;
	}

	int listGraph(const std::string& graphPath, const core::Factory<flow::Node>& factory, const BoundaryBinders& binders)
	{
		flow::Graph graph;
		if (!buildOrLoad(graph, graphPath, factory))
			return 1;

		// The definition and its runtime state, created together and used together.
		flow::Evaluation evaluation{graph};

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
		return 0;
	}
} // namespace flowview
