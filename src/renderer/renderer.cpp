#include <andromeda/renderer/renderer.hpp>

#include <phobos/renderer/renderer.hpp>
#include <phobos/present/present_manager.hpp>
#include <phobos/core/vulkan_context.hpp>
#include <phobos/pipeline/shader_info.hpp>
#include <stl/literals.hpp>

#include <andromeda/util/version.hpp>
#include <andromeda/util/log.hpp>

#include <andromeda/assets/assets.hpp>
#include <andromeda/assets/texture.hpp>
#include <andromeda/world/world.hpp>

#include <andromeda/components/static_mesh.hpp>
#include <andromeda/components/camera.hpp>
#include <andromeda/components/point_light.hpp>
#include <andromeda/components/transform.hpp>
#include <andromeda/components/mesh_renderer.hpp>

#include <andromeda/util/math.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <imgui/imgui_impl_phobos.h>

namespace andromeda::renderer {

Renderer::Renderer(Context& ctx) {
	using namespace stl::literals;

	vk_present = std::make_unique<ph::PresentManager>(*ctx.vulkan);
	vk_renderer = std::make_unique<ph::Renderer>(*ctx.vulkan);

	geometry_pass = std::make_unique<GeometryPass>(ctx, *vk_present);
	lighting_pass = std::make_unique<LightingPass>(ctx, *vk_present);
	skybox_pass = std::make_unique<SkyboxPass>(ctx, *vk_present);
	tonemap_pass = std::make_unique<TonemapPass>(ctx);

	scene_color = &vk_present->add_color_attachment("scene_color", { 1280, 720 }, vk::Format::eR16G16B16A16Sfloat);
	scene_color_tonemapped = &vk_present->add_color_attachment("scene_color_tonemapped", { 1280, 720 });
}

Renderer::~Renderer() {
	vk_renderer->destroy();
	vk_present->destroy();
}


void Renderer::render(Context& ctx) {
	using namespace components;

	vk_present->wait_for_available_frame();

	ph::FrameInfo& frame = vk_present->get_frame_info();
	ph::RenderGraph graph{ ctx.vulkan.get(), &ctx.vulkan->thread_contexts[0] };

	// Reset render database, we will fill this again each frame
	database.reset();

	// Fill render database

	for (auto const& [trans, cam] : ctx.world->ecs().view<Transform, Camera>()) {
		database.projection = glm::perspective(cam.fov, (float)scene_color->get_width() / (float)scene_color->get_height(), 0.1f, 100.0f);
		database.projection[1][1] *= -1; // Vulkan has upside down projection
		database.view = glm::lookAt(trans.position, trans.position + cam.front, cam.up);
		database.projection_view = database.projection * database.view;
		database.camera_position = trans.position;
		database.environment_map = cam.env_map;
		break;
	}

	// Only populate render database with draws if there is an environment map ready to use (TODO: default envmap)
	if (database.environment_map.id != Handle<EnvMap>::none && assets::is_ready(database.environment_map)) {
		// For now, we populate the render database with all loaded materials. Later, we can improve this to only include used materials
		for (auto const& [id, _] : assets::storage::data<Material>) {
			database.add_material(Handle<Material>{ id });
		}

		for (auto const& [transform, rend, mesh] : ctx.world->ecs().view<Transform, MeshRenderer, StaticMesh>()) {
			// Calculate model matrix from transformation
			glm::mat4 model = glm::translate(glm::mat4(1.0), transform.position);
			model = glm::rotate(model, { glm::radians(transform.rotation.x),
									glm::radians(transform.rotation.y),
									glm::radians(transform.rotation.z) });
			model = glm::scale(model, transform.scale);
			database.add_draw(renderer::Draw{ .mesh = mesh.mesh, .material = rend.material, .transform = model });
		}

		for (auto const& [trans, light] : ctx.world->ecs().view<Transform, PointLight>()) {
			database.add_point_light(trans.position, light.radius, light.color, light.intensity);
		}
	}

	geometry_pass->build(ctx, frame, graph, database);
	lighting_pass->build(ctx, {
			.output = *scene_color,
			.depth = geometry_pass->get_depth(),
			.albedo_ao = geometry_pass->get_albedo_ao(),
			.metallic_roughness = geometry_pass->get_metallic_roughness(),
			.normal = geometry_pass->get_normal()
		}, frame, graph, database);
	skybox_pass->build(ctx, {
			.output = *scene_color,
			.depth = lighting_pass->get_resolved_depth()
		}, frame, graph, database);
	tonemap_pass->build(ctx, {
			.input_hdr = *scene_color,
			.output_ldr = *scene_color_tonemapped
		}, frame, graph, database);

	ImGui::Render();
	ImGui_ImplPhobos_RenderDrawData(ImGui::GetDrawData(), &frame, &graph, vk_renderer.get());

	graph.build();

	frame.render_graph = &graph;
	vk_renderer->render_frame(frame);
	vk_present->present_frame(frame);
}

}