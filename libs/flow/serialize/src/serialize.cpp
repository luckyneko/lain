#include "lain/flow/serialize/serialize.h"

#include "version1.h" // the v1 -> v2 DOM migrator (private; deletable whole when v1 goes)

#include <lain/core/uuid.h>				// parsing a node's stored identity
#include <lain/flow/edit.h>				// syncGroupPorts — a loaded group re-derives its ports
#include <lain/flow/group.h>			// GroupNode / LinkedGroupNode — the recursion's subject
#include <lain/flow/porttyperegistry.h> // addPortOfType / portTypeKey (+ DynamicPortsNode)
#include <lain/log/log.h>
#include <lain/meta/enums.h>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace lain::flow::serialize
{
	// --- shared helpers -------------------------------------------------------

	// Read an integer that may have arrived as Int or UInt (JSON collapses a positive Int to UInt).
	static std::optional<std::int64_t> asInteger(const data::Value& value)
	{
		if (auto i = value.asInt64())
			return *i;
		if (auto u = value.asUInt64())
			return static_cast<std::int64_t>(*u);
		return std::nullopt;
	}

	static const Port* findPortByName(const Node& node, Port::Direction direction, const std::string& name)
	{
		const std::size_t count = (direction == Port::Direction::Input) ? node.inputCount() : node.outputCount();
		for (std::size_t i = 0; i < count; ++i)
		{
			const Port& port = (direction == Port::Direction::Input) ? node.input(i) : node.output(i);
			if (port.name() == name)
				return &port;
		}
		return nullptr;
	}

	static Param* findParamByName(Node& node, const std::string& name)
	{
		for (std::size_t i = 0; i < node.paramCount(); ++i)
		{
			if (node.param(i).name() == name)
				return &node.param(i);
		}
		return nullptr;
	}

	// --- save -----------------------------------------------------------------

	static data::Value bodyToValue(const Graph& graph, const core::Factory<Node>& factory, const ValueCodecs& codecs, const EditorTree& editor);

	// A linked group's interface cache, taken from the LIVE inner boundary at save time (so what we
	// record is what this parent actually last saw), falling back to the node's stored cache when the
	// link is unresolved — otherwise saving an unresolved link would erase the very cache that lets
	// it be repaired.
	static data::Value interfaceToValue(const LinkedGroupNode& linked)
	{
		const auto pinsOf = [](const std::vector<PinSpec>& specs)
		{
			data::Value arr = data::Value::array();
			for (const PinSpec& spec : specs)
			{
				data::Value pin = data::Value::object();
				pin.set("name", data::Value(spec.name));
				pin.set("type", data::Value(spec.typeKey));
				arr.push(std::move(pin));
			}
			return arr;
		};

		std::vector<PinSpec> inputs;
		std::vector<PinSpec> outputs;
		if (linked.resolved())
		{
			const Graph& inner = linked.inner();
			const GroupInputNode& boundaryIn = inner.boundaryInputNode();
			for (std::size_t i = 0; i < boundaryIn.outputCount(); ++i)
				inputs.push_back(PinSpec{boundaryIn.output(i).name(), portTypeKey(boundaryIn.output(i).type())});
			const GroupOutputNode& boundaryOut = inner.boundaryOutputNode();
			for (std::size_t i = 0; i < boundaryOut.inputCount(); ++i)
				outputs.push_back(PinSpec{boundaryOut.input(i).name(), portTypeKey(boundaryOut.input(i).type())});
		}
		else
		{
			inputs = linked.cachedInputs();
			outputs = linked.cachedOutputs();
		}

		data::Value out = data::Value::object();
		out.set("inputs", pinsOf(inputs));
		out.set("outputs", pinsOf(outputs));
		return out;
	}

	static data::Value nodeToValue(const Node& node, const std::string& kind, const ValueCodecs& codecs,
								   const core::Factory<Node>& factory, const EditorTree* subtree)
	{
		data::Value out = data::Value::object();
		// The node's REAL identity, canonical lowercase (ADR-0011). No renumbering on save, so a
		// node keeps its id across saves and a diff shows what actually changed. A string rather
		// than a number because 128 bits do not survive a JavaScript-based reader, and 36
		// characters read better in a diff than a 19-digit decimal would.
		out.set("id", data::Value(node.id().toString()));
		out.set("kind", data::Value(kind));
		out.set("name", data::Value(node.name())); // the node's title — user-editable, so it round-trips

		data::Value params = data::Value::array();
		for (std::size_t i = 0; i < node.paramCount(); ++i)
		{
			if (auto p = paramToValue(node.param(i), codecs))
				params.push(std::move(*p));
		}
		if (const data::Value::Array* arr = params.asArray(); arr && !arr->empty())
			out.set("params", std::move(params));

		// Dynamic pins: a DynamicPortsNode's runtime pins (addDynamicPort) on its dynamic side, each
		// { name, type } (type = its port-type key). Direction is implied by dynamicSide(), so it isn't
		// stored. Only the pins added at runtime are replayed — a STATIC pin the node declares in its
		// ctor on the dynamic side (a Select's `selector` input) is rebuilt by the ctor on load, so it
		// is skipped here (Port::isDynamic), else it would double-add.
		if (const auto* dynamic = dynamic_cast<const DynamicPortsNode*>(&node))
		{
			const Port::Direction side = dynamic->dynamicSide();
			const std::size_t count = (side == Port::Direction::Input) ? node.inputCount() : node.outputCount();
			data::Value pins = data::Value::array();
			for (std::size_t i = 0; i < count; ++i)
			{
				const Port& pin = (side == Port::Direction::Input) ? node.input(i) : node.output(i);
				if (!pin.isDynamic())
					continue; // static pin declared in the node's ctor — reconstructed on load, not replayed
				const std::string typeKey = portTypeKey(pin.type());
				if (typeKey.empty())
					continue; // unregistered port type — can't name it; skip (best-effort)
				data::Value pinV = data::Value::object();
				pinV.set("name", data::Value(pin.name()));
				pinV.set("type", data::Value(typeKey));
				pins.push(std::move(pinV));
			}
			if (const data::Value::Array* arr = pins.asArray(); arr && !arr->empty())
				out.set("dynamicPins", std::move(pins));
		}

		// A group node. Its OWN ports are never stored: they are derived from the inner boundary and
		// re-created by edit::syncGroupPorts on load, so storing them would be a second source of
		// truth. A LINKED group stores where its recipe lives plus the interface cache; an INLINE one
		// stores the recipe itself, as a nested body — the same shape as the document root.
		if (const auto* linked = dynamic_cast<const LinkedGroupNode*>(&node))
		{
			out.set("source", data::Value(linked->source()));
			out.set("interface", interfaceToValue(*linked));
		}
		else if (const auto* group = dynamic_cast<const GroupNode*>(&node))
		{
			static const EditorTree empty;
			out.set("graph", bodyToValue(group->inner(), factory, codecs, subtree ? *subtree : empty));
		}

		return out;
	}

	static data::Value bodyToValue(const Graph& graph, const core::Factory<Node>& factory, const ValueCodecs& codecs, const EditorTree& editor)
	{
		// Walked in the graph's INSERTION order, and the array carries that order — which is why no
		// separate `order` field is needed and why a pasted id sorting oddly among the others has no
		// effect on the document. `serialized` is the set that made it into the file, so an edge or
		// editor blob whose node was skipped is skipped with it.
		std::set<NodeId> serialized;

		data::Value nodes = data::Value::array();
		for (const NodeId id : graph.nodeIds())
		{
			const Node& node = graph.node(id);
			const std::string kind = factory.keyOf(node);
			if (kind.empty())
				continue; // unregistered node type — can't name it; skip (best-effort save)
			serialized.insert(id);
			const auto subtree = editor.groups.find(id);
			nodes.push(nodeToValue(node, kind, codecs, factory, subtree == editor.groups.end() ? nullptr : &subtree->second));
		}

		data::Value edges = data::Value::array();
		for (const Graph::Edge& edge : graph.edges())
		{
			if (serialized.count(edge.from.node) == 0 || serialized.count(edge.to.node) == 0)
				continue; // an endpoint node was skipped
			const Port* fromPort = graph.node(edge.from.node).findOutput(edge.from.port);
			const Port* toPort = graph.node(edge.to.node).findInput(edge.to.port);
			if (!fromPort || !toPort)
				continue;

			data::Value fromRef = data::Value::object();
			fromRef.set("node", data::Value(edge.from.node.toString()));
			fromRef.set("port", data::Value(fromPort->name()));
			data::Value toRef = data::Value::object();
			toRef.set("node", data::Value(edge.to.node.toString()));
			toRef.set("port", data::Value(toPort->name()));

			data::Value e = data::Value::object();
			e.set("from", std::move(fromRef));
			e.set("to", std::move(toRef));
			edges.push(std::move(e));
		}

		// Editor section: the adapter's per-node blobs, keyed by the node's id — the same key the
		// nodes array uses, so a load hands them straight back. Only nodes that were serialised and
		// have a non-null blob appear. Emitted in node order, so the section is diff-clean.
		data::Value editorSection = data::Value::object();
		for (const NodeId id : graph.nodeIds())
		{
			if (serialized.count(id) == 0)
				continue;
			const auto it = editor.nodes.find(id);
			if (it != editor.nodes.end() && !it->second.isNull())
				editorSection.set(id.toString(), it->second);
		}

		data::Value body = data::Value::object();
		body.set("nodes", std::move(nodes));
		body.set("edges", std::move(edges));
		if (const data::Value::Object* obj = editorSection.asObject(); obj && !obj->empty())
			body.set("editor", std::move(editorSection));
		return body;
	}

	data::Value toValue(const Graph& graph, const core::Factory<Node>& factory, const ValueCodecs& codecs, const EditorTree& editor)
	{
		// A document is a body plus the format version. `version` is set first so it leads the file
		// (the json codec preserves insertion order, which is what keeps a save diff-clean).
		data::Value document = data::Value::object();
		document.set("version", data::Value(kFormatVersion));
		data::Value body = bodyToValue(graph, factory, codecs, editor);
		if (const data::Value::Object* obj = body.asObject())
		{
			for (const auto& [key, value] : *obj)
				document.set(key, value);
		}
		return document;
	}

	// --- load -----------------------------------------------------------------

	static void readParams(Node& node, const data::Value& params, const ValueCodecs& codecs, std::vector<LoadIssue>& issues)
	{
		const data::Value::Array* arr = params.asArray();
		if (!arr)
			return;

		for (const data::Value& stored : *arr)
		{
			const data::Value* nameV = stored.find("name");
			const std::string* name = nameV ? nameV->asString() : nullptr;
			if (!name)
				continue; // a param with no name — nothing to bind it to

			Param* param = findParamByName(node, *name);
			if (!param)
			{
				const std::string msg = "unknown param \"" + *name + "\" on node \"" + node.name() + "\" — skipped";
				log::warn("flow::serialize: {}", msg);
				issues.push_back({Severity::Warning, msg});
				continue;
			}
			if (!paramFromValue(*param, stored, codecs))
			{
				const std::string msg = "param \"" + *name + "\" on node \"" + node.name() + "\" failed to read — left at default";
				log::warn("flow::serialize: {}", msg);
				issues.push_back({Severity::Warning, msg});
			}
		}
	}

	// The id AS WRITTEN in the document mapped to the id the node actually got. Almost always the
	// same string, since load preserves identity — but not when a boundary node was adopted onto the
	// graph's own pair, when a duplicate was re-minted, when the id was unreadable, or when the body
	// is a template being INSTANTIATED as a copy. Keyed by the raw text so a hand-written document
	// that names its nodes "a" / "b" still wires up.
	using IdRemap = std::map<std::string, NodeId>;

	// Resolve a `{ node, port }` reference (node id + port name) to a live PortAddress, or nullopt.
	static std::optional<PortAddress> resolveEndpoint(const data::Value& ref, Port::Direction direction,
													  Graph& graph, const IdRemap& remap)
	{
		const data::Value* nodeRef = ref.find("node");
		const data::Value* portRef = ref.find("port");
		const std::string* nodeId = nodeRef ? nodeRef->asString() : nullptr;
		const std::string* portName = portRef ? portRef->asString() : nullptr;
		if (!nodeId || !portName)
			return std::nullopt;

		const auto live = remap.find(*nodeId);
		if (live == remap.end())
			return std::nullopt; // the referenced node was skipped

		const Port* port = findPortByName(graph.node(live->second), direction, *portName);
		if (!port)
			return std::nullopt;
		return PortAddress{live->second, port->id()};
	}

	// Everything a load needs that is the same at every level of nesting. Passed by reference down
	// the recursion so issues accumulate in one list and the cycle guard is shared.
	struct LoadContext
	{
		const core::Factory<Node>& factory;
		const ValueCodecs& codecs;
		const TemplateResolver& resolver;
		std::vector<LoadIssue>& issues;
		std::set<std::string> resolving; // canonical template keys currently being loaded

		void error(std::string msg)
		{
			log::error("flow::serialize: {}", msg);
			issues.push_back({Severity::Error, std::move(msg)});
		}
		void warn(std::string msg)
		{
			log::warn("flow::serialize: {}", msg);
			issues.push_back({Severity::Warning, std::move(msg)});
		}
	};

	// What a body does with the ids it reads (ADR-0011: "load preserves identity; paste mints it").
	enum class IdPolicy
	{
		Preserve, // OPENING a document: its nodes come back as themselves, so undo, the active path
				  // and the canvas selection survive a rebuild
		Mint, // INSTANTIATING a template as a copy: the same file may back several linked groups
			  // in one document, and preserving would make that a guaranteed duplicate
	};

	static Graph loadBody(const data::Value& body, EditorTree& editor, LoadContext& ctx, IdPolicy policy);
	static Graph loadDocument(const data::Value& document, EditorTree& editor, LoadContext& ctx, IdPolicy policy);

	// The pins a linked group's template actually exposes, as PinSpecs — what the cached interface is
	// rectified against.
	static void currentInterface(const Graph& inner, std::vector<PinSpec>& inputs, std::vector<PinSpec>& outputs)
	{
		const GroupInputNode& boundaryIn = inner.boundaryInputNode();
		for (std::size_t i = 0; i < boundaryIn.outputCount(); ++i)
			inputs.push_back(PinSpec{boundaryIn.output(i).name(), portTypeKey(boundaryIn.output(i).type())});
		const GroupOutputNode& boundaryOut = inner.boundaryOutputNode();
		for (std::size_t i = 0; i < boundaryOut.inputCount(); ++i)
			outputs.push_back(PinSpec{boundaryOut.input(i).name(), portTypeKey(boundaryOut.input(i).type())});
	}

	// Read a stored { inputs:[{name,type}], outputs:[...] } interface cache.
	static void readInterface(const data::Value& stored, std::vector<PinSpec>& inputs, std::vector<PinSpec>& outputs)
	{
		const auto readPins = [](const data::Value* arrV, std::vector<PinSpec>& out)
		{
			const data::Value::Array* arr = arrV ? arrV->asArray() : nullptr;
			if (!arr)
				return;
			for (const data::Value& pin : *arr)
			{
				const data::Value* nameV = pin.find("name");
				const data::Value* typeV = pin.find("type");
				const std::string* name = nameV ? nameV->asString() : nullptr;
				const std::string* type = typeV ? typeV->asString() : nullptr;
				if (name && type)
					out.push_back(PinSpec{*name, *type});
			}
		};
		readPins(stored.find("inputs"), inputs);
		readPins(stored.find("outputs"), outputs);
	}

	// Compare what the parent last saw against what the template now offers, and REPORT the
	// differences. A pin that vanished or changed type will cost the parent an edge (edges are
	// addressed by port name), so the user is told rather than left to discover missing wiring.
	static void rectify(const std::string& source, const std::vector<PinSpec>& cached, const std::vector<PinSpec>& current,
						const char* side, LoadContext& ctx)
	{
		for (const PinSpec& was : cached)
		{
			const auto now = std::find_if(current.begin(), current.end(), [&](const PinSpec& p)
										  { return p.name == was.name; });
			if (now == current.end())
			{
				ctx.warn("template \"" + source + "\": " + side + " \"" + was.name + "\" no longer exists — connections to it are dropped");
			}
			else if (now->typeKey != was.typeKey)
			{
				ctx.warn("template \"" + source + "\": " + side + " \"" + was.name + "\" changed type (" + was.typeKey + " -> " + now->typeKey + ")");
			}
		}
	}

	// Rebuild a linked group: resolve its template, load that document into the inner graph, and
	// rectify against the cache. When the template cannot be resolved — missing file, no resolver, or
	// a recursive link — the group loads as an UNRESOLVED PLACEHOLDER whose pins come from the cache,
	// so the parent's wiring survives and saving is lossless.
	static void loadLinkedGroup(LinkedGroupNode& linked, const data::Value& nodeV, EditorTree& editor, LoadContext& ctx)
	{
		const data::Value* sourceV = nodeV.find("source");
		const std::string* source = sourceV ? sourceV->asString() : nullptr;
		linked.setSource(source ? *source : std::string{});

		std::vector<PinSpec> cachedIn;
		std::vector<PinSpec> cachedOut;
		if (const data::Value* interfaceV = nodeV.find("interface"))
			readInterface(*interfaceV, cachedIn, cachedOut);
		linked.setCachedInterface(cachedIn, cachedOut);

		std::optional<ResolvedTemplate> resolved;
		if (source && !source->empty() && ctx.resolver)
			resolved = ctx.resolver(*source);

		if (resolved && ctx.resolving.count(resolved->key) != 0)
		{
			ctx.error("recursive template \"" + *source + "\" — the link is not followed");
			resolved.reset();
		}

		if (!resolved)
		{
			// Unresolved: rebuild the interface from the cache so the parent's edges still land.
			// The pins go on the inner boundary nodes (which are DynamicPortsNodes), and
			// syncGroupPorts mirrors them outward — so a placeholder is a real, empty graph with the
			// right face, not a special case downstream.
			if (source && !source->empty())
				ctx.warn("template \"" + *source + "\" could not be resolved — the group loads unresolved from its cached interface");
			for (const PinSpec& pin : cachedIn)
			{
				if (addPortOfType(linked.inner().boundaryInputNode(), pin.typeKey, pin.name) == PortId{})
					ctx.warn("unresolved template pin \"" + pin.name + "\" (type \"" + pin.typeKey + "\") could not be rebuilt");
			}
			for (const PinSpec& pin : cachedOut)
			{
				if (addPortOfType(linked.inner().boundaryOutputNode(), pin.typeKey, pin.name) == PortId{})
					ctx.warn("unresolved template pin \"" + pin.name + "\" (type \"" + pin.typeKey + "\") could not be rebuilt");
			}
			linked.setResolved(false);
			return;
		}

		ctx.resolving.insert(resolved->key);
		// The template's OWN layout comes with it: a linked group is read-only, so showing anything
		// other than the arrangement the template author made would be a worse view of it — and there
		// is no divergence to worry about, because nothing here can edit it. It lands in this group's
		// subtree of the parent's layout, so an unresolved link still remembers where things sat.
		//
		// A template is a DOCUMENT in its own right, so it enters the version router rather than the
		// body decoder — a v1 template linked from a v2 parent must still open. And its nodes are
		// INSTANTIATED, not restored: one template may back several linked groups in one document,
		// so preserving its ids would make a duplicate certain rather than unlikely. Nothing is lost
		// — a linked group's interior is never written to the parent, only its source + interface.
		linked.inner() = loadDocument(resolved->document, editor, ctx, IdPolicy::Mint);
		ctx.resolving.erase(resolved->key);
		linked.setResolved(true);

		std::vector<PinSpec> currentIn;
		std::vector<PinSpec> currentOut;
		currentInterface(linked.inner(), currentIn, currentOut);
		rectify(linked.source(), cachedIn, currentIn, "input", ctx);
		rectify(linked.source(), cachedOut, currentOut, "output", ctx);
	}

	LoadResult fromValue(const data::Value& document, const core::Factory<Node>& factory, const ValueCodecs& codecs,
						 const TemplateResolver& resolver)
	{
		LoadResult result;
		LoadContext ctx{factory, codecs, resolver, result.issues, {}};
		result.graph = loadDocument(document, result.editor, ctx, IdPolicy::Preserve);
		return result;
	}

	// The VERSION ROUTER: every document — the root and each linked template — enters here, and
	// exactly one decoder (the current one) ever runs. An older format is handled by translating its
	// data::Value into the current shape first, so support for it is a self-contained translation
	// unit that can later be deleted whole rather than a second loader to keep working.
	Graph loadDocument(const data::Value& document, EditorTree& editor, LoadContext& ctx, IdPolicy policy)
	{
		const data::Value* versionV = document.find("version");
		const auto version = versionV ? asInteger(*versionV) : std::nullopt;

		if (!version)
		{
			// Fatal, not a guess. Assuming the current format would read a version-1 file as v2,
			// find no node whose id is a string, drop every one of them — and then a save would
			// write that empty result back over the original. A document that does not say what it
			// is cannot be safely interpreted; one line of JSON fixes a hand-written file.
			ctx.error("document has no version field — refusing to guess its format");
			return Graph{};
		}
		if (*version == kFormatVersion)
			return loadBody(document, editor, ctx, policy);
		if (*version > kFormatVersion)
		{
			// A format from the future can't be half-understood — refusing beats loading a
			// plausible-looking subset and then saving it back over the original.
			ctx.error("document version " + std::to_string(*version) + " is newer than supported " + std::to_string(kFormatVersion));
			return Graph{};
		}
		if (*version == 1)
		{
			ctx.warn("document is version 1 (numeric node ids) — migrated on load; save it to store version " + std::to_string(kFormatVersion) + " identities");
			return loadBody(detail::migrateVersion1(document), editor, ctx, policy);
		}

		ctx.error("document version " + std::to_string(*version) + " is not a format this build knows");
		return Graph{};
	}

	// One node's header, staged before the Graph exists. Identity is decided at CONSTRUCTION — a
	// NodeId is immutable once its node is admitted (ADR-0011) — so the boundary pair's saved ids
	// must be known before the graph they belong to is built. Staging also creates each node, since
	// "is this the boundary input?" is a question about the class, not about the kind string.
	struct StagedNode
	{
		const data::Value* dom = nullptr; // the node object, re-read once the node is seated
		std::unique_ptr<Node> node;
		std::string kind;
		std::string idText; // the id AS WRITTEN — the remap key edges and editor blobs use
		NodeId requested;	// parsed and non-nil, else null: "mint one and say so"
		bool boundaryIn = false;
		bool boundaryOut = false;
	};

	// One level of the load: nodes (recursing into groups), then edges, then the editor blobs.
	// Returns the graph rather than filling one, because its boundary pair is born with the
	// document's ids — a group's inner graph is move-assigned from this, exactly as the root is.
	Graph loadBody(const data::Value& document, EditorTree& editor, LoadContext& ctx, IdPolicy policy)
	{
		const auto error = [&](std::string msg)
		{ ctx.error(std::move(msg)); };
		const auto warn = [&](std::string msg)
		{ ctx.warn(std::move(msg)); };
		const core::Factory<Node>& factory = ctx.factory;
		const ValueCodecs& codecs = ctx.codecs;

		// --- stage: read every node header and create the node, deciding nothing yet ---
		std::vector<StagedNode> staged;
		if (const data::Value* nodes = document.find("nodes"); nodes && nodes->asArray())
		{
			for (const data::Value& nodeV : *nodes->asArray())
			{
				const data::Value* idV = nodeV.find("id");
				const data::Value* kindV = nodeV.find("kind");
				const std::string* idText = idV ? idV->asString() : nullptr;
				const std::string* kind = kindV ? kindV->asString() : nullptr;
				if (!idText || !kind)
				{
					warn("node missing id or kind — skipped");
					continue;
				}

				auto node = factory.create(*kind);
				if (!node)
				{
					error("unknown node kind \"" + *kind + "\" — node and its edges skipped");
					continue;
				}

				StagedNode entry;
				entry.dom = &nodeV;
				entry.kind = *kind;
				entry.idText = *idText;
				// The nil uuid and unreadable text amount to the same thing — the document names no
				// identity — so one is minted and the reader is told which case it was. Edges still
				// resolve either way: the remap is keyed by the text as written, not by a uuid.
				const auto uuid = core::Uuid::parse(*idText);
				if (uuid && !uuid->isNil())
					entry.requested = NodeId{*uuid};
				else if (uuid)
					warn("node id is the nil uuid — a fresh identity is minted for it");
				else
					warn("node id \"" + *idText + "\" is not a uuid — a fresh identity is minted for it");
				entry.boundaryIn = dynamic_cast<const GroupInputNode*>(node.get()) != nullptr;
				entry.boundaryOut = dynamic_cast<const GroupOutputNode*>(node.get()) != nullptr;
				entry.node = std::move(node);
				staged.push_back(std::move(entry));
			}
		}

		// The FIRST valid boundary id of each side seats the pair. A document that names neither
		// (hand-written, truncated) is not an error: the pair is a graph invariant, so the missing
		// id is minted and reported, and the file still opens.
		BoundaryIds boundary;
		bool adoptedInput = false;
		bool adoptedOutput = false;
		for (const StagedNode& entry : staged)
		{
			if (entry.boundaryIn && !adoptedInput)
			{
				boundary.input = entry.requested;
				adoptedInput = true;
			}
			else if (entry.boundaryOut && !adoptedOutput)
			{
				boundary.output = entry.requested;
				adoptedOutput = true;
			}
		}
		if (!adoptedInput)
			warn("document has no GroupInput node — the graph's own is used (its interface pair is an invariant)");
		if (!adoptedOutput)
			warn("document has no GroupOutput node — the graph's own is used (its interface pair is an invariant)");

		// A COPY keeps none of the document's identities: see IdPolicy.
		Graph graph = (policy == IdPolicy::Preserve) ? Graph{boundary} : Graph{};

		// --- adopt: seat each staged node, in array order, and replay its definition ---
		IdRemap remap;
		bool seatedInput = false;
		bool seatedOutput = false;
		for (StagedNode& entry : staged)
		{
			// Boundary nodes are ADOPTED onto the pair the graph was born with, not added. Their
			// ids are already the document's (above), the graph's are pinless, so the document's
			// dynamic pins replay onto them exactly as onto a fresh node. Adding would be refused,
			// and this node's edges lost with it. A SECOND of either is reported and skipped rather
			// than merged, which would silently give two documents' pins to one node.
			NodeId liveId;
			if (entry.boundaryIn)
			{
				if (seatedInput)
				{
					warn("document has a second GroupInput — skipped (a graph has exactly one)");
					continue;
				}
				seatedInput = true;
				liveId = graph.boundaryInputNode().id();
			}
			else if (entry.boundaryOut)
			{
				if (seatedOutput)
				{
					warn("document has a second GroupOutput — skipped (a graph has exactly one)");
					continue;
				}
				seatedOutput = true;
				liveId = graph.boundaryOutputNode().id();
			}
			else if (policy == IdPolicy::Preserve)
			{
				liveId = graph.add(std::move(entry.node), entry.requested);
				// add() never overwrites an owner, so a document naming one id twice costs the
				// second node its identity, not the first node its existence.
				if (entry.requested != NodeId{} && liveId != entry.requested)
					warn("duplicate node id " + entry.idText + " — the second node was given a fresh identity");
			}
			else
			{
				liveId = graph.add(std::move(entry.node));
			}

			if (liveId == NodeId{})
			{
				error("node of kind \"" + entry.kind + "\" was refused by the graph — node and its edges skipped");
				continue;
			}
			remap[entry.idText] = liveId;
			const data::Value& nodeV = *entry.dom;
			Node& created = graph.node(liveId);

			// A user-chosen title (Node::setName) — display only, so an absent/blank one simply
			// leaves the name the node's constructor gave it.
			if (const data::Value* nameV = nodeV.find("name"))
			{
				if (const std::string* name = nameV->asString(); name && !name->empty())
					created.setName(*name);
			}

			// Replay dynamic pins BEFORE edges, so an edge addressing one resolves.
			if (const data::Value* pins = nodeV.find("dynamicPins"))
			{
				auto* dynamic = dynamic_cast<DynamicPortsNode*>(&created);
				const data::Value::Array* arr = pins->asArray();
				if (dynamic && arr)
				{
					for (const data::Value& pinV : *arr)
					{
						const data::Value* nameV = pinV.find("name");
						const data::Value* typeV = pinV.find("type");
						const std::string* pinName = nameV ? nameV->asString() : nullptr;
						const std::string* typeKey = typeV ? typeV->asString() : nullptr;
						if (!pinName || !typeKey)
						{
							warn("dynamic pin missing name or type — skipped");
							continue;
						}
						if (addPortOfType(*dynamic, *typeKey, *pinName) == PortId{})
							warn("dynamic pin \"" + *pinName + "\" (type \"" + *typeKey + "\") could not be added — skipped");
					}
				}
				else if (!dynamic)
				{
					warn("node \"" + created.name() + "\" has dynamicPins but is not a dynamic-ports node — ignored");
				}
			}

			if (const data::Value* params = nodeV.find("params"))
				readParams(created, *params, codecs, ctx.issues);

			// A group node: rebuild what it CONTAINS, then re-derive its own ports from that
			// inner boundary. Both happen before this level's edges are resolved below, which is
			// what lets an edge addressing one of the group's ports by name find it.
			if (auto* linked = dynamic_cast<LinkedGroupNode*>(&created))
			{
				loadLinkedGroup(*linked, nodeV, editor.groups[liveId], ctx);
			}
			else if (auto* group = dynamic_cast<GroupNode*>(&created))
			{
				// An INLINE group's body is part of THIS document, so it inherits the id policy
				// (and, having no version of its own, never re-enters the router).
				if (const data::Value* innerBody = nodeV.find("graph"))
					group->inner() = loadBody(*innerBody, editor.groups[liveId], ctx, policy);
			}
			if (created.innerGraph() != nullptr)
				edit::syncGroupPorts(graph, liveId);
		}

		// Edges: resolve endpoints by node id + port name, reconnect through Graph::connect.
		if (const data::Value* edges = document.find("edges"); edges && edges->asArray())
		{
			for (const data::Value& edgeV : *edges->asArray())
			{
				const data::Value* fromRef = edgeV.find("from");
				const data::Value* toRef = edgeV.find("to");
				const auto from = fromRef ? resolveEndpoint(*fromRef, Port::Direction::Output, graph, remap) : std::nullopt;
				const auto to = toRef ? resolveEndpoint(*toRef, Port::Direction::Input, graph, remap) : std::nullopt;
				if (!from || !to)
				{
					warn("edge endpoint could not be resolved — skipped");
					continue;
				}

				if (const Connection outcome = graph.connect(*from, *to); outcome != Connection::Ok)
					warn("edge rejected (" + std::string(meta::enums::name(outcome)) + ") — skipped");
			}
		}

		// Editor section: each blob is keyed by the node id as written, so the remap hands it to the
		// live node — usually the same id, but not when it was adopted, re-minted or instantiated.
		// A blob for a node that was skipped is dropped.
		if (const data::Value* editorSection = document.find("editor"))
		{
			if (const data::Value::Object* obj = editorSection->asObject())
			{
				for (const auto& [key, blob] : *obj)
				{
					if (const auto live = remap.find(key); live != remap.end())
						editor.nodes[live->second] = blob;
				}
			}
		}
		return graph;
	}
} // namespace lain::flow::serialize
