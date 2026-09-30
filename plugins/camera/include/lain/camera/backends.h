#pragma once

namespace lain::camera
{
	// Register every camera backend enabled at build time into lain::camera's registries (the board
	// renderer and detector today). Call once at startup; it is the app's single camera-backend
	// wiring point, mirroring registerVideoCodecs.
	//
	// Generated from the discovered plugin list, so adding a plugin needs no edit here. With no
	// plugin enabled it is a no-op and calling it is still right: the capability queries
	// (board::canDetect, board::canRender) then answer false, and a host registers no camera node
	// kind that could not run (ADR-0016).
	void registerCameraBackends();
} // namespace lain::camera
