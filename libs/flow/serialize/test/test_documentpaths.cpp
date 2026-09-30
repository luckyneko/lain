// Document-relative paths (ADR-0027): relativizePaths / resolvePaths, the transforms a host applies to
// a document at the file boundary. Driven on documents toValue wrote, so the params they rewrite are
// the ones the serializer really produces; no file is read or written.

#include "lain/flow/serialize/documentpaths.h"
#include "lain/flow/serialize/serialize.h"

#include <lain/core/factory.h>
#include <lain/data/value.h>
#include <lain/flow/boundary.h>
#include <lain/flow/graph.h>
#include <lain/flow/group.h>
#include <lain/flow/node.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <filesystem>
#include <string>

using lain::core::Factory;
using lain::data::Value;
using namespace lain::flow;
using namespace lain::flow::serialize;

// One path param, and one STRING param holding path-shaped text, which must never be rewritten: the
// transforms find a file reference by its stored type key, and a string is not one.
class PathNode : public Node
{
public:
	PathNode()
		: Node("Path")
	{
		m_path = addParam<std::filesystem::path>("path", {});
		m_text = addParam<std::string>("text", {});
	}
	std::unique_ptr<Node> clone() const override { return std::make_unique<PathNode>(*this); }
	void compute(NodeEvaluation&) const override {}

	const std::filesystem::path& path() const { return param(m_path).get<std::filesystem::path>(); }
	void setPath(const std::filesystem::path& path) { REQUIRE(setParam(m_path, path)); }
	void setText(const std::string& text) { REQUIRE(setParam(m_text, text)); }

private:
	PortId m_path;
	PortId m_text;
};

static Factory<Node> pathFactory()
{
	Factory<Node> factory;
	factory.registerType<GroupInputNode>("groupInput");
	factory.registerType<GroupOutputNode>("groupOutput");
	factory.registerType<InlineGroupNode>("group");
	factory.registerType<PathNode>("pathNode");
	return factory;
}

static ValueCodecs pathCodecs()
{
	ValueCodecs codecs;
	codecs.registerType<std::filesystem::path>(kPathTypeKey);
	codecs.registerType<std::string>("string");
	return codecs;
}

// An absolute folder, spelled the same way on every platform's terms. Nothing is created in it.
static std::filesystem::path root()
{
	return std::filesystem::absolute("docpaths-root").lexically_normal();
}

// A document holding one PathNode whose path is `path`, inside an inline group when `nested`.
static Value documentHolding(const std::filesystem::path& path, bool nested = false)
{
	Graph graph;
	Graph* level = &graph;
	if (nested)
		level = &static_cast<InlineGroupNode&>(graph.node(graph.add<InlineGroupNode>())).inner();
	static_cast<PathNode&>(level->node(level->add<PathNode>())).setPath(path);
	return toValue(graph, pathFactory(), pathCodecs());
}

// The text stored for the first param named `name`, searching nested bodies too.
static std::string storedText(const Value& body, const std::string& name = "path")
{
	const Value* nodes = body.find("nodes");
	if (!nodes || !nodes->asArray())
		return "<none>";
	for (const Value& node : *nodes->asArray())
	{
		if (const Value* params = node.find("params"); params && params->asArray())
		{
			for (const Value& param : *params->asArray())
			{
				const Value* paramName = param.find("name");
				const Value* value = param.find("value");
				if (paramName && *paramName->asString() == name && value && value->asString())
					return *value->asString();
			}
		}
		if (const Value* inner = node.find("graph"))
		{
			if (std::string found = storedText(*inner, name); found != "<none>")
				return found;
		}
	}
	return "<none>";
}

TEST_CASE("a path inside the folder becomes relative, and resolves back to itself", "[serialize][documentpaths]")
{
	Value document = documentHolding(root() / "data" / "a.png");
	relativizePaths(document, root());
	REQUIRE(storedText(document) == "data/a.png");

	// Read back in a folder moved whole: the file beside the document, which is the reason for the rule.
	const std::filesystem::path moved = std::filesystem::absolute("docpaths-moved").lexically_normal();
	Value copy = document;
	resolvePaths(copy, moved);
	REQUIRE(storedText(copy) == (moved / "data" / "a.png").generic_string());

	// ... and through the loader, the node holds that absolute path.
	resolvePaths(document, root());
	LoadResult loaded = fromValue(document, pathFactory(), pathCodecs());
	REQUIRE(loaded.clean());
	for (const NodeId id : loaded.graph.nodeIds())
	{
		if (const auto* node = dynamic_cast<const PathNode*>(&loaded.graph.node(id)))
			REQUIRE(node->path() == root() / "data" / "a.png");
	}
}

TEST_CASE("a path outside the folder stays absolute, never ../", "[serialize][documentpaths]")
{
	// Footage kept elsewhere. A `../` chain would break the moment the document moved on its own.
	const std::filesystem::path sibling = root().parent_path() / "footage" / "clip.png";
	Value document = documentHolding(sibling);
	relativizePaths(document, root());
	REQUIRE(storedText(document) == sibling.generic_string());
	resolvePaths(document, root());
	REQUIRE(storedText(document) == sibling.generic_string());
}

TEST_CASE("a relative in-memory path keeps meaning the working directory", "[serialize][documentpaths]")
{
	// The OS reads a relative path against the working directory. Written as it is, a load would read
	// it against the DOCUMENT instead: a different file, silently.
	SECTION("inside the folder, it becomes relative to the folder")
	{
		Value document = documentHolding("docpaths-root/data/a.png");
		relativizePaths(document, root());
		REQUIRE(storedText(document) == "data/a.png");
	}
	SECTION("outside it, it becomes absolute")
	{
		Value document = documentHolding("elsewhere.png");
		relativizePaths(document, root());
		REQUIRE(storedText(document) == std::filesystem::absolute("elsewhere.png").generic_string());
	}
}

TEST_CASE("an empty path and a uri with a scheme are left as written", "[serialize][documentpaths]")
{
	// An empty path is "no file chosen yet"; rebasing it would name the document's own folder.
	const std::string text = GENERATE(std::string{}, std::string{"s3://bucket/a.png"});
	Value document = documentHolding(text);
	relativizePaths(document, root());
	REQUIRE(storedText(document) == text);
	resolvePaths(document, root());
	REQUIRE(storedText(document) == text);
}

TEST_CASE("a nested body is rewritten, and a string param never is", "[serialize][documentpaths]")
{
	Value nested = documentHolding(root() / "a.png", true);
	relativizePaths(nested, root());
	REQUIRE(storedText(nested) == "a.png");
	resolvePaths(nested, root());
	REQUIRE(storedText(nested) == (root() / "a.png").generic_string());

	// Path-shaped text in a string param is not a file reference.
	Graph graph;
	static_cast<PathNode&>(graph.node(graph.add<PathNode>())).setText((root() / "a.png").generic_string());
	Value document = toValue(graph, pathFactory(), pathCodecs());
	relativizePaths(document, root());
	REQUIRE(storedText(document, "text") == (root() / "a.png").generic_string());
}

TEST_CASE("relativize then resolve is byte-stable", "[serialize][documentpaths]")
{
	// A document read and written again in the same folder writes exactly what it read, so a save
	// that changed nothing shows no diff.
	Value document = documentHolding(root() / "data" / "a.png");
	relativizePaths(document, root());
	const Value onDisk = document;
	resolvePaths(document, root());
	relativizePaths(document, root());
	REQUIRE(document == onDisk);
}
