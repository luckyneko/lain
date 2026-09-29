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
#include <lain/io/data/load.h>
#include <lain/io/data/save.h>
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

	const lain::core::Uri direct = flowview::templateKey(dir / "template.json");
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
		cache.invalidate(flowview::templateKey(templatePath).toString());
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

// --- document options (M14 slice 5) -------------------------------------------
//
// A document's run trigger is saved with it (the root editor blob) but is not an undo step — so it is
// written by saveGraph, stripped from a load by takeDocumentOptions, and absent from every snapshot.

// A document with one image input on its boundary, so a round-trip has something to carry besides the
// options.
static Graph optionsDocument()
{
	Graph graph;
	graph.boundaryInputNode().addBoundary<lain::image::Image>("source");
	return graph;
}

TEST_CASE("a document's run trigger is saved with it and read back", "[graphio][options]")
{
	const std::filesystem::path dir = scratchDir("options-roundtrip");
	const Factory<Node> factory = ioFactory();
	const std::filesystem::path file = dir / "doc.json";

	for (const flowview::RunTrigger trigger : {flowview::RunTrigger::OnCommit, flowview::RunTrigger::Manual})
	{
		flowview::DocumentOptions options;
		options.trigger = trigger;
		REQUIRE(flowview::saveGraph(file.string(), optionsDocument(), factory, {}, options));

		LoadResult loaded = flowview::loadGraph(file.string(), factory, nullptr);
		REQUIRE(loaded.clean());
		REQUIRE(flowview::takeDocumentOptions(loaded) == options);
		REQUIRE(loaded.graph.boundaryInputNode().outputCount() == 1); // and the document is all there
	}
}

TEST_CASE("a document at the default options is written without them", "[graphio][options]")
{
	// Absent means Live, so a Live document writes no blob at all — which is what keeps every file
	// saved before options existed byte-identical when it is saved again.
	const std::filesystem::path dir = scratchDir("options-default");
	const Factory<Node> factory = ioFactory();
	const std::filesystem::path file = dir / "doc.json";

	// And the options OWN the root blob: one left in the layout does not leak through.
	EditorTree layout;
	lain::data::Value stale = lain::data::Value::object();
	stale.set("trigger", lain::data::Value(std::string("Manual")));
	layout.graph = stale;

	REQUIRE(flowview::saveGraph(file.string(), optionsDocument(), factory, layout, flowview::DocumentOptions{}));
	const auto written = lain::io::data::load(lain::core::Uri::fromPath(file));
	REQUIRE(written.has_value());
	REQUIRE(written->find("graphEditor") == nullptr);

	LoadResult loaded = flowview::loadGraph(file.string(), factory, nullptr);
	REQUIRE(flowview::takeDocumentOptions(loaded) == flowview::DocumentOptions{});
}

TEST_CASE("options this build does not understand fall back to Live and the document still loads", "[graphio][options]")
{
	// The graph must never be the price of a blob this build cannot read.
	const std::filesystem::path dir = scratchDir("options-unknown");
	const Factory<Node> factory = ioFactory();
	const std::filesystem::path file = dir / "doc.json";

	const auto loadWith = [&](lain::data::Value blob)
	{
		lain::data::Value document = flowview::snapshotGraph(optionsDocument(), factory);
		document.set("graphEditor", std::move(blob));
		REQUIRE(lain::io::data::save(lain::core::Uri::fromPath(file), document));
		LoadResult loaded = flowview::loadGraph(file.string(), factory, nullptr);
		REQUIRE(loaded.clean()); // flow round-trips the blob opaquely — it is not flow's to judge
		return loaded;
	};

	SECTION("a trigger a newer flowview wrote")
	{
		// lain::data reads a struct member best-effort, so this falls back to Live on its own.
		lain::data::Value blob = lain::data::Value::object();
		blob.set("trigger", lain::data::Value(std::string("WhenIdle")));
		LoadResult loaded = loadWith(std::move(blob));
		REQUIRE(flowview::takeDocumentOptions(loaded) == flowview::DocumentOptions{});
		REQUIRE(loaded.graph.boundaryInputNode().outputCount() == 1);
	}

	SECTION("a blob that is not options at all")
	{
		LoadResult loaded = loadWith(lain::data::Value(std::string("manual please")));
		REQUIRE(flowview::takeDocumentOptions(loaded) == flowview::DocumentOptions{});
		REQUIRE(loaded.graph.boundaryInputNode().outputCount() == 1);
		// ... and the user is told, since a Manual document quietly running Live is what the option
		// exists to prevent.
		REQUIRE(loaded.issues.size() == 1);
		REQUIRE(loaded.issues.front().severity == Severity::Warning);
	}
}

TEST_CASE("a loaded document's snapshots never carry its options", "[graphio][options]")
{
	// The undo half of "saved, never undone". The layout a load leaves behind is the undo baseline and
	// every snapshot after it — so if the options stayed in it, the whole history would carry a frozen
	// copy of the trigger, one line away from being reapplied by a restore.
	const std::filesystem::path dir = scratchDir("options-snapshot");
	const Factory<Node> factory = ioFactory();
	const std::filesystem::path file = dir / "doc.json";

	flowview::DocumentOptions manual;
	manual.trigger = flowview::RunTrigger::Manual;
	REQUIRE(flowview::saveGraph(file.string(), optionsDocument(), factory, {}, manual));

	LoadResult loaded = flowview::loadGraph(file.string(), factory, nullptr);
	REQUIRE(flowview::takeDocumentOptions(loaded) == manual);
	REQUIRE(loaded.editor.graph.isNull());

	const lain::data::Value baseline = flowview::snapshotGraph(loaded.graph, factory, loaded.editor);
	REQUIRE(baseline.find("graphEditor") == nullptr);
	// ... and so an undo restore of that snapshot hands back no options to apply.
	LoadResult restored = flowview::restoreGraph(baseline, factory, dir, nullptr);
	REQUIRE(restored.editor.graph.isNull());
}
