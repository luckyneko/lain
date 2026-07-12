#include "lain/flow/serialize/serialize.h"

#include <lain/log/log.h>
#include <lain/meta/enums.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>

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
			if (node.param(i).name() == name)
				return &node.param(i);
		return nullptr;
	}

	// --- save -----------------------------------------------------------------

	static data::Value nodeToValue(const Node& node, std::int64_t fileId, const std::string& kind, const ValueCodecs& codecs)
	{
		data::Value out = data::Value::object();
		out.set("id", data::Value(fileId));
		out.set("kind", data::Value(kind));
		out.set("name", data::Value(node.name())); // informational (node names are fixed by type)

		data::Value params = data::Value::array();
		for (PortIndex i = 0; i < node.paramCount(); ++i)
			if (auto p = paramToValue(node.param(i), codecs))
				params.push(std::move(*p));
		if (const data::Value::Array* arr = params.asArray(); arr && !arr->empty())
			out.set("params", std::move(params));

		return out;
	}

	data::Value toValue(const Graph& graph, const core::Factory<Node>& factory, const ValueCodecs& codecs)
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
			nodes.push(nodeToValue(node, next, kind, codecs));
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

		data::Value document = data::Value::object();
		document.set("version", data::Value(kFormatVersion));
		document.set("nodes", std::move(nodes));
		document.set("edges", std::move(edges));
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

	LoadResult fromValue(const data::Value& document, const core::Factory<Node>& factory, const ValueCodecs& codecs)
	{
		LoadResult result;
		const auto error = [&](std::string msg)
		{
			log::error("flow::serialize: {}", msg);
			result.issues.push_back({Severity::Error, std::move(msg)});
		};
		const auto warn = [&](std::string msg)
		{
			log::warn("flow::serialize: {}", msg);
			result.issues.push_back({Severity::Warning, std::move(msg)});
		};

		// Version gate: a too-new document can't be half-understood -> fatal (empty graph).
		if (const data::Value* versionV = document.find("version"))
		{
			if (const auto version = asInteger(*versionV); version && *version > kFormatVersion)
			{
				error("document version " + std::to_string(*version) + " is newer than supported " + std::to_string(kFormatVersion));
				return result;
			}
		}
		else
		{
			warn("document has no version field — assuming current");
		}

		// Nodes: create by kind (fresh ids), read params. Build the fileId -> live NodeId remap.
		std::map<std::int64_t, NodeId> remap;
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

				const NodeId liveId = result.graph.add(std::move(node));
				remap[*fileId] = liveId;

				if (const data::Value* params = nodeV.find("params"))
					readParams(result.graph.node(liveId), *params, codecs, result.issues);
			}
		}

		// Edges: resolve endpoints by fileId + port name, reconnect through Graph::connect.
		if (const data::Value* edges = document.find("edges"); edges && edges->asArray())
		{
			for (const data::Value& edgeV : *edges->asArray())
			{
				const data::Value* fromRef = edgeV.find("from");
				const data::Value* toRef = edgeV.find("to");
				const auto from = fromRef ? resolveEndpoint(*fromRef, Port::Direction::Output, result.graph, remap) : std::nullopt;
				const auto to = toRef ? resolveEndpoint(*toRef, Port::Direction::Input, result.graph, remap) : std::nullopt;
				if (!from || !to)
				{
					warn("edge endpoint could not be resolved — skipped");
					continue;
				}

				if (const Connection outcome = result.graph.connect(*from, *to); outcome != Connection::Ok)
					warn("edge rejected (" + std::string(meta::enums::name(outcome)) + ") — skipped");
			}
		}

		return result;
	}
} // namespace lain::flow::serialize
