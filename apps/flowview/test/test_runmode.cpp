// `run` and `list` refuse a document that did not load as saved, through the production runGraph /
// listGraph.
//
// An Error-severity load issue is a structural loss: an unreadable file, or a node dropped together
// with every edge touching it because this build has no such kind. The refusal matters most once a
// node kind can be ABSENT from a build on purpose — camera nodes register only when a backend can run
// them (ADR-0016) — since a document saved by one build then loses nodes in another. Running what is
// left would run a different graph and report success, and a --save would write that smaller graph
// back over the path it names.

#include "clibinders.h"
#include "graphio.h"
#include "runmode.h"
#include "scene.h"

#include <lain/io/image/codecs.h>
#include <lain/io/sequence/openers.h>
#include <lain/io/video/codecs.h>
#include <lain/testing/scratch.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace lain;
namespace fs = std::filesystem;

namespace
{
	void ensureRegistered()
	{
		static const bool once = []
		{
			io::image::registerImageCodecs();
			io::video::registerVideoCodecs();
			io::sequence::registerSequenceOpeners();
			flowview::registerSceneSerialization();
			return true;
		}();
		(void)once;
	}

	core::Factory<flow::Node> exampleFactory()
	{
		ensureRegistered();
		core::Factory<flow::Node> factory;
		flowview::registerExampleNodes(factory, 8);
		return factory;
	}

	fs::path scratchDir(const std::string& name)
	{
		const fs::path dir = lain::testing::scratchDir() / ("runmode-" + name);
		fs::remove_all(dir);
		fs::create_directories(dir);
		return dir;
	}

	std::string readText(const fs::path& file)
	{
		std::ifstream in(file, std::ios::binary);
		return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
	}

	void writeText(const fs::path& file, const std::string& text)
	{
		std::ofstream out(file, std::ios::binary);
		out << text;
	}

	// The built-in example scene (source -> tint -> blur -> result), saved through the production
	// `run --save` path so the document is exactly what a user's build writes.
	fs::path saveExample(const fs::path& dir, const core::Factory<flow::Node>& factory,
						 const flowview::BoundaryBinders& binders)
	{
		const fs::path file = dir / "example.json";
		flowview::RunOptions options;
		options.savePath = file.string();
		REQUIRE(flowview::runGraph(options, factory, binders) == 0);
		REQUIRE(fs::exists(file));
		return file;
	}

	// A copy of `from` with one exact substring replaced, REQUIRE-ing that it was there, so a change
	// in how the document is written fails here rather than silently testing an untouched file.
	fs::path editedCopy(const fs::path& from, const fs::path& to, const std::string& find, const std::string& replace)
	{
		std::string text = readText(from);
		const auto at = text.find(find);
		REQUIRE(at != std::string::npos);
		text.replace(at, find.size(), replace);
		writeText(to, text);
		return to;
	}

	flowview::RunOptions runOf(const fs::path& graph)
	{
		flowview::RunOptions options;
		options.graphPath = graph.string();
		return options;
	}
} // namespace

TEST_CASE("run and list refuse a document that dropped a node on load", "[runmode]")
{
	const core::Factory<flow::Node> factory = exampleFactory();
	flowview::BoundaryBinders binders;
	flowview::registerBoundaryBinders(binders);

	const fs::path dir = scratchDir("dropped");
	const fs::path saved = saveExample(dir, factory, binders);

	// The control: the document as saved runs and lists. Without it, a refusal below could be caused
	// by anything about the document, not by the dropped node.
	REQUIRE(flowview::runGraph(runOf(saved), factory, binders) == 0);
	REQUIRE(flowview::listGraph(saved.string(), factory, binders) == 0);

	// The same document naming a kind this build does not register — a document written by a build
	// that had it. The loader drops the node and both of its edges and reports an Error.
	const fs::path lossy = editedCopy(saved, dir / "lossy.json", "\"kind\": \"blur\"", "\"kind\": \"notInThisBuild\"");

	CHECK(flowview::runGraph(runOf(lossy), factory, binders) != 0);
	CHECK(flowview::listGraph(lossy.string(), factory, binders) != 0);

	// The sharpest consequence of carrying on: --save would write the smaller graph. A refused run
	// writes nothing at all.
	flowview::RunOptions withSave = runOf(lossy);
	withSave.savePath = (dir / "resaved.json").string();
	CHECK(flowview::runGraph(withSave, factory, binders) != 0);
	CHECK_FALSE(fs::exists(dir / "resaved.json"));
}

TEST_CASE("run and list refuse a graph file that cannot be read", "[runmode]")
{
	// Before this refusal, the check was "nothing loaded", which a Graph can never be (it is born
	// with its boundary pair) — so a missing file ran an empty graph and exited 0.
	const core::Factory<flow::Node> factory = exampleFactory();
	flowview::BoundaryBinders binders;
	flowview::registerBoundaryBinders(binders);

	const fs::path missing = scratchDir("missing") / "not-here.json";
	REQUIRE_FALSE(fs::exists(missing));

	CHECK(flowview::runGraph(runOf(missing), factory, binders) != 0);
	CHECK(flowview::listGraph(missing.string(), factory, binders) != 0);
}

TEST_CASE("a load that only WARNED still runs", "[runmode]")
{
	// The refusal is on Error, not on "any issue": a Warning is one item skipped with the rest intact.
	// A refusal on every issue would make a document with one stale param unrunnable.
	const core::Factory<flow::Node> factory = exampleFactory();
	flowview::BoundaryBinders binders;
	flowview::registerBoundaryBinders(binders);

	const fs::path dir = scratchDir("warned");
	const fs::path saved = saveExample(dir, factory, binders);
	const fs::path stale = editedCopy(saved, dir / "stale.json", "\"name\": \"radius\"", "\"name\": \"noLongerAParam\"");

	CHECK(flowview::runGraph(runOf(stale), factory, binders) == 0);
	CHECK(flowview::listGraph(stale.string(), factory, binders) == 0);
}
