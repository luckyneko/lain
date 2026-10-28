#include "lain/render/viewport.h"

#include "state.h"

#include <lain/image/transfercurve.h>
#include <lain/string/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <utility>

namespace lain::render
{
	// What ImGui composes: sRGB-encoded values in UNORM, so the shaders write lain's curve themselves.
	static constexpr acm::Format kDisplayFormat = acm::Format::R8G8B8A8_Unorm;
	static constexpr std::uint32_t kFrames = acm::Renderer::MaxFramesInFlight;

	namespace detail
	{
		struct ViewportState
		{
			RendererState* renderer = nullptr;
			std::vector<std::string> problems;
			acm::SampleCount samples = acm::SampleCount::One;
			acm::Pipeline scene; // for kDisplayFormat targets at `samples`
			acm::Pipeline lines;
			acm::Pipeline grid; // a Grid's backdrop

			// What one frame in flight writes: only touched when acm::Renderer hands that frame's index to
			// a pre-pass, by which point the frame's last use has finished.
			struct Frame
			{
				acm::Extent2D extent;
				acm::Texture texture; // the resolved picture a pane samples
				acm::RenderTarget target;
				acm::Buffer constants; // FrameConstants
				acm::Buffer lineVertices;
				std::size_t lineCapacity = 0; // in vertices
				acm::DescriptorSet lineSet;
				acm::DescriptorSet gridSet;
				std::uint64_t sceneSerial = 0; // the upload `sets` were made for
				std::vector<acm::DescriptorSet> sets;
			};
			std::array<Frame, kFrames> frames;
			acm::Texture none; // what texture() answers for a frame out of range
		};
	} // namespace detail

	Viewport::Viewport(Renderer& renderer)
		: m(std::make_unique<detail::ViewportState>())
	{
		m->renderer = renderer.m.get();
		if (!m->renderer->problems.empty())
		{
			m->problems = m->renderer->problems;
			return;
		}
		acm::Device& device = *m->renderer->device;
		m->samples = std::min(device.maxSampleCount(), acm::SampleCount::Four,
							  [](acm::SampleCount a, acm::SampleCount b)
							  { return int(a) < int(b); });

		acm::Texture probe = device.createTexture(kDisplayFormat, acm::Extent2D{1, 1});
		acm::RenderTarget target = device.createRenderTarget(probe, acm::RenderTargetConfig{acm::RenderTargetFinish::Sampled, true, m->samples});
		if (target.valid())
		{
			m->scene = detail::scenePipeline(*m->renderer, target);
			acm::PipelineShaders shaders;
			shaders.vertex = m->renderer->lineVertex;
			shaders.fragment = m->renderer->lineFragment;
			acm::PipelineConfig config;
			config.descriptorLayout = m->renderer->lineLayout;
			config.depth = acm::DepthState::Test(); // tested but not written, so lines never hide each other
			config.topology = acm::Topology::LineList;
			m->lines = device.createPipeline(shaders, target, config);

			acm::PipelineShaders grid;
			grid.vertex = m->renderer->gridVertex;
			grid.fragment = m->renderer->gridFragment;
			acm::PipelineConfig gridConfig;
			gridConfig.descriptorLayout = m->renderer->gridLayout;
			gridConfig.depth = acm::DepthState::Test();	   // behind what is in front of the plane, over what is behind it
			gridConfig.blend = acm::BlendMode::AlphaBlend; // transparent between its lines
			m->grid = device.createPipeline(grid, target, gridConfig);
		}
		for (detail::ViewportState::Frame& frame : m->frames)
		{
			frame.constants = device.createBuffer(sizeof(detail::FrameConstants), acm::BufferUsage::Uniform);
			frame.gridSet = device.createDescriptorSet(m->renderer->gridLayout);
			if (frame.constants.valid() && frame.gridSet.valid())
				frame.gridSet.setBuffer(0, frame.constants);
		}

		for (const acm::Error& error : {target.error(), m->scene.error(), m->lines.error(), m->grid.error(), m->frames[0].constants.error(),
										m->frames[0].gridSet.error()})
		{
			if (error)
			{
				m->problems.push_back("the device refused the viewport: " + error.message());
				return;
			}
		}
		if (!m->scene.valid() || !m->lines.valid() || !m->grid.valid())
			m->problems.push_back("the device refused the viewport's pipelines");
	}

