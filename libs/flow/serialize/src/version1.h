#pragma once

#include <lain/data/value.h>

// PRIVATE to flow-serialize — the only place that knows anything about the version-1 document
// format. Not under include/: nothing outside this library may reach for it.
//
// Version 1 addressed nodes by a per-body integer that the writer renumbered 1..N on every save;
// version 2 addresses them by the uuid a node actually carries (ADR-0011). Rather than keep a
// second graph loader alive, v1 is handled as a DOM-to-DOM translation: this turns a v1
// data::Value into the v2 shape, and the sole v2 decoder then loads it.
//
// That is what makes v1 REMOVABLE later — deleting support means deleting this translation unit,
// its dispatch case in serialize.cpp, and the v1 fixtures, without touching the v2 writer or
// decoder.
namespace lain::flow::serialize::detail
{
	// Rewrite a version-1 document as a version-2 one: every node's integer id becomes a freshly
	// minted uuid string, and the edge endpoints and editor keys that referenced it follow —
	// recursively through inline group bodies, each of which numbered its nodes independently.
	//
	// The ids are MINTED, not derived: v1 stored no durable identity (the writer renumbered on
	// every save), so there is nothing to preserve. Opening the same v1 file twice therefore yields
	// different ids, which is honest — identity begins when the document is saved as v2.
	data::Value migrateVersion1(const data::Value& document);
} // namespace lain::flow::serialize::detail
