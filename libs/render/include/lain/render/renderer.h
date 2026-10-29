#pragma once

#include "lain/render/capture.h"
#include "lain/render/scene.h"
#include "lain/render/view.h"

#include <lain/image/image.h>

#include <archimedes/acmDevice.h>

#include <cstddef>
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
		struct RayTableState;
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

	// An update's outcome: how many meshes and textures it uploaded, or why the scene was left as it was.
	struct UpdateResult
	{
		std::vector<std::string> problems; // empty when the scene was updated
		std::size_t meshesUploaded = 0;
		std::size_t texturesUploaded = 0;
	};

	// A ray table's copy on the device: its pixel-corner rays and its pixels' sums, in bands, and the
	// texture each tile of the intermediate image is drawn into. Move-only, and written by every
	// capture through it, so it serves one capture at a time. Uploaded once per camera model, it serves
	// every frame. The device it was uploaded to must outlive it.
	class GpuRayTable
	{
	public:
		GpuRayTable(GpuRayTable&&) noexcept;
		GpuRayTable& operator=(GpuRayTable&&) noexcept;
		~GpuRayTable();

	private:
		friend class Renderer;
		explicit GpuRayTable(std::unique_ptr<detail::RayTableState> state);
		std::unique_ptr<detail::RayTableState> m;
	};

	// A ray table upload's outcome: the table on the device, or why not.
	struct GpuRayTableResult
	{
		std::optional<GpuRayTable> table;
		std::vector<std::string> problems; // empty exactly when there is a table
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

	// A texture as a device stores it: 8-bit sRGB RGBA, which the device decodes to linear light before
	// it filters. An upload converts any other texture to this on the thread that uploads, and that is
	// nearly all an upload costs: an 8192 x 8192 linear texture took 0.75 s on Metal, 0.03 s once
	// stored. A texture already stored goes up as it is, uncopied. So a host that uploads on its frame
	// loop converts its textures elsewhere first, through this, and the device holds the same texels
	// either way. Invalid when `texture` states no colour space.
	image::Image storedTexture(const image::Image& texture);

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

		// Makes `scene` the copy of `next`, uploading only the meshes and textures it does not already hold:
		// one `next` names by the same pointer as `scene` does is shared, and is never compared by its
		// contents, since an asset is immutable while a shared_ptr<const> names it. Everything else (the
		// materials, the instances, the lights) is written afresh, so a viewport rebuilds its descriptor
		// sets for it as for a new upload. What a frame changes, a mover's pose or a mesh clip's frame,
		// costs only itself. Validates `next` (validate(scene)); on a refusal `scene` is left as it was.
		// `scene` must have been uploaded by this renderer.
		UpdateResult update(GpuScene& scene, const Scene& next);

		// `scene` seen through `view`'s pinhole. `scene` must have been uploaded by this renderer.
		RenderResult render(GpuScene& scene, const View& view);

		// Copies `table` to the device, for any number of captures through its camera model.
		GpuRayTableResult upload(const RayTable& table);

		// What the camera `table` was built for records of `scene` from `view` (a capture render,
		// ADR-0028): each tile of the intermediate image drawn and waited for in turn, then remapped,
		// each pixel the mean of the table's s x s rays. The same contract as render(): RGBA32F linear
		// light at the camera's image size, with alpha as the share of the pixel's rays a surface
		// covers. Both `scene` and `table` must have been uploaded by this renderer.
		RenderResult capture(GpuScene& scene, GpuRayTable& table, const CaptureView& view);

	private:
		friend class Viewport;
		std::unique_ptr<detail::RendererState> m;
	};
} // namespace lain::render
