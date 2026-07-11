#pragma once

namespace lain::io::data::json
{
	// Register this plugin's JSON reader + writer into the io::data registries (under "json").
	// Called once by the generated registerDataCodecs() aggregator — or directly by a consumer
	// that wants only JSON. The codec attaches through this seam; nothing in the io::data core
	// names nlohmann.
	void registerCodec();
} // namespace lain::io::data::json
