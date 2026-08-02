#include "version1.h"

#include "lain/flow/serialize/serialize.h" // kFormatVersion — what the migrated document claims to be

#include <lain/core/uuid.h>

#include <charconv>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace lain::flow::serialize::detail
{
	// A v1 node id could arrive as either integer arm (JSON collapses a positive Int to UInt), so
	// read both. Mirrors serialize.cpp's asInteger — duplicated rather than shared because this
	// file is meant to be deletable whole.
	static std::optional<std::int64_t> asInteger(const data::Value& value)
	{
		if (auto i = value.asInt64())
			return *i;
		if (auto u = value.asUInt64())
			return static_cast<std::int64_t>(*u);
		return std::nullopt;
	}

	// One body's file id -> the uuid string minted for it. Each body (the document root, and every
	// inline group) numbered its nodes from 1 independently, so each gets its own map.
	using IdMap = std::map<std::int64_t, std::string>;

	static data::Value migrateBody(const data::Value& body);

	// Rewrite one `{ node: <int>, port: <name> }` edge endpoint. An id the map does not know is
	// left alone: the v1 loader dropped such an edge too, and leaving it lets the v2 decoder report
	// the unresolvable endpoint in its own words rather than inventing a plausible one here.
	static data::Value migrateEndpoint(const data::Value& endpoint, const IdMap& ids)
	{
		data::Value out = endpoint;
		if (const data::Value* nodeV = endpoint.find("node"))
		{
			if (const auto fileId = asInteger(*nodeV))
			{
				if (const auto it = ids.find(*fileId); it != ids.end())
					out.set("node", data::Value(it->second));
			}
		}
		return out;
	}

	static data::Value migrateBody(const data::Value& body)
	{
		const data::Value::Object* object = body.asObject();
		if (!object)
			return body; // not a body shape — pass it through and let the decoder say so

		// Pass one: mint an identity per node, so edges and editor keys below can be rewritten.
		IdMap ids;
		if (const data::Value* nodes = body.find("nodes"); nodes && nodes->asArray())
		{
			for (const data::Value& nodeV : *nodes->asArray())
			{
				const data::Value* idV = nodeV.find("id");
				const auto fileId = idV ? asInteger(*idV) : std::nullopt;
				if (fileId && ids.count(*fileId) == 0)
					ids[*fileId] = core::Uuid::generate().toString();
			}
		}

		// Pass two: rebuild the body, key by key, so anything this version did not know about
		// survives untouched and the key order (which the json codec preserves) stays put.
		data::Value out = data::Value::object();
		for (const auto& [key, value] : *object)
		{
			if (key == "version")
			{
				out.set(key, data::Value(kFormatVersion)); // what it now IS, not what it was
			}
			else if (key == "nodes" && value.asArray())
			{
				data::Value migrated = data::Value::array();
				for (const data::Value& nodeV : *value.asArray())
				{
					data::Value node = nodeV;
					const data::Value* idV = nodeV.find("id");
					const auto fileId = idV ? asInteger(*idV) : std::nullopt;
					if (fileId)
					{
						if (const auto it = ids.find(*fileId); it != ids.end())
							node.set("id", data::Value(it->second));
					}
					// An INLINE group carries a body of its own, numbered in its own namespace —
					// so it migrates recursively, with a fresh id map. (A LINKED group stores only
					// a path plus its interface cache: no ids, nothing to rewrite. Its template is
					// a document in its own right and enters the version router separately.)
					if (const data::Value* inner = nodeV.find("graph"))
						node.set("graph", migrateBody(*inner));
					migrated.push(std::move(node));
				}
				out.set(key, std::move(migrated));
			}
			else if (key == "edges" && value.asArray())
			{
				data::Value migrated = data::Value::array();
				for (const data::Value& edgeV : *value.asArray())
				{
					data::Value edge = edgeV;
					if (const data::Value* from = edgeV.find("from"))
						edge.set("from", migrateEndpoint(*from, ids));
					if (const data::Value* to = edgeV.find("to"))
						edge.set("to", migrateEndpoint(*to, ids));
					migrated.push(std::move(edge));
				}
				out.set(key, std::move(migrated));
			}
			else if (key == "editor" && value.asObject())
			{
				// Keyed by the decimal file id; re-key to the uuid string. A blob for a node that
				// is not in the array is dropped, as the v1 loader's remap lookup did.
				data::Value migrated = data::Value::object();
				for (const auto& [blobKey, blob] : *value.asObject())
				{
					std::int64_t fileId = 0;
					if (std::from_chars(blobKey.data(), blobKey.data() + blobKey.size(), fileId).ec != std::errc{})
						continue; // a non-numeric editor key was never ours — the v1 loader ignored it too
					if (const auto it = ids.find(fileId); it != ids.end())
						migrated.set(it->second, blob);
				}
				out.set(key, std::move(migrated));
			}
			else
			{
				out.set(key, value);
			}
		}
		return out;
	}

	data::Value migrateVersion1(const data::Value& document)
	{
		return migrateBody(document);
	}
} // namespace lain::flow::serialize::detail
