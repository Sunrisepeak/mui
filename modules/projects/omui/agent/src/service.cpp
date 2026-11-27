module omui.agent;

import std;
import mlibs.compose;
import mlibs.image;
import mrender.vector;

namespace omui::agent {

namespace {

Endpoint* make(Runtime& runtime, EndpointOptions options) { return new Endpoint(runtime, std::move(options)); }
void destroy(Endpoint* endpoint) noexcept { delete endpoint; }
std::expected<void, AgentFailure> start(Endpoint& endpoint) { return endpoint.start(); }
void stop(Endpoint& endpoint) { endpoint.stop(); }
std::size_t pump(Endpoint& endpoint) { return endpoint.pump(); }
std::optional<AgentInput> take_input(Endpoint& endpoint) { return endpoint.take_input(); }
std::expected<VectorCapture, ViewFailure> capture(const Runtime& runtime, const mrender::Resources& resources, Rect region, float scale,
                                                  std::optional<Color> background) {
    return vector_capture(runtime, resources, region, scale, background);
}
std::expected<std::vector<std::uint8_t>, std::string> png(const mlibs::image::View& pixels) { return mlibs::image::encode_png(pixels); }
std::expected<mlibs::image::Image, std::string> decode(std::span<const std::uint8_t> bytes) { return mlibs::image::decode_png(bytes); }
TaskBroker* make_tasks(Runtime& runtime) { return new TaskBroker(runtime); }
void end_tasks(TaskBroker* broker) noexcept { delete broker; }
std::string hash_of_tree(semantic::Tree& tree) { return tree_hash(tree); }
std::expected<std::string, AgentFailure> hash_of_pixels(std::span<const std::uint8_t> bytes) { return pixels_hash(bytes); }

}  // namespace

// Made when installed, not when the program starts: a table initialised at start-up would keep the endpoint and
// the protocol in a program that never installs them (mlibs.compose).
void install_endpoint() {
    static const EndpointService service{.name = "local",
                                         .make = &make,
                                         .destroy = &destroy,
                                         .start = &start,
                                         .stop = &stop,
                                         .pump = &pump,
                                         .take_input = &take_input,
                                         .tasks = &make_tasks,
                                         .end_tasks = &end_tasks,
                                         .tree_hash = &hash_of_tree};
    mlibs::compose::Composition& composition = mlibs::compose::process();
    if (composition.get(endpoint_slot) == &service) return;
    (void)composition.install(endpoint_slot, service, service.name);
}

void install() {
    install_endpoint();
    static const CaptureService service{.name = "png", .vector_capture = &capture, .png = &png, .decode = &decode, .pixels_hash = &hash_of_pixels};
    mlibs::compose::Composition& composition = mlibs::compose::process();
    if (composition.get(captures_slot) == &service) return;
    (void)composition.install(captures_slot, service, service.name);
}

namespace {
std::atomic<const CaptureService*> found_captures{nullptr};
}  // namespace

const CaptureService* captures() noexcept {
    if (const CaptureService* found = found_captures.load(std::memory_order_acquire)) return found;
    const CaptureService* found = mlibs::compose::process().get(captures_slot);
    if (found) found_captures.store(found, std::memory_order_release);
    return found;
}

}  // namespace omui::agent
