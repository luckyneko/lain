#pragma once

// Re-expose Nelarius/imnodes under a lain face — the node-graph canvas for ImGui. Its
// functions (BeginNodeEditor, BeginNode, Link, ...) live in namespace ImNodes; here we
// alias that to lain::gui::nodes, so a consumer writes lain::gui::nodes::BeginNode(...).
// (A sub-namespace rather than a flat using-directive like gui.h's, because ImNodes and
// ImGui share a few names — e.g. GetIO — that would otherwise collide in lain::gui.)
//
// The per-window ImNodes context is created and owned by lain::gui::Context alongside
// the ImGui context (context.h); a consumer just issues the editor calls per frame.

#include <imnodes.h>

namespace lain::gui
{
	namespace nodes = ImNodes;
}
