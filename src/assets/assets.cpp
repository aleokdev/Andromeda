#include <andromeda/assets/assets.hpp>
#include <string_view>

namespace andromeda {

class Texture;
class Mesh;

namespace assets {

template<>
Handle<Texture> load(Context& ctx, std::string_view path) {
    return ctx.request_texture(path);
}

template<>
Handle<Mesh> load(Context& ctx, std::string_view path) {
    return ctx.request_mesh(path);
}

}
}