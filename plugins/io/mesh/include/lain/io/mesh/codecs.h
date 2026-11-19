#pragma once

namespace lain::io::mesh
{
	// Register every built-in mesh codec — the plugins enabled at build time — into the reader registry.
	// Call once at startup, before load(); the app's single codec-wiring point, mirroring
	// registerImageCodecs. Generated from the discovered codec list, so adding a codec plugin needs no
	// edit here. With no codecs enabled it is a no-op.
	void registerMeshCodecs();
} // namespace lain::io::mesh
