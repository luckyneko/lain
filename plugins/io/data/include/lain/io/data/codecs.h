#pragma once

namespace lain::io::data
{
	// Register every built-in serialization codec — the plugins enabled at build time — into the
	// reader/writer registries. Call once at startup, before load()/save(); the app's single
	// codec-wiring point, mirroring registerImageCodecs.
	//
	// The set is build-determined (which plugins/io/data/<fmt> were enabled), not hand-listed:
	// this function is generated from the discovered codec list, so adding a codec plugin needs no
	// edit here. With no codecs enabled it is a no-op.
	void registerDataCodecs();
} // namespace lain::io::data
