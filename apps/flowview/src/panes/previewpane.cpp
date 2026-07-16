#include "previewpane.h"

#include "../appcontext.h"
#include "../previewcache.h"

#include <lain/flow/graph.h>
#include <lain/flow/node.h>
#include <lain/flow/port.h>
#include <lain/gui/dock.h> // activateWindowTab
#include <lain/gui/gui.h>
#include <lain/image/image.h>
#include <lain/math/types.h>

#include <algorithm>
#include <string>

namespace flowview
{
	using namespace lain;

	// The Preview window contents (no Begin/End — the caller owns those). Early-returns freely.
	static void drawPreviewContents(AppContext& ctx, const flow::Graph& graph, const PreviewCache& previews)
	{
		if (!ctx.previewTarget)
		{
			gui::TextDisabled("Click an asset thumbnail to preview it here.");
			return;
		}

		// Resolve the targeted port; drop the target if its node/port is gone or no longer a ready image.
		const PinKey key = *ctx.previewTarget;
		const flow::NodeId nid{key.node};
		const flow::Node* node = graph.contains(nid) ? &graph.node(nid) : nullptr;
		const flow::Port* port = node != nullptr ? (key.output ? node->findOutput(key.port) : node->findInput(key.port)) : nullptr;
		const gui::Texture* tex = previews.find(key);
		if (node == nullptr || port == nullptr || !port->ready() || port->type() != typeid(image::Image) || tex == nullptr)
		{
			ctx.previewTarget.reset();
			gui::TextDisabled("(the previewed asset is no longer available)");
			return;
		}

		// Header: what you are looking at (which node's which pin, and the image size). ASCII only —
		// the default font has no fancy separators.
		const image::Image& img = port->value().get<image::Image>();
		gui::Text("%s [%llu]  |  %s : %s  (%dx%d)", node->name().c_str(), static_cast<unsigned long long>(key.node),
				  port->name().c_str(), std::string(port->typeName()).c_str(), img.width(), img.height());
		gui::Separator();

		// Fit-to-pane, preserving aspect, centred horizontally. (Zoom/pan is a future refinement.)
		const ImVec2 avail = gui::GetContentRegionAvail();
		const float imgW = static_cast<float>(img.width());
		const float imgH = static_cast<float>(img.height());
		if (avail.x <= 0.0f || avail.y <= 0.0f || imgW <= 0.0f || imgH <= 0.0f)
			return;
		const float scale = std::min(avail.x / imgW, avail.y / imgH);
		const float w = imgW * scale;
		const float h = imgH * scale;
		gui::SetCursorPosX(gui::GetCursorPosX() + std::max(0.0f, (avail.x - w) * 0.5f));
		gui::Image(*tex, math::Vec2f{w, h});
	}

	void PreviewPane::draw(AppContext& ctx, const flow::Graph& graph, const PreviewCache& previews)
	{
		if (ctx.activatePreview)
		{
			gui::activateWindowTab("Preview"); // flip the Graph/Preview tab group to Preview
			ctx.activatePreview = false;
		}
		if (gui::Begin("Preview"))
			drawPreviewContents(ctx, graph, previews);
		gui::End();
	}
} // namespace flowview
