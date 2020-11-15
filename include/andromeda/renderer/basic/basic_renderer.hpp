#pragma once

#include <andromeda/renderer/renderer.hpp>

namespace andromeda::renderer {

class BasicRenderer : public Renderer {
public:
	BasicRenderer(Context& ctx);
	virtual ph::ImageView debug_image() const;
protected:
	virtual void render_frame(Context& ctx, ph::FrameInfo& frame, ph::RenderGraph& graph);

	ph::RenderAttachment* depth;
	void create_pipeline(Context& ctx);

	struct PerFrameBuffers {
		ph::BufferSlice camera;
		ph::BufferSlice transforms;
	} per_frame_buffers;

	struct Bindings {
		ph::ShaderInfo::BindingInfo camera;
		ph::ShaderInfo::BindingInfo transforms;
	} bindings;

	void update_transforms(ph::CommandBuffer& cmd_buf, RenderDatabase& database);
	void update_camera_data(ph::CommandBuffer& cmd_buf, RenderDatabase& database);
	vk::DescriptorSet get_descriptors(ph::FrameInfo& frame, ph::CommandBuffer& cmd_buf, RenderDatabase& database);
};

}