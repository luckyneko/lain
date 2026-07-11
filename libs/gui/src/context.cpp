#include "lain/gui/context.h"

#include <archimedes/acmVulkanInterop.h>
#include <archimedes/archimedes.h>
#include <lain/app/application.h>
#include <lain/app/window.h>
#include <lain/image/image.h>

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
		std::string iniFilename;				   // kept alive: ImGui stores io.IniFilename by pointer
		VkFormat colorFormat{VK_FORMAT_UNDEFINED}; // kept alive: imgui holds pColorAttachmentFormats by pointer
		acm::Device* device{nullptr};			   // the shared device, for upload()
	};

	Context::Context(app::Application& app, app::Window& window, std::string iniFilename)
		: m(std::make_unique<impl>())
	{
		acm::Device& device = app.device();
		acm::Instance& instance = app.instance();
		m->device = &device;

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

		const uint32_t images = static_cast<uint32_t>(window.swapChain().renderTargetCount());

		// archimedes renders through dynamic rendering (there is no VkRenderPass): imgui
		// builds its pipeline from the swapchain's color format instead. imgui keeps the
		// format array by pointer, so it lives in impl for the Context's lifetime.
		m->colorFormat = acm::interop::colorFormat(window.swapChain());

		ImGui_ImplVulkan_InitInfo info{};
		info.ApiVersion = VK_API_VERSION_1_3;
		info.Instance = acm::interop::instance(instance);
		info.PhysicalDevice = acm::interop::physicalDevice(device);
		info.Device = acm::interop::device(device);
		info.QueueFamily = device.queueFamily();
		info.Queue = acm::interop::queue(device);
		info.DescriptorPoolSize = 64; // > 0 -> the backend creates + owns its pool
		info.MinImageCount = 2;
		info.ImageCount = images < 2 ? 2 : images;
		info.UseDynamicRendering = true;
		info.PipelineInfoMain.RenderPass = VK_NULL_HANDLE;
		info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT; // inspector window is single-sampled
		info.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
		info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
		info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &m->colorFormat;
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
		// ImGui's Vulkan backend lazily uploads pending font/texture atlases inside
		// NewFrame, and that upload submits to and waits on the device queue. Run it
		// under archimedes' queue mutex (via interop::withQueue) so a worker-thread GPU
		// submit can't race it once the graph evaluates in parallel. withQueue only errors
		// on an invalid device / null callback — neither is possible here (this is the
		// device we initialised ImGui against), so the returned Error is discarded.
		(void)acm::interop::withQueue(*m->device, [](VkQueue) { ImGui_ImplVulkan_NewFrame(); });
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
	}

	void Context::render(acm::CommandBuffer cmd)
	{
		ImGui::Render();
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), acm::interop::commandBuffer(cmd));
	}

	// --- createTexture (CPU image -> drawable gui::Texture) ---------------------

	// The acm texture format an image::PixelFormat samples as. Only RGBA8 has a direct GPU
	// mapping today; a format needing conversion first (YUV/LAB -> RGBA via image::convert())
	// has no entry, so createTexture returns an invalid Texture. (This bridge — and
	// image<->texture readback — belongs in a future lain::graphics layer once non-gui
	// consumers appear; named static here for now.)
	static bool toAcmFormat(image::PixelFormat format, acm::Format& out)
	{
		switch (format)
		{
			case image::PixelFormat::RGBA8:
				out = acm::Format::R8G8B8A8_Unorm;
				return true;
			default:
				return false;
		}
	}

	Texture Context::createTexture(const lain::image::Image& img)
	{
		acm::Format format{};
		if (m->device == nullptr || !img.valid() || !toAcmFormat(img.pixelFormat(), format))
			return {};

		acm::Texture texture = m->device->createTexture(format, acm::Extent2D{static_cast<std::uint32_t>(img.width()), static_cast<std::uint32_t>(img.height())});
		if (!texture.valid())
			return {};
		texture.upload(img.data(), img.byteSize());

		VkDescriptorSet set = ImGui_ImplVulkan_AddTexture(acm::interop::imageView(texture), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		return Texture(std::move(texture), reinterpret_cast<ImTextureID>(set), img.pixelFormat());
	}
} // namespace lain::gui
