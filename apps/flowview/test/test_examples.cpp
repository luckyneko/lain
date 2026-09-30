// The committed example documents (apps/flowview/examples), held to the catalog that describes them.
// These are the first graph documents in the tree written by an EARLIER build rather than by the test
// reading them, so they are also the first check that an existing document still loads.
//
// Two claims. Every example loads, runs and raises exactly what its entry says: nothing, or its one
// expected issue. And every built example on disk is byte for byte what the builder writes today, so
// a builder change that is not regenerated, or a hand edit, fails here rather than drifting.

#include "examples.h"
#include "graphio.h"
#include "scene.h"		// registerExampleNodes, bindDefaultInput: the production palette and stand-in
#include "validation.h" // collectIssues: the Issues pane's rows

#include <lain/camera/backends.h>
#include <lain/camera/flow/register.h>
#include <lain/core/factory.h>
#include <lain/core/uri.h>
#include <lain/flow/boundary.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/graph.h>
#include <lain/flow/portvalue.h>
#include <lain/flow/scheduler.h>
#include <lain/io/image/codecs.h>
#include <lain/io/sequence/openers.h>
#include <lain/io/video/codecs.h>
#include <lain/io/video/open.h>
#include <lain/media/frameposition.h>
#include <lain/testing/scratch.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <typeindex>
#include <vector>

using flowview::examples::catalog;
using flowview::examples::Example;

static const std::filesystem::path kExamples{LAIN_FLOWVIEW_EXAMPLES_DIR};

static lain::core::Factory<lain::flow::Node> exampleFactory()
{
	lain::io::image::registerImageCodecs();
	lain::io::video::registerVideoCodecs();
	lain::io::sequence::registerSequenceOpeners();
	lain::camera::registerCameraBackends(); // before the palette: camera kinds follow the backends
	flowview::registerSceneSerialization();
	lain::core::Factory<lain::flow::Node> factory;
	flowview::registerExampleNodes(factory, 64);
	return factory;
}

// Asked of the production seam, not of a CMake flag: what matters is whether a backend registered.
static bool haveVideoReader()
{
	return !lain::io::video::readerRegistry().keys().empty();
}

static bool haveCameraBackend()
{
	return !lain::camera::availableCameraNodeKeys().empty();
}

static std::string bytesOf(const std::filesystem::path& file)
{
	std::ifstream in(file, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static std::string joined(const std::vector<std::string>& lines)
{
	std::string text;
	for (const std::string& line : lines)
		text += "\n  " + line;
	return text.empty() ? " (none)" : text;
}

TEST_CASE("every example loads and runs as the catalog says", "[examples]")
{
	const lain::core::Factory<lain::flow::Node> factory = exampleFactory();
	for (const Example& example : catalog())
	{
		DYNAMIC_SECTION(example.name)
		{
			const std::filesystem::path file = kExamples / (example.name + ".json");
			REQUIRE(std::filesystem::exists(file));
			lain::flow::serialize::LoadResult loaded = flowview::loadGraph(lain::core::Uri::fromPath(file), factory, nullptr);

			std::vector<std::string> raised;
			for (const lain::flow::serialize::LoadIssue& issue : loaded.issues)
				raised.push_back(issue.message);

			if (example.needsCamera && !haveCameraBackend())
			{
				// Without a backend the camera kinds are not registered (ADR-0016), so the document
				// does not load as saved, and each kind it names is reported rather than run around.
				INFO("raised:" << joined(raised));
				const std::string text = joined(raised);
				CHECK(text.find("unknown node kind \"boardSpecification\"") != std::string::npos);
				CHECK(text.find("unknown node kind \"renderBoard\"") != std::string::npos);
			}
			else if (example.needsVideo && !haveVideoReader())
			{
				// Without a codec the document still OPENS: video is a capability, not vocabulary.
				INFO("raised:" << joined(raised));
				CHECK(raised.empty());
			}
			else
			{
				// A root input is bound the way a user would find it bound: the gradient stand-in
				// for an image, the first frame for a position.
				lain::flow::Evaluation evaluation{loaded.graph};
				for (const lain::flow::BoundaryInput& in : loaded.graph.boundaryInputs())
				{
					if (in.type == std::type_index(typeid(lain::media::FramePosition)))
					{
						lain::flow::PortValue first;
						first.set(lain::media::FramePosition{0});
						evaluation.bind(in, std::move(first));
					}
					else
						flowview::bindDefaultInput(in, evaluation, 32);
				}
				lain::flow::SerialScheduler{}.run(loaded.graph, evaluation);

				// An Info row (an unused output) is a heads-up, not a problem.
				for (const flowview::Issue& issue : flowview::collectIssues(loaded.graph, evaluation, {}))
				{
					if (issue.severity != flowview::Issue::Severity::Info)
						raised.push_back(issue.message);
				}
				INFO("raised:" << joined(raised));

				if (example.expectedIssue.empty())
				{
					CHECK(raised.empty());
					for (const lain::flow::BoundaryOutput& out : loaded.graph.boundaryOutputs())
					{
						INFO("output " << out.name);
						CHECK_FALSE(evaluation.value(out).empty());
					}
				}
				else
				{
					bool found = false;
					for (const std::string& message : raised)
						found = found || message.find(example.expectedIssue) != std::string::npos;
					INFO("expected: " << example.expectedIssue);
					CHECK(found);
				}
			}
		}
	}
}

TEST_CASE("the committed examples are exactly what the builder writes", "[examples]")
{
	// Regenerate into scratch and compare bytes. A failure here means a builder changed without the
	// examples being rewritten (build the `examples` target and commit), or a committed file was
	// edited by hand.
	const lain::core::Factory<lain::flow::Node> factory = exampleFactory();
	const std::filesystem::path scratch = lain::testing::scratchDir() / "examples";
	std::filesystem::create_directories(scratch);
	flowview::examples::writeDocuments(scratch, factory);

	for (const Example& example : catalog())
	{
		if (!example.build)
			continue;
		INFO(example.name << ".json is stale: build the `examples` target and commit what it writes");
		const bool same = bytesOf(scratch / (example.name + ".json")) == bytesOf(kExamples / (example.name + ".json"));
		CHECK(same); // a bool, so a failure names the file rather than printing two whole documents
	}
}
