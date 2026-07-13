#include "runmode.h"

#include "dump.h"
#include "graphio.h"
#include "scene.h"

#include <lain/flow/boundary.h>
#include <lain/flow/graph.h>
#include <lain/flow/portvalue.h>
#include <lain/flow/scheduler.h>
#include <lain/image/image.h>
#include <lain/io/image/load.h>
#include <lain/io/image/save.h>
#include <lain/log/log.h>

#include <iostream>
#include <map>
#include <set>
#include <typeinfo>
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
			inputs.insert(in.name());
		for (const flow::BoundaryOutput& out : graph.boundaryOutputs())
		{
			if (inputs.count(out.name()) != 0)
				log::warn("flowview: boundary '{}' is both an input and an output — --{} is ambiguous", out.name(), out.name());
		}
	}

	// Bind one boundary input from a cli value. Image boundaries load the path; other types aren't
	// cli-bindable yet (the app-side type switch grows a loader when scalar/video boundaries land).
	static void bindBoundaryInput(const flow::BoundaryInput& in, const std::string& value)
	{
		if (in.type() == typeid(image::Image))
		{
			if (auto image = io::image::load(value))
			{
				flow::PortValue slot;
				slot.set<image::Image>(std::move(*image));
				in.setValue(std::move(slot));
			}
			else
			{
				log::error("flowview: could not load image for --{}: {}", in.name(), value);
			}
		}
		else
		{
			log::error("flowview: --{} has a non-image type; only image inputs bind from the cli for now", in.name());
		}
	}

	// --- subcommands ----------------------------------------------------------

	int runGraph(const std::string& graphPath, const std::string& savePath,
				 const std::vector<std::string>& bindings,
				 const core::Factory<flow::Node>& factory, std::uint32_t exampleSize)
	{
		flow::Graph graph;
		if (!buildOrLoad(graph, graphPath, factory))
			return 1;

		warnBoundaryCollisions(graph);
		std::map<std::string, std::string> args = parseBindings(bindings);

		// Bind inputs from --<name>; an unbound input on the example gets a default gradient (so it
		// runs out of the box), on a loaded graph is left empty with a warning.
		for (const flow::BoundaryInput& in : graph.boundaryInputs())
		{
			const auto arg = args.find(in.name());
			if (arg != args.end())
			{
				bindBoundaryInput(in, arg->second);
				args.erase(arg);
			}
			else if (graphPath.empty())
			{
				bindDefaultInput(graph, exampleSize); // the example's single "source" input
			}
			else
			{
				log::warn("flowview: input --{} not bound — leaving it empty", in.name());
			}
		}

		flow::SerialScheduler scheduler;
		scheduler.run(graph);
		dumpGraph(std::cout, graph);

		// Write bound outputs: --<outputName> <path> saves that output boundary's image.
		for (const flow::BoundaryOutput& out : graph.boundaryOutputs())
		{
			const auto arg = args.find(out.name());
			if (arg == args.end())
				continue;
			if (out.value().empty())
			{
				// Its producer was gated off / suppressed (conditional eval, ADR-0007): no value this
				// run. A legitimate outcome, not an error — report it and write nothing.
				log::info("flowview: --{} produced no output this run — nothing written", out.name());
			}
			else if (out.value().holds<image::Image>())
			{
				if (io::image::save(arg->second, out.value().get<image::Image>()))
					log::info("flowview: wrote --{} to {}", out.name(), arg->second);
				else
					log::error("flowview: could not write --{} to {}", out.name(), arg->second);
			}
			else
			{
				log::error("flowview: output --{} is not an image", out.name());
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

	int listGraph(const std::string& graphPath, const core::Factory<flow::Node>& factory)
	{
		flow::Graph graph;
		if (!buildOrLoad(graph, graphPath, factory))
			return 1;

		warnBoundaryCollisions(graph);
		std::cout << "inputs:\n";
		for (const flow::BoundaryInput& in : graph.boundaryInputs())
			std::cout << "  --" << in.name() << " : " << in.typeName() << "\n";
		std::cout << "outputs:\n";
		for (const flow::BoundaryOutput& out : graph.boundaryOutputs())
			std::cout << "  --" << out.name() << " : " << out.typeName() << "\n";
		return 0;
	}
} // namespace flowview
