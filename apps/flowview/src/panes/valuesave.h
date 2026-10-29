#pragma once

#include "../pinkey.h"

namespace lain::flow
{
	class PortValue;
}

namespace flowview
{
	struct AppContext;

	// The Save... widget for one output value, shared by the Inspector's output ports and the Interface
	// panel's outputs. An image gets a format dropdown, its encoder settings and Save...; a camera model
	// or a registered rig a format dropdown and Save..., written as the document `run --model` or `run
	// --rig` writes. A value of any other type has nothing to save and draws nothing. `key` scopes the per-pin remembered format
	// choice (AppContext::saveFormat). Draws inline (no window of its own).
	void renderValueSave(AppContext& ctx, const PinKey& key, const lain::flow::PortValue& value);
} // namespace flowview