	Viewport::Viewport(Viewport&&) noexcept = default;
	Viewport& Viewport::operator=(Viewport&&) noexcept = default;
	Viewport::~Viewport() = default;

	const std::vector<std::string>& Viewport::problems() const
	{
		return m->problems;
	}

	std::vector<std::string> validate(const Grid& grid)
	{
		std::vector<std::string> problems;
		const auto finite = [](const math::Vec3d& v)
		{ return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
		if (!finite(grid.origin))
			problems.push_back("the grid's origin is not finite");
		const auto unit = [&](const math::Vec3d& v)
		{ return finite(v) && std::abs(math::length(v) - 1.0) <= 1e-9; };
		if (!unit(grid.u) || !unit(grid.v) || std::abs(math::dot(grid.u, grid.v)) > 1e-9)
			problems.push_back("the grid's axes are not unit length and at right angles");
		if (!(std::isfinite(grid.spacing) && grid.spacing > 0))
			problems.push_back("the grid's spacing is not positive and finite");
		if (!(std::isfinite(grid.extent.x) && std::isfinite(grid.extent.y) && grid.extent.x > 0 && grid.extent.y > 0))
			problems.push_back("the grid's extent is not positive and finite");
		for (const image::ColorRGBf& colour : {grid.minor, grid.major})
		{
			if (!(std::isfinite(colour.r) && std::isfinite(colour.g) && std::isfinite(colour.b) && colour.r >= 0 && colour.g >= 0 && colour.b >= 0))
			{
				problems.push_back("a grid colour is negative or not finite");
				break;
			}
		}
		return problems;
	}

	std::vector<std::string> Viewport::record(acm::CommandBuffer& cmd, std::uint32_t frame, const GpuScene& scene, const View& view,
											  const std::vector<Segment>& lines, const std::optional<Grid>& grid)
	{
		if (!m->problems.empty())
			return m->problems;
		std::vector<std::string> problems = validate(view);
		if (grid)
		{
			for (std::string& problem : validate(*grid))
				problems.push_back(std::move(problem));
		}
		if (frame >= kFrames)
			problems.push_back(string::format("frame {} is not one of the {} in flight", frame, kFrames));
		if (scene.m->owner != m->renderer)
			problems.push_back("the scene was uploaded by another renderer");
		if (!problems.empty())
			return problems;

		acm::Device& device = *m->renderer->device;
		const detail::SceneState& gpu = *scene.m;
		detail::ViewportState::Frame& f = m->frames[frame];

		const acm::Extent2D extent{view.image.width, view.image.height};
		if (!f.texture.valid() || f.extent.width != extent.width || f.extent.height != extent.height)
		{
			f.texture = device.createTexture(kDisplayFormat, extent);
			f.target = device.createRenderTarget(f.texture, acm::RenderTargetConfig{acm::RenderTargetFinish::Sampled, true, m->samples});
			f.extent = extent;
			if (!f.target.valid())
			{
				const acm::Error error = f.texture.valid() ? f.target.error() : f.texture.error();
				f.texture.reset();
				return {"the device refused the viewport's target: " + error.message()};
			}
		}

		if (f.sceneSerial != gpu.serial)
		{
			f.sets.clear();
			for (std::size_t i = 0; i < gpu.materials.size(); ++i)
			{
				acm::DescriptorSet set = detail::sceneSet(*m->renderer, gpu, i, f.constants);
				if (!set.valid())
					return {string::format("the device refused material {}'s descriptors: {}", i, set.error().message())};
				f.sets.push_back(std::move(set));
			}
			f.sceneSerial = gpu.serial;
		}

		const detail::FrameConstants constants = detail::frameConstants(gpu, view, true, grid ? &*grid : nullptr);
		if (const acm::Error error = f.constants.write(&constants, sizeof(constants)))
			return {"the device refused the view's constants: " + error.message()};

		const std::size_t vertices = 2 * lines.size();
		if (vertices > f.lineCapacity)
		{
			const std::size_t capacity = std::max<std::size_t>(vertices, 2 * f.lineCapacity);
			f.lineVertices = device.createBuffer(capacity * sizeof(detail::LineVertex), acm::BufferUsage::Storage);
			f.lineSet = device.createDescriptorSet(m->renderer->lineLayout);
			if (!f.lineVertices.valid() || !f.lineSet.valid())
			{
				const acm::Error error = f.lineVertices.valid() ? f.lineSet.error() : f.lineVertices.error();
				f.lineCapacity = 0;
				return {"the device refused the lines: " + error.message()};
			}
			f.lineSet.setBuffer(0, f.constants);
			f.lineSet.setBuffer(1, f.lineVertices);
			f.lineCapacity = capacity;
		}
		if (vertices > 0)
		{
			std::vector<detail::LineVertex> packed;
			packed.reserve(vertices);
			for (const Segment& segment : lines)
			{
				const float colour[4] = {segment.colour.r, segment.colour.g, segment.colour.b, 1.0f};
				for (const math::Vec3d& end : {segment.from, segment.to})
					packed.push_back({{float(end.x), float(end.y), float(end.z), 1.0f}, {colour[0], colour[1], colour[2], colour[3]}});
			}
			if (const acm::Error error = f.lineVertices.write(packed.data(), packed.size() * sizeof(detail::LineVertex)))
				return {"the device refused the lines: " + error.message()};
		}

		// The clear colour is written as given, so it is encoded here as the shaders encode.
		const image::TransferCurve srgb = image::transferCurve(image::ColorSpace::sRGB);
		if (acm::Error error = cmd.beginRendering(f.target, srgb.fromLinear(std::clamp(gpu.background.r, 0.0f, 1.0f)),
												  srgb.fromLinear(std::clamp(gpu.background.g, 0.0f, 1.0f)),
												  srgb.fromLinear(std::clamp(gpu.background.b, 0.0f, 1.0f)), 1.0f))
			return {"the device refused the viewport's drawing: " + error.message()};
		cmd.setViewportAndScissor(extent);
		acm::Error error = detail::recordScene(cmd, m->scene, gpu, f.sets);
		// The grid's backdrop after the scene, so the depth it is tested against is the scene's, and
		// before the lines, which are drawn over everything that is not in front of them.
		if (!error && grid)
		{
			error = cmd.bindPipeline(m->grid);
			if (!error)
			{
				cmd.bindDescriptorSet(m->grid, f.gridSet);
				error = cmd.draw(6);
			}
		}
		if (!error && vertices > 0)
		{
			error = cmd.bindPipeline(m->lines);
			if (!error)
			{
				cmd.bindDescriptorSet(m->lines, f.lineSet);
				error = cmd.draw(std::uint32_t(vertices));
			}
		}
		cmd.endRendering();
		if (error)
			return {"the device refused the viewport's drawing: " + error.message()};
		return {};
	}

	const acm::Texture& Viewport::texture(std::uint32_t frame) const
	{
		return frame < kFrames ? m->frames[frame].texture : m->none;
	}

	image::Image Viewport::read(std::uint32_t frame)
	{
		if (frame >= kFrames || !m->frames[frame].texture.valid())
			return {};
		acm::Device& device = *m->renderer->device;
		const acm::Texture& texture = m->frames[frame].texture;
		const acm::Extent2D extent = texture.extent();
		const std::size_t bytes = std::size_t{extent.width} * extent.height * 4;
		acm::Buffer readback = device.createBuffer(bytes, acm::BufferUsage::TransferDst);
		if (!readback.valid())
			return {};
		const acm::Error submitted = device.submitSync(
			[&](acm::CommandBuffer& cmd)
			{
				cmd.transitionImage(texture, acm::ImageLayout::ShaderReadOnly, acm::ImageLayout::TransferSrc);
				cmd.copyTextureToBuffer(texture, readback);
				cmd.transitionImage(texture, acm::ImageLayout::TransferSrc, acm::ImageLayout::ShaderReadOnly);
			});
		if (submitted)
			return {};
		image::Image picture(int(extent.width), int(extent.height), image::PixelFormat::RGBA8, image::ColorSpace::sRGB,
							 image::AlphaMode::Straight);
		const void* mapped = readback.map();
		if (!mapped)
			return {};
		std::memcpy(picture.data(), mapped, bytes);
		readback.unmap();
		return picture;
	}
} // namespace lain::render
