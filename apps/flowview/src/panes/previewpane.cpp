#include "previewpane.h"

#include "../appcontext.h"
#include "../previewcache.h"
#include "../valueviews.h"

#include <lain/flow/evaluation.h>
#include <lain/flow/graph.h>
#include <lain/flow/node.h>
#include <lain/flow/port.h>
#include <lain/gui/dock.h> // activateWindowTab
#include <lain/gui/gui.h>
#include <lain/math/types.h>

#include <string>

namespace flowview
{
	using namespace lain;

	PreviewPane::PreviewPane() = default;
	PreviewPane::~PreviewPane() = default;

	void PreviewPane::releaseView()
	{
		m_view.reset();
		m_shownKey.reset();
	}

	// The Preview window contents (no Begin/End — the caller owns those). Early-returns freely.
	void PreviewPane::drawContents(AppContext& ctx, const flow::Graph& graph, const flow::Evaluation& evaluation,
								   const PreviewCache& previews, const ValueViews& views, gui::Context& gui)
	{
		if (!ctx.previewTarget)
		{
			releaseView(); // nothing on screen -> nothing holding a texture
			gui::TextDisabled("Click an asset thumbnail to preview it here.");
			return;
		}

		// Resolve the targeted port; drop the target if its node/port is gone, empty, or of a type
		// nothing can show.
		const PinKey key = *ctx.previewTarget;
		// Resolved against the level the KEY names, not merely the level on screen — the key now says
		// which. This pane only ever holds the active graph, so a target from elsewhere simply does
		// not resolve and is dropped below; what changed is that it can no longer resolve to the
		// WRONG pin by sharing ids with one here.
		const bool sameLevel = key.path == ctx.activePath;
		const flow::Node* node = (sameLevel && graph.contains(key.port.node)) ? &graph.node(key.port.node) : nullptr;
		const flow::Port* port = node != nullptr ? node->findOutput(key.port.port) : nullptr;
		if (node != nullptr && port == nullptr)
			port = node->findInput(key.port.port);
		const flow::PortValue& value = (node != nullptr && port != nullptr) ? evaluation.value(key.port) : flow::PortValue{};
		if (node == nullptr || port == nullptr || value.empty() || !views.has(port->type()))
		{
			ctx.previewTarget.reset();
			releaseView();
			gui::TextDisabled("(the previewed asset is no longer available)");
			return;
		}

		// A different asset gets a fresh view: its state — zoom, or a playback position — has nothing
		// to do with the last one's.
		if (m_shownKey != key || m_view == nullptr)
		{
			m_shownKey = key;
			m_view = views.make(port->type());
			if (m_view == nullptr)
				return; // has() said otherwise a line ago; nothing to draw either way
		}

		// Header: what you are looking at — which node's which pin, and what the value IS. The detail
		// comes from Evaluation::describe(), the PortType capability every pane already reads, so a
		// newly viewable type says what it is without a per-view string of its own. ASCII only — the
		// default font has no fancy separators.
		gui::Text("%s [%s]  |  %s : %s", node->name().c_str(), key.port.node.shortString().c_str(),
				  port->name().c_str(), evaluation.describe(key.port).c_str());
		gui::Separator();

		const math::Vec2f area = gui::GetContentRegionAvail();
		if (area.x <= 0.0f || area.y <= 0.0f)
			return;
		m_view->draw(value, previews.find(key), area, gui);
	}

	void PreviewPane::draw(AppContext& ctx, const flow::Graph& graph, const flow::Evaluation& evaluation,
						   const PreviewCache& previews, const ValueViews& views, gui::Context& gui)
	{
		if (ctx.activatePreview)
		{
			gui::activateWindowTab("Preview"); // flip the Graph/Preview tab group to Preview
			ctx.activatePreview = false;
		}
		if (gui::Begin("Preview"))
			drawContents(ctx, graph, evaluation, previews, views, gui);
		gui::End();
	}
} // namespace flowview
