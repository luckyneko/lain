#include <archimedes/archimedes.h>
#include <lain/app/application.h>
#include <lain/app/window.h>
#include <lain/gui/context.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <imnodes.h>

#include <cstdint>
#include <string>
#include <utility>

namespace lain::gui
{
	struct Context::impl
	{
		ImGuiContext* ctx{nullptr};
		ImNodesContext* nodesCtx{nullptr};
		std::string iniFilename; // kept alive: ImGui stores io.IniFilename by pointer
	};

	Context::Context(app::Application& app, app::Window& window, std::string iniFilename)
		: m(std::make_unique<impl>())
	{
		acm::Device device = app.device();
		acm::Instance instance = app.instance();

		IMGUI_CHECKVERSION();
		m->ctx = ImGui::CreateContext();
		ImGui::StyleColorsDark();

		// Point ImGui at our owned string (or disable persistence when empty). Set
		// before the first newFrame() so a saved layout loads on boot.
		m->iniFilename = std::move(iniFilename);
		ImGui::GetIO().IniFilename = m->iniFilename.empty() ? nullptr : m->iniFilename.c_str();

		// The node canvas (lain::gui::nodes) rides on this window's ImGui context.
		m->nodesCtx = ImNodes::CreateContext();
		ImNodes::SetImGuiContext(m->ctx);
		ImNodes::StyleColorsDark();

		ImGui_ImplGlfw_InitForVulkan(static_cast<GLFWwindow*>(window.nativeHandle()), true);

		const uint32_t images = static_cast<uint32_t>(window.swapChain().getRenderTargetCount());

		ImGui_ImplVulkan_InitInfo info{};
		info.ApiVersion = VK_API_VERSION_1_0;
		info.Instance = instance.vkInstance();
		info.PhysicalDevice = device.getGPU().device;
		info.Device = device.vkDevice();
		info.QueueFamily = device.getQueueIdx();
		info.Queue = device.vkQueue();
		info.DescriptorPoolSize = 64; // > 0 -> the backend creates + owns its pool
		info.MinImageCount = 2;
		info.ImageCount = images < 2 ? 2 : images;
		info.PipelineInfoMain.RenderPass = window.swapChain().vkRenderPass();
		info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT; // inspector window is single-sampled
		ImGui_ImplVulkan_Init(&info);
	}

	Context::~Context()
	{
		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImNodes::DestroyContext(m->nodesCtx);
		ImGui::DestroyContext(m->ctx);
	}

	void Context::newFrame()
	{
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
	}

	void Context::render(acm::CommandBuffer cmd)
	{
		ImGui::Render();
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd.vkCommandBuffer());
	}

	ImTextureID Context::image(acm::Texture texture, acm::Sampler sampler)
	{
		VkDescriptorSet set = ImGui_ImplVulkan_AddTexture(sampler.vkSampler(), texture.vkImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		return reinterpret_cast<ImTextureID>(set);
	}
} // namespace lain::gui
