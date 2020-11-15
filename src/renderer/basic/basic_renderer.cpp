#include <andromeda/renderer/basic/basic_renderer.hpp>
#include <andromeda/core/context.hpp>

#include <phobos/present/present_manager.hpp>
#include <stl/literals.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <andromeda/renderer/util.hpp>
#include <andromeda/assets/assets.hpp>

namespace andromeda::renderer {

BasicRenderer::BasicRenderer(Context& ctx) : Renderer(ctx) {
	depth = &vk_present->add_depth_attachment("depth", { 1920, 1080 });
	attachments.push_back(depth);
	create_pipeline(ctx);
}

ph::ImageView BasicRenderer::debug_image() const {
	return color_final->image_view();
}

void BasicRenderer::render_frame(Context& ctx, ph::FrameInfo& frame, ph::RenderGraph& graph) {
	per_frame_buffers = {};
	ph::RenderPass pass;
#if ANDROMEDA_DEBUG
	pass.debug_name = "basic_main_pass";
#endif
	pass.outputs = { *color_final, *depth };
	pass.clear_values.emplace_back();
	pass.clear_values[0].color = vk::ClearColorValue{ std::array<float, 4>{ {0, 0, 0, 1}} };
	pass.clear_values.emplace_back();
	pass.clear_values[1].depthStencil = vk::ClearDepthStencilValue{ 1.0, 0 };
	pass.callback = [this, &frame, &ctx](ph::CommandBuffer& cmd_buf) {
		auto_viewport_scissor(cmd_buf);
		// Note that we must set the viewport and scissor state before early returning, this is because we specified them as dynamic states.
		// This requires us to have commands to update them.
		if (database.draws.empty()) return;
		ph::Pipeline pipeline = cmd_buf.get_pipeline("basic_pipeline");
		cmd_buf.bind_pipeline(pipeline);

		update_transforms(cmd_buf, database);
		update_camera_data(cmd_buf, database);

		// Bind descriptor set
		vk::DescriptorSet descr_set = get_descriptors(frame, cmd_buf, database);
		cmd_buf.bind_descriptor_set(0, descr_set);

		for (uint32_t draw_idx = 0; draw_idx < database.draws.size(); ++draw_idx) {
			auto const& draw = database.draws[draw_idx];

			// Don't draw if the mesh isn't ready
			if (!assets::is_ready(draw.mesh)) {
				continue;
			}

			Mesh* mesh = assets::get(draw.mesh);
			// Bind draw data
			cmd_buf.bind_vertex_buffer(0, ph::whole_buffer_slice(*ctx.vulkan, mesh->get_vertices()));
			cmd_buf.bind_index_buffer(ph::whole_buffer_slice(*ctx.vulkan, mesh->get_indices()));

			// update push constant ranges
			stl::uint32_t const transform_index = draw_idx;
			cmd_buf.push_constants(vk::ShaderStageFlagBits::eVertex, 0, sizeof(uint32_t), &transform_index);
			// Execute drawcall
			cmd_buf.draw_indexed(mesh->index_count(), 1, 0, 0, 0);
		}
	};
	graph.add_pass(std::move(pass));
}


void BasicRenderer::update_transforms(ph::CommandBuffer& cmd_buf, RenderDatabase& database) {
	vk::DeviceSize const size = database.transforms.size() * sizeof(glm::mat4);
	per_frame_buffers.transforms = cmd_buf.allocate_scratch_ssbo(size);
	std::memcpy(per_frame_buffers.transforms.data, database.transforms.data(), size);
}

void BasicRenderer::update_camera_data(ph::CommandBuffer& cmd_buf, RenderDatabase& database) {
	// The deferred main pass needs a mat4 for the projection_view and a vec3 padded to vec4 for the camera position
	vk::DeviceSize const size = sizeof(glm::mat4) + sizeof(glm::vec4);
	per_frame_buffers.camera = cmd_buf.allocate_scratch_ubo(size);

	std::memcpy(per_frame_buffers.camera.data, glm::value_ptr(database.projection_view), sizeof(glm::mat4));
//	std::memcpy(per_frame_buffers.camera.data + sizeof(glm::mat4), glm::value_ptr(database.camera_position), sizeof(glm::vec3));
}

vk::DescriptorSet BasicRenderer::get_descriptors(ph::FrameInfo& frame, ph::CommandBuffer& cmd_buf, RenderDatabase& database) {
	ph::DescriptorSetBinding set;
	set.add(ph::make_descriptor(bindings.camera, per_frame_buffers.camera));
	set.add(ph::make_descriptor(bindings.transforms, per_frame_buffers.transforms));
	return cmd_buf.get_descriptor(set);
}


void BasicRenderer::create_pipeline(Context& ctx) {
	using namespace stl::literals;

	ph::PipelineCreateInfo pci;
#if ANDROMEDA_DEBUG
	pci.debug_name = "basic_pipeline";
#endif
	pci.blend_logic_op_enable = false;
	vk::PipelineColorBlendAttachmentState blend{};
	blend.blendEnable = false;
	blend.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
	pci.blend_attachments.push_back(blend);

	pci.dynamic_states.push_back(vk::DynamicState::eViewport);
	pci.dynamic_states.push_back(vk::DynamicState::eScissor);
	pci.viewports.emplace_back();
	pci.scissors.emplace_back();

	// Pos + Normal + Tangent + TexCoords. 
	constexpr size_t stride = (3 + 3 + 3 + 2) * sizeof(float);
	pci.vertex_input_bindings.push_back(vk::VertexInputBindingDescription(0, stride, vk::VertexInputRate::eVertex));
	// vec3 iPos;
	pci.vertex_attributes.emplace_back(0_u32, 0_u32, vk::Format::eR32G32B32Sfloat, 0_u32);
	// vec3 iNormal
	pci.vertex_attributes.emplace_back(1_u32, 0_u32, vk::Format::eR32G32B32Sfloat, 3 * (uint32_t)sizeof(float));
	// vec3 iTangent;
	pci.vertex_attributes.emplace_back(2_u32, 0_u32, vk::Format::eR32G32B32Sfloat, 6 * (uint32_t)sizeof(float));
	// vec2 iTexCoords
	pci.vertex_attributes.emplace_back(3_u32, 0_u32, vk::Format::eR32G32Sfloat, 9 * (uint32_t)sizeof(float));

	std::vector<uint32_t> vert_code = ph::load_shader_code("data/shaders/basic.vert.spv");
	std::vector<uint32_t> frag_code = ph::load_shader_code("data/shaders/basic.frag.spv");
	ph::ShaderHandle vertex_shader = ph::create_shader(*ctx.vulkan, vert_code, "main", vk::ShaderStageFlagBits::eVertex);
	ph::ShaderHandle fragment_shader = ph::create_shader(*ctx.vulkan, frag_code, "main", vk::ShaderStageFlagBits::eFragment);
	pci.shaders.push_back(vertex_shader);
	pci.shaders.push_back(fragment_shader);

	pci.depth_stencil.depthTestEnable = true;
	pci.depth_stencil.depthWriteEnable = true;
	pci.depth_stencil.depthCompareOp = vk::CompareOp::eLess;
	ph::reflect_shaders(*ctx.vulkan, pci);
	bindings.camera = pci.shader_info["camera"];
	bindings.transforms = pci.shader_info["transforms"];

	ctx.vulkan->pipelines.create_named_pipeline("basic_pipeline", std::move(pci));
}


}