#include "lain/flow/serialize/serialize.h"

#include <lain/flow/edit.h>				// syncGroupPorts — a loaded group re-derives its ports
#include <lain/flow/group.h>			// GroupNode / LinkedGroupNode — the recursion's subject
#include <lain/flow/porttyperegistry.h> // addPortOfType / portTypeKey (+ DynamicPortsNode)
#include <lain/log/log.h>
#include <lain/meta/enums.h>

#include <charconv>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <system_error>
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
		const PortIndex count = (direction == Port::Direction::Input) ? node.inputCount() : node.outputCount();
		for (PortIndex i = 0; i < count; ++i)
		{
			const Port& port = (direction == Port::Direction::Input) ? node.input(i) : node.output(i);
			if (port.name() == name)
				return &port;
		}
		return nullptr;
	}

	static Param* findParamByName(Node& node, const std::string& name)
	{
		for (PortIndex i = 0; i < node.paramCount(); ++i)
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
			const GroupInputNode& boundaryIn = const_cast<Graph&>(inner).boundaryInputNode();
			for (PortIndex i = 0; i < boundaryIn.outputCount(); ++i)
				inputs.push_back(PinSpec{boundaryIn.output(i).name(), portTypeKey(boundaryIn.output(i).type())});
			const GroupOutputNode& boundaryOut = const_cast<Graph&>(inner).boundaryOutputNode();
			for (PortIndex i = 0; i < boundaryOut.inputCount(); ++i)
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

	static data::Value nodeToValue(const Node& node, std::int64_t fileId, const std::string& kind, const ValueCodecs& codecs,
								   const core::Factory<Node>& factory, const EditorTree* subtree)
	{
		data::Value out = data::Value::object();
		out.set("id", data::Value(fileId));
		out.set("kind", data::Value(kind));
		out.set("name", data::Value(node.name())); // the node's title — user-editable, so it round-trips

		data::Value params = data::Value::array();
		for (PortIndex i = 0; i < node.paramCount(); ++i)
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
			const PortIndex count = (side == Port::Direction::Input) ? node.inputCount() : node.outputCount();
			data::Value pins = data::Value::array();
			for (PortIndex i = 0; i < count; ++i)
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
		// Assign dense, canonical file ids (1..N) in id order — so a file we write is canonical and
		// round-trips byte-identically.
		std::map<NodeId, std::int64_t> fileId;
		std::int64_t next = 1;

		data::Value nodes = data::Value::array();
		for (const NodeId id : graph.nodeIds())
		{
			const Node& node = graph.node(id);
			const std::string kind = factory.keyOf(node);
			if (kind.empty())
				continue; // unregistered node type — can't name it; skip (best-effort save)
			fileId[id] = next;
			const auto subtree = editor.groups.find(id);
			nodes.push(nodeToValue(node, next, kind, codecs, factory, subtree == editor.groups.end() ? nullptr : &subtree->second));
			++next;
		}

		data::Value edges = data::Value::array();
		for (const Graph::Edge& edge : graph.edges())
		{
			const auto from = fileId.find(edge.from.node);
			const auto to = fileId.find(edge.to.node);
			if (from == fileId.end() || to == fileId.end())
				continue; // an endpoint node was skipped
			const Port* fromPort = graph.node(edge.from.node).findOutput(edge.from.port);
			const Port* toPort = graph.node(edge.to.node).findInput(edge.to.port);
			if (!fromPort || !toPort)
				continue;

			data::Value fromRef = data::Value::object();
			fromRef.set("node", data::Value(from->second));
			fromRef.set("port", data::Value(fromPort->name()));
			data::Value toRef = data::Value::object();
			toRef.set("node", data::Value(to->second));
			toRef.set("port", data::Value(toPort->name()));

			data::Value e = data::Value::object();
			e.set("from", std::move(fromRef));
			e.set("to", std::move(toRef));
			edges.push(std::move(e));
		}

		// Editor section: the adapter's per-node blobs, keyed by FILE id (so it survives the load
		// remap). Only nodes that were serialised (in fileId) and have a non-null blob appear.
		data::Value editorSection = data::Value::object();
		for (const auto& [liveId, fid] : fileId)
		{
			const auto it = editor.nodes.find(liveId);
			if (it != editor.nodes.end() && !it->second.isNull())
				editorSection.set(std::to_string(fid), it->second);
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

	// Resolve a `{ node, port }` reference (fileId + port name) to a live PortAddress, or nullopt.
	static std::optional<PortAddress> resolveEndpoint(const data::Value& ref, Port::Direction direction,
													  Graph& graph, const std::map<std::int64_t, NodeId>& remap)
	{
		const data::Value* nodeRef = ref.find("node");
		const data::Value* portRef = ref.find("port");
		const auto fileId = nodeRef ? asInteger(*nodeRef) : std::nullopt;
		const std::string* portName = portRef ? portRef->asString() : nullptr;
		if (!fileId || !portName)
			return std::nullopt;

		const auto live = remap.find(*fileId);
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

	static void loadBody(const data::Value& body, Graph& graph, EditorTree& editor, LoadContext& ctx);

	// The pins a linked group's template actually exposes, as PinSpecs — what the cached interface is
	// rectified against.
	static void currentInterface(Graph& inner, std::vector<PinSpec>& inputs, std::vector<PinSpec>& outputs)
	{
		GroupInputNode& boundaryIn = inner.boundaryInputNode();
		for (PortIndex i = 0; i < boundaryIn.outputCount(); ++i)
			inputs.push_back(PinSpec{boundaryIn.output(i).name(), portTypeKey(boundaryIn.output(i).type())});
		GroupOutputNode& boundaryOut = inner.boundaryOutputNode();
		for (PortIndex i = 0; i < boundaryOut.inputCount(); ++i)
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
	static void loadLinkedGroup(LinkedGroupNode& linked, const data::Value& nodeV, LoadContext& ctx)
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
		EditorTree ignored; // a template's own layout belongs to that document, not to this parent
		loadBody(resolved->document, linked.inner(), ignored, ctx);
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

		// Version gate: a too-new document can't be half-understood -> fatal (empty graph).
		if (const data::Value* versionV = document.find("version"))
		{
			if (const auto version = asInteger(*versionV); version && *version > kFormatVersion)
			{
				ctx.error("document version " + std::to_string(*version) + " is newer than supported " + std::to_string(kFormatVersion));
				return result;
			}
		}
		else
		{
			ctx.warn("document has no version field — assuming current");
		}

		loadBody(document, result.graph, result.editor, ctx);
		return result;
	}

	// One level of the load: nodes (recursing into groups), then edges, then the editor blobs.
	// `graph` is filled in place, so a group's inner graph loads through the same path as the root.
	void loadBody(const data::Value& document, Graph& graph, EditorTree& editor, LoadContext& ctx)
	{
		const auto error = [&](std::string msg)
		{ ctx.error(std::move(msg)); };
		const auto warn = [&](std::string msg)
		{ ctx.warn(std::move(msg)); };
		const core::Factory<Node>& factory = ctx.factory;
		const ValueCodecs& codecs = ctx.codecs;

		// Nodes: create by kind (fresh ids), read params. Build the fileId -> live NodeId remap.
		std::map<std::int64_t, NodeId> remap;
		bool adoptedInput = false; // the document's boundary pair maps onto the graph's own (below)
		bool adoptedOutput = false;
		if (const data::Value* nodes = document.find("nodes"); nodes && nodes->asArray())
		{
			for (const data::Value& nodeV : *nodes->asArray())
			{
				const data::Value* idV = nodeV.find("id");
				const data::Value* kindV = nodeV.find("kind");
				const auto fileId = idV ? asInteger(*idV) : std::nullopt;
				const std::string* kind = kindV ? kindV->asString() : nullptr;
				if (!fileId || !kind)
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

				// Boundary nodes are ADOPTED, not added. Every Graph is constructed with exactly one
				// GroupInputNode + one GroupOutputNode (the interface invariant), so the document's
				// pair maps onto the pair that already exists — the graph's is pinless, so the
				// document's dynamic pins replay onto it exactly as they would onto a fresh node.
				// Adding would be refused, and this node's edges would be lost with it.
				NodeId liveId;
				if (dynamic_cast<const GroupInputNode*>(node.get()) != nullptr)
				{
					if (adoptedInput)
						warn("document has a second GroupInput — merged into the graph's own");
					adoptedInput = true;
					liveId = graph.boundaryInputNode().id();
				}
				else if (dynamic_cast<const GroupOutputNode*>(node.get()) != nullptr)
				{
					if (adoptedOutput)
						warn("document has a second GroupOutput — merged into the graph's own");
					adoptedOutput = true;
					liveId = graph.boundaryOutputNode().id();
				}
				else
				{
					liveId = graph.add(std::move(node));
				}

				if (liveId == NodeId{})
				{
					error("node of kind \"" + *kind + "\" was refused by the graph — node and its edges skipped");
					continue;
				}
				remap[*fileId] = liveId;
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
					loadLinkedGroup(*linked, nodeV, ctx);
				}
				else if (auto* group = dynamic_cast<GroupNode*>(&created))
				{
					if (const data::Value* innerBody = nodeV.find("graph"))
						loadBody(*innerBody, group->inner(), editor.groups[liveId], ctx);
				}
				if (created.innerGraph() != nullptr)
					edit::syncGroupPorts(graph, liveId);
			}
		}

		// Edges: resolve endpoints by fileId + port name, reconnect through Graph::connect.
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

		// Editor section: re-key each file-id blob to its fresh live NodeId (via the remap) so the
		// adapter applies it directly. A blob for a node that was skipped is dropped.
		if (const data::Value* editorSection = document.find("editor"))
		{
			if (const data::Value::Object* obj = editorSection->asObject())
			{
				for (const auto& [key, blob] : *obj)
				{
					std::int64_t fileId = 0;
					if (std::from_chars(key.data(), key.data() + key.size(), fileId).ec != std::errc{})
						continue; // a non-numeric editor key — ignore
					if (const auto live = remap.find(fileId); live != remap.end())
						editor.nodes[live->second] = blob;
				}
			}
		}
	}
} // namespace lain::flow::serialize
