#pragma once

#include "lain/render/scene.h"
#include "lain/render/view.h"

#include <lain/image/image.h>

#include <archimedes/acmDevice.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lain::render
{
	namespace detail
	{
		struct RendererState;
		struct SceneState;
	} // namespace detail

	// A scene's copy on the device: its meshes, textures and materials uploaded once, so it can be
	// drawn from any number of views. Move-only. Renderer::render writes the scene's own per-view
	// constants, so it draws one view of a scene at a time; a Viewport keeps its own. The device it was
	// uploaded to must outlive it.
	class GpuScene
	{
	public:
		GpuScene(GpuScene&&) noexcept;
		GpuScene& operator=(GpuScene&&) noexcept;
		~GpuScene();

	private:
		friend class Renderer;
		friend class Viewport;
		explicit GpuScene(std::unique_ptr<detail::SceneState> state);
		std::unique_ptr<detail::SceneState> m;
	};

	// An upload's outcome: the scene on the device, or why not.
	struct UploadResult
	{
		std::optional<GpuScene> scene;
		std::vector<std::string> problems; // empty exactly when there is a scene
	};

	// A drawing's outcome: the picture, or why not.
	struct RenderResult
	{
		// RGBA32F, ColorSpace::Linear: the light each pixel's centre receives from the surface it sees,
		// or the scene's background. Alpha is coverage, 1 where a surface was drawn and 0 where none was.
		// Invalid when there are problems.
		image::Image image;
		std::vector<std::string> problems;
	};

	// The raster renderer (ADR-0028) on one device: its pipelines are built once, then any number of
	// scenes is uploaded and drawn. The device must outlive it and every scene it uploads. render() is
	// synchronous: it waits for the device and reads the picture back. A Viewport draws on its behalf
	// into a window's frame instead (viewport.h).
	class Renderer
	{
	public:
		explicit Renderer(acm::Device& device);
		Renderer(Renderer&&) noexcept;
		Renderer& operator=(Renderer&&) noexcept;
		~Renderer();

		// Why this renderer cannot draw (the device is not usable, or refused a pipeline), or empty
		// when it can.
		const std::vector<std::string>& problems() const;

		// Validates `scene` (validate(scene)) and copies it to the device.
		UploadResult upload(const Scene& scene);

		// `scene` seen through `view`'s pinhole. `scene` must have been uploaded by this renderer.
		RenderResult render(GpuScene& scene, const View& view);

	private:
		friend class Viewport;
		std::unique_ptr<detail::RendererState> m;
	};
} // namespace lain::render
