#pragma once

namespace lain::io::image
{
	// Register every built-in image codec — the plugins enabled at build time — into the
	// reader registry. Call once at startup, before load()/decode(); it is the app's
	// single codec-wiring point, mirroring flowview's registerExampleNodes for nodes.
	//
	// The set is determined by the build (which plugins/io/image/<fmt> were enabled), not
	// hand-listed: this function is generated from the discovered codec list, so adding a
	// codec plugin needs no edit here. With no codecs enabled it is a no-op.
	void registerImageCodecs();
} // namespace lain::io::image
