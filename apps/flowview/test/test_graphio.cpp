// flowview's save/load facade. Driver-free — no window, no device — but it does touch the FILE
// SYSTEM, because that is the whole point of the case below: a linked group's template is a separate
// document, and resolving it is a file read.
//
// The bug this exists for: restoreGraph (the undo/redo path) took no TemplateResolver, so undoing in
// a document containing a linked group rebuilt that group as an UNRESOLVED placeholder — its interior
// emptied and it stopped producing output. Nothing warned, because loading unresolved is a legitimate
// mode (a headless `flowview list` wants exactly that).

#include "graphio.h"

#include <lain/core/factory.h>
#include <lain/data/value.h>
#include <lain/flow/boundary.h>
#include <lain/flow/edit.h>
#include <lain/flow/graph.h>
#include <lain/flow/group.h>
#include <lain/flow/node.h>
#include <lain/image/image.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

using lain::core::Factory;
using namespace lain::flow;
using namespace lain::flow::serialize;

namespace
{
	// Only what the documents below name — the boundary pair and a linked group. No example nodes,
	// so this test needs neither a device nor an image codec.
	Factory<Node> ioFactory()
	{
		flowview::registerSceneSerialization(); // the Image port type + the json codec
		Factory<Node> factory;
		factory.registerType<GroupInputNode>("groupInput");
		factory.registerType<GroupOutputNode>("groupOutput");
		factory.registerType<LinkedGroupNode>("linkedGroup");
		return factory;
	}

	// A scratch directory of this test's own, removed and recreated per case so a rerun is clean.
	std::filesystem::path scratchDir(const char* name)
	{
		const std::filesystem::path dir = std::filesystem::temp_directory_path() / "flowview-graphio-test" / name;
		std::error_code ec;
		std::filesystem::remove_all(dir, ec);
		std::filesystem::create_directories(dir, ec);
		return dir;
	}

	// A template document with one input pin and one output pin, written to `dir/name`.
	void writeTemplate(const Factory<Node>& factory, const std::filesystem::path& path)
	{
		Graph templateGraph;
		templateGraph.boundaryInputNode().addBoundary<lain::image::Image>("source");
		templateGraph.boundaryOutputNode().addBoundary<lain::image::Image>("result");
		REQUIRE(flowview::saveGraph(path.string(), templateGraph, factory));
	}

	// A parent graph holding one linked group whose `source` is `source`, with the group's ports
	// already mirrored from the resolved template.
	Graph parentLinking(const std::string& source, const Factory<Node>& factory, const std::filesystem::path& dir)
	{
		Graph parent;
		const NodeId id = parent.add<LinkedGroupNode>();
		static_cast<LinkedGroupNode&>(parent.node(id)).setSource(source);

		// Resolve it once through the normal load path, so the fixture starts from the same state the
		// app is in when the user hits undo: a RESOLVED linked group.
		const lain::data::Value document = flowview::snapshotGraph(parent, factory);
		LoadResult loaded = flowview::restoreGraph(document, factory, dir);
		REQUIRE(loaded.clean());
		return std::move(loaded.graph);
	}

	const LinkedGroupNode* onlyLink(const Graph& graph)
	{
		for (const NodeId id : graph.nodeIds())
		{
			if (const auto* linked = dynamic_cast<const LinkedGroupNode*>(&graph.node(id)))
				return linked;
		}
		return nullptr;
	}
} // namespace

TEST_CASE("restoreGraph resolves a linked group's template", "[graphio]")
{
	// The undo/redo path. A snapshot stores a linked group the same way the file does — source plus
	// interface cache — so a restore has to follow the link, or the group comes back empty.
	const std::filesystem::path dir = scratchDir("resolve");
	const Factory<Node> factory = ioFactory();
	writeTemplate(factory, dir / "template.json");

	Graph parent = parentLinking("template.json", factory, dir);
	REQUIRE(onlyLink(parent) != nullptr);
	REQUIRE(onlyLink(parent)->resolved()); // the fixture starts resolved, as the app does

	// Snapshot and restore, exactly as Undo does.
	LoadResult restored = flowview::restoreGraph(flowview::snapshotGraph(parent, factory), factory, dir);
	REQUIRE(restored.clean());

	const LinkedGroupNode* link = onlyLink(restored.graph);
	REQUIRE(link != nullptr);
	REQUIRE(link->resolved()); // ... and comes back resolved, not as an empty placeholder
	REQUIRE(link->innerGraph()->boundaryInputNode().outputCount() == 1);
	REQUIRE(link->innerGraph()->boundaryOutputNode().inputCount() == 1);
	REQUIRE(link->inputCount() == 1); // the group's own mirrored ports survive with it
	REQUIRE(link->outputCount() == 1);
}

TEST_CASE("restoreGraph resolves a template named by an absolute path", "[graphio]")
{
	// A `source` typed or dropped in as an absolute path is unaffected by the document folder —
	// worth pinning down, since operator/ discards the left side for an absolute right side.
	const std::filesystem::path dir = scratchDir("absolute");
	const Factory<Node> factory = ioFactory();
	const std::filesystem::path templatePath = dir / "template.json";
	writeTemplate(factory, templatePath);

	Graph parent = parentLinking(templatePath.string(), factory, {});

	LoadResult restored = flowview::restoreGraph(flowview::snapshotGraph(parent, factory), factory, {});
	REQUIRE(onlyLink(restored.graph) != nullptr);
	REQUIRE(onlyLink(restored.graph)->resolved());
}

TEST_CASE("a template that cannot be found still restores as a repairable placeholder", "[graphio]")
{
	// The other half of the contract: an unresolved link is not a load failure. Its cached interface
	// rebuilds the pins, so the parent's wiring survives and re-saving is lossless — that is what
	// makes a broken link repairable rather than destructive.
	const std::filesystem::path dir = scratchDir("missing");
	const Factory<Node> factory = ioFactory();
	writeTemplate(factory, dir / "template.json");

	Graph parent = parentLinking("template.json", factory, dir);
	const lain::data::Value snapshot = flowview::snapshotGraph(parent, factory);

	std::error_code ec;
	std::filesystem::remove(dir / "template.json", ec); // the template goes away under us

	LoadResult restored = flowview::restoreGraph(snapshot, factory, dir);
	REQUIRE_FALSE(restored.clean()); // reported...
	const LinkedGroupNode* link = onlyLink(restored.graph);
	REQUIRE(link != nullptr);
	REQUIRE_FALSE(link->resolved());
	REQUIRE(link->inputCount() == 1); // ... but the face is rebuilt from the cache, so edges still land
	REQUIRE(link->outputCount() == 1);
}
