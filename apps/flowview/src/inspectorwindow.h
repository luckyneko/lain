#pragma once

#include "parameditors.h"

#include <lain/app/windowdelegate.h>
#include <lain/flow/types.h> // PortIndex
#include <lain/gui/context.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>

namespace lain::flow
{
	class Graph;
}

namespace lain::image
{
	class Image;
}

namespace flowview
{
	class FlowviewApp; // the delegate whose graph the Interface panel binds
	// Thumbnail size for the image preview, chosen at runtime via a lain::gui::enumCombo
	// (its labels come from lain::meta::enums).
	enum class PreviewSize
	{
		Small,
		Medium,
		Large,
	};

	// Identifies one port's preview by its stable logical position (node + direction +
	// index) — a key that survives recomputes but not node deletion. No GPU/ImGui types.
	struct PinKey
	{
		std::uint64_t node;
		bool output;
		lain::flow::PortIndex port;

		bool operator<(const PinKey& o) const
		{
			if (node != o.node)
				return node < o.node;
			if (output != o.output)
				return output < o.output;
			return port < o.port;
		}
	};

	// Per-window ImGui inspector. Owns only its GUI resources (the lain::gui Context + the
	// per-port preview cache); it holds no app/graph state. Each hook reaches what it needs
	// from its Window argument — window.app() for the Application, and
	// window.app().getDelegate<FlowviewApp>().graph() for the scene it draws. The panel
	// shows each node's ports as text and a lain::image::Image output as a live thumbnail.
	// The Vulkan/ImGui plumbing of that thumbnail lives in lain::gui — this adapter only
	// holds gui::Texture handles keyed by pin. ImGui is single-threaded — every call here
	// is on the main/render thread.
	class InspectorWindow : public lain::app::WindowDelegate
	{
	public:
		bool onInit(lain::app::Window& window) override;
		void onRender(lain::app::Window& window, const lain::app::TimeState& time) override;
		void onShutdown(lain::app::Window& window) override;

	private:
		// Upsert a preview per ready image port (upload in place when the size/format
		// matches, else recreate) and prune previews whose port is gone. Runs only when the
		// scene may have changed (first frame + after an edit) — acm::Texture::upload is a
		// synchronous submit, so it must not run every frame.
		void refreshPreviews(const lain::flow::Graph& graph);

		// The graph's I/O boundary as a panel (Inputs: Bind file…; Outputs: thumbnail +
		// Save…), driven by Graph::boundaryInputs()/outputs() — the same seam the cli binds
		// through. The host-binding surface, separate from the per-node inspector.
		void renderInterfacePanel(FlowviewApp& appDelegate);

		// The format dropdown + Save… for one image, shared by the inspector's output ports and
		// the Interface panel's outputs. `key` scopes the per-pin remembered format choice.
		void renderImageSave(const PinKey& key, const lain::image::Image& img);

		std::unique_ptr<lain::gui::Context> m_guiCtx;
		ParamEditors m_paramEditors;					 // type-keyed param editors (registered in onInit)
		std::map<PinKey, lain::gui::Texture> m_previews; // one uploaded thumbnail per image port
		bool m_previewsDirty = true;					 // rebuild previews on the next frame (init + after edits)
		bool m_laidOut = false;							 // node canvas: seed node positions on the first frame
		int m_addCounter = 0;							 // palette-added nodes cascade their position
		PreviewSize m_previewSize = PreviewSize::Medium; // thumbnail size (enumCombo-driven)

		// The chosen save format per image output pin (the inline dropdown's selection), by format
		// key ("png" / "jpg" / …). Robust to the savable list changing — an entry not (or no
		// longer) in a port's list falls back to that list's first format.
		std::map<PinKey, std::string> m_saveFormat;
	};
} // namespace flowview
