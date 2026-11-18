module omui.agent;

import std;
import mlibs.compose;
import mlibs.image;
import xrender.vector;

namespace omui::agent {

namespace {

Endpoint* make(Runtime& runtime, EndpointOptions options) { return new Endpoint(runtime, std::move(options)); }
void destroy(Endpoint* endpoint) noexcept { delete endpoint; }
std::expected<void, AgentFailure> start(Endpoint& endpoint) { return endpoint.start(); }
void stop(Endpoint& endpoint) { endpoint.stop(); }
std::size_t pump(Endpoint& endpoint) { return endpoint.pump(); }
std::optional<AgentInput> take_input(Endpoint& endpoint) { return endpoint.take_input(); }
std::expected<VectorCapture, ViewFailure> capture(const Runtime& runtime, const xrender::Resources& resources, Rect region, float scale,
                                                  std::optional<Color> background) {
    return vector_capture(runtime, resources, region, scale, background);
}
std::expected<std::vector<std::uint8_t>, std::string> png(const mlibs::image::View& pixels) { return mlibs::image::encode_png(pixels); }
TaskBroker* make_tasks(Runtime& runtime) { return new TaskBroker(runtime); }
void end_tasks(TaskBroker* broker) noexcept { delete broker; }
std::string hash_of_tree(semantic::Tree& tree) { return tree_hash(tree); }
std::expected<std::string, AgentFailure> hash_of_pixels(std::span<const std::uint8_t> bytes) { return pixels_hash(bytes); }

}  // namespace

// Made when installed, not when the program starts: a table initialised at start-up would keep the endpoint and
// the protocol in a program that never installs them (mlibs.compose).
void install() {
    static const EndpointService service{"local",       &make, &destroy,   &start,        &stop,          &pump, &take_input,
                                         &capture,      &png,  &make_tasks, &end_tasks,    &hash_of_tree,  &hash_of_pixels};
    (void)mlibs::compose::process().install(endpoint_slot, service, service.name);
}

}  // namespace omui::agent
