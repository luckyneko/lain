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
#include <lain/testing/scratch.h>

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
		const std::filesystem::path dir = lain::testing::scratchDir() / "graphio" / name;
		std::error_code ec;
		std::filesystem::remove_all(dir, ec);
		std::filesystem::create_directories(dir, ec);
		return dir;
	}

	// A template document with `inputs` input pins and one output pin, written to `path`.
	void writeTemplate(const Factory<Node>& factory, const std::filesystem::path& path, int inputs = 1)
	{
		Graph templateGraph;
		for (int i = 0; i < inputs; ++i)
			templateGraph.boundaryInputNode().addBoundary<lain::image::Image>("source" + std::to_string(i));
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
		LoadResult loaded = flowview::restoreGraph(document, factory, dir, nullptr);
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
	LoadResult restored = flowview::restoreGraph(flowview::snapshotGraph(parent, factory), factory, dir, nullptr);
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

	LoadResult restored = flowview::restoreGraph(flowview::snapshotGraph(parent, factory), factory, {}, nullptr);
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

	// No cache: with one, the deleted template would still resolve from the entry the first load
	// stored, which is the documented cost of having no file watching.
	LoadResult restored = flowview::restoreGraph(snapshot, factory, dir, nullptr);
	REQUIRE_FALSE(restored.clean()); // reported...
	const LinkedGroupNode* link = onlyLink(restored.graph);
	REQUIRE(link != nullptr);
	REQUIRE_FALSE(link->resolved());
	REQUIRE(link->inputCount() == 1); // ... but the face is rebuilt from the cache, so edges still land
	REQUIRE(link->outputCount() == 1);
}

TEST_CASE("an undo restore keeps the template definition it already had", "[graphio]")
{
	// Why the host's cache survives undo/redo while it is cleared on New/Open: a restore rebuilds the
	// document, so without the cache the template would be rebuilt too and its inner nodes would come
	// back with... the same ids (a load preserves identity), but as a DIFFERENT definition — and every
	// instance would drift apart again. Keeping it is what makes an undo a no-op for a template.
	const std::filesystem::path dir = scratchDir("undo-cache");
	const Factory<Node> factory = ioFactory();
	writeTemplate(factory, dir / "template.json");

	lain::flow::serialize::TemplateCache cache;
	Graph parent = parentLinking("template.json", factory, dir);
	const lain::data::Value snapshot = flowview::snapshotGraph(parent, factory);

	LoadResult first = flowview::restoreGraph(snapshot, factory, dir, &cache);
	REQUIRE(first.clean());
	REQUIRE(cache.size() == 1);
	const std::shared_ptr<const Graph> definition = onlyLink(first.graph)->definition();

	LoadResult second = flowview::restoreGraph(snapshot, factory, dir, &cache); // undo, then redo
	REQUIRE(second.clean());
	REQUIRE(onlyLink(second.graph)->definition() == definition);
}

// --- the template cache's keys and its invalidation (M7 slice 3) -------------

TEST_CASE("one file has one template key, however it is spelled", "[graphio]")
{
	// The cache is keyed on this, and so is the cycle guard. A key computed two ways is a key that
	// eventually disagrees with itself — and the failure is SILENT: an invalidation that misses just
	// keeps serving the definition it was asked to drop.
	const std::filesystem::path dir = scratchDir("keys");
	const Factory<Node> factory = ioFactory();
	writeTemplate(factory, dir / "template.json");

	const std::string direct = flowview::templateKey(dir / "template.json");
	REQUIRE(flowview::templateKey(dir / "sub" / ".." / "template.json") == direct);
	REQUIRE(flowview::templateKey(dir / "./template.json") == direct);

	// ... and it is the key the RESOLVER reports, which is what makes save-invalidation land on the
	// entry a load created.
	const auto resolved = flowview::templateResolver(dir)("./template.json");
	REQUIRE(resolved.has_value());
	REQUIRE(resolved->key == direct);
}

TEST_CASE("invalidating a template's key is what makes an edit to it visible", "[graphio]")
{
	// The mechanism behind BOTH of slice 3's triggers: Reload Linked Groups (clear the cache) and
	// saving a document (drop that path's entry, so Edit Template... -> Save -> Return shows the edit).
	// Without the drop the cache keeps serving the pre-edit definition — which is the behaviour being
	// pinned here, not merely the fix.
	const std::filesystem::path dir = scratchDir("invalidate");
	const Factory<Node> factory = ioFactory();
	const std::filesystem::path templatePath = dir / "template.json";
	writeTemplate(factory, templatePath, 1);

	lain::flow::serialize::TemplateCache cache;
	Graph parent = parentLinking("template.json", factory, dir);
	const lain::data::Value document = flowview::snapshotGraph(parent, factory);
	REQUIRE(flowview::restoreGraph(document, factory, dir, &cache).clean());

	// The template gains a pin, out from under the running app.
	writeTemplate(factory, templatePath, 2);

	SECTION("a stale entry keeps serving the old definition")
	{
		const LoadResult again = flowview::restoreGraph(document, factory, dir, &cache);
		REQUIRE(onlyLink(again.graph)->innerGraph()->boundaryInputNode().outputCount() == 1);
	}

	SECTION("dropping that one key picks the edit up")
	{
		cache.invalidate(flowview::templateKey(templatePath));
		const LoadResult again = flowview::restoreGraph(document, factory, dir, &cache);
		const LinkedGroupNode* link = onlyLink(again.graph);
		REQUIRE(link->innerGraph()->boundaryInputNode().outputCount() == 2);
		REQUIRE(link->inputCount() == 2); // ... and the group's own face followed it
	}

	SECTION("clearing the whole cache does too — the reload gesture")
	{
		cache.clear();
		const LoadResult again = flowview::restoreGraph(document, factory, dir, &cache);
		REQUIRE(onlyLink(again.graph)->innerGraph()->boundaryInputNode().outputCount() == 2);
	}
}
