#include "inspectorwindow.h"

#include "flowviewapp.h"

#include <archimedes/archimedes.h>
#include <lain/app/application.h>
#include <lain/app/window.h>
#include <lain/flow/graph.h>
#include <lain/flow/node.h>
#include <lain/flow/port.h>
#include <lain/gui/gui.h>

#include <cstdint>
#include <string>

namespace flowview
{
	using namespace lain;

	// A port's value as inspector text — CPU scalars/strings only. Textures are shown
	// as a thumbnail by the caller, not here, so this never touches the GPU (no
	// readback, no layout change every frame).
	static std::string cpuLabel(const flow::Port& port)
	{
		const flow::PortValue& value = port.value();
		if (value.holds<int>())
			return std::to_string(value.get<int>());
		if (value.holds<float>())
			return std::to_string(value.get<float>());
		if (value.holds<double>())
			return std::to_string(value.get<double>());
		if (value.holds<bool>())
			return value.get<bool>() ? "true" : "false";
		if (value.holds<std::string>())
			return value.get<std::string>();
		return std::string("<") + value.type().name() + '>';
	}

	bool InspectorWindow::onInit(app::Window& window)
	{
		m_guiCtx = std::make_unique<gui::Context>(window.app(), window);
		m_sampler = window.app().device().createSampler();
		return true;
	}

	void InspectorWindow::onRender(app::Window& window, const app::TimeState&)
	{
		const flow::Graph& graph = window.app().getDelegate<FlowviewApp>().graph();

		m_guiCtx->newFrame();

		gui::Begin("Inspector");
		for (const flow::NodeId id : graph.topoOrder())
		{
			const flow::Node& node = graph.node(id);
			gui::Text("[%zu] %s", id, node.name().c_str());

			auto port = [&](const char* tag, const flow::Port& p)
			{
				if (!p.ready())
				{
					gui::Text("    %s %s: (empty)", tag, p.name().c_str());
					return;
				}
				if (p.type() == typeid(acm::Texture))
				{
					const acm::Texture& texture = p.value().get<acm::Texture>();
					const acm::Extent2D extent = texture.getExtent();
					gui::Text("    %s %s: acm::Texture %ux%u", tag, p.name().c_str(), extent.width, extent.height);
					if (!m_havePreview && texture.valid())
					{
						m_preview = m_guiCtx->image(texture, m_sampler);
						m_havePreview = true;
					}
					if (m_havePreview)
						gui::Image(m_preview, ImVec2(192.0f, 192.0f));
				}
				else
				{
					gui::Text("    %s %s: %s", tag, p.name().c_str(), cpuLabel(p).c_str());
				}
			};

			for (flow::PortIndex i = 0; i < node.inputCount(); ++i)
				port("in ", node.input(i));
			for (flow::PortIndex i = 0; i < node.outputCount(); ++i)
				port("out", node.output(i));
		}
		gui::End();

		window.renderer().render([&](acm::CommandBuffer cmd, uint32_t)
								 { m_guiCtx->render(cmd); });
	}

	void InspectorWindow::onShutdown(app::Window&)
	{
		m_guiCtx.reset(); // destroy the ImGui backends before the device tears down
		m_sampler = {};	  // drop the sampler handle
		m_havePreview = false;
	}
} // namespace flowview
