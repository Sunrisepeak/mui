// omui.agent:service — the slot an application's agent surface is installed at (mlibs.compose, D-238): the
// endpoint agents find the application at and talk to it through, and the captures they observe it by. A host
// reaches all of it through this table, so a program whose root does not install it (a `lean` world) names none
// of the endpoint, the protocol or the captures, and the linker leaves them out; such a program serves no agent,
// as a platform without local endpoints does not. The provider is this package's own (`install`): there is no
// dependency to keep apart, so no package of its own — only a program's root calls it.
export module omui.agent:service;

import std;
import mlibs.compose;
import mlibs.image;
import mrender.vector;
import omui.core;
import omui.view;
import :protocol;
import :endpoint;
import :broker;
import omui.semantic;

export namespace omui::agent {

// Only what a host calls.
struct EndpointService {
    std::string_view name;  // "local"
    // An endpoint for the runtime, and its end.
    Endpoint* (*make)(Runtime& runtime, EndpointOptions options) = nullptr;
    void (*destroy)(Endpoint* endpoint) noexcept = nullptr;
    std::expected<void, AgentFailure> (*start)(Endpoint& endpoint) = nullptr;
    void (*stop)(Endpoint& endpoint) = nullptr;
    // Serves what arrived, once a frame; the next frame's input from the connections.
    std::size_t (*pump)(Endpoint& endpoint) = nullptr;
    std::optional<AgentInput> (*take_input)(Endpoint& endpoint) = nullptr;
    // An application's background tasks (task.*), and their end.
    TaskBroker* (*tasks)(Runtime& runtime) = nullptr;
    void (*end_tasks)(TaskBroker* tasks) noexcept = nullptr;
    // What a replay must end as: the semantic tree's hash (a PNG's pixels' is the captures').
    std::string (*tree_hash)(semantic::Tree& tree) = nullptr;
};

inline constexpr mlibs::compose::Slot<EndpointService> endpoint_slot{
    {.id = "omui.agent.endpoint",
     .summary = "the application's agent surface: the endpoint agents talk to it through",
     .absent = "build with omui.sdk's feature `agent` (worlds `app` and `full` have it), or call omui::agent::install() before a host is made"}};

// The captures agents observe drawn pixels by, at a slot of their own (terminal design §9 step ③): the display list
// as SVG over a region (its islands drawn by the CPU renderer), pixels as PNG and back, a PNG's pixels' hash. A host
// that draws pixels reads them here; a program that draws none (a terminal) serves its endpoint without them and
// carries no PNG coder, SVG writer or renderer — asked for a picture, it says it has none.
struct CaptureService {
    std::string_view name;  // "png"
    std::expected<VectorCapture, ViewFailure> (*vector_capture)(const Runtime& runtime, const mrender::Resources& resources, Rect region, float scale,
                                                                std::optional<Color> background) = nullptr;
    std::expected<std::vector<std::uint8_t>, std::string> (*png)(const mlibs::image::View& pixels) = nullptr;
    std::expected<mlibs::image::Image, std::string> (*decode)(std::span<const std::uint8_t> png) = nullptr;
    std::expected<std::string, AgentFailure> (*pixels_hash)(std::span<const std::uint8_t> png) = nullptr;
};

inline constexpr mlibs::compose::Slot<CaptureService> captures_slot{
    {.id = "omui.agent.captures",
     .summary = "the captures agents observe drawn pixels by: SVG over a region, PNG, a picture's hash",
     .absent = "build with omui.sdk's feature `agent` (worlds `app` and `full` have it), or call omui::agent::install() (the endpoint and the "
               "captures) before a host is made; a program that draws no pixels (a terminal) has none"}};

// Installs this package's endpoint and captures in the process's composition. A program's root calls it (omui.sdk
// does, by its feature `agent`); nothing of this package does.
void install();
// The endpoint alone, for a program that draws no pixels (a terminal's): nothing of the captures is in it.
void install_endpoint();
// The captures installed at `captures_slot`, or null: absent. Kept once found.
[[nodiscard]] const CaptureService* captures() noexcept;

// An endpoint held through the slot's table: made, used and ended by the provider that made it. Empty where the
// program installs none, or the options ask for none.
class Served {
public:
    Served() = default;
    Served(const EndpointService& service, Runtime& runtime, EndpointOptions options)
        : service_(&service), endpoint_(service.make(runtime, std::move(options))) {}
    ~Served() { reset(); }
    Served(const Served&) = delete;
    Served& operator=(const Served&) = delete;
    Served(Served&& other) noexcept : service_(other.service_), endpoint_(std::exchange(other.endpoint_, nullptr)) {}
    Served& operator=(Served&& other) noexcept {
        if (this != &other) {
            reset();
            service_ = other.service_;
            endpoint_ = std::exchange(other.endpoint_, nullptr);
        }
        return *this;
    }

    explicit operator bool() const noexcept { return endpoint_ != nullptr; }
    [[nodiscard]] Endpoint* get() const noexcept { return endpoint_; }
    void reset() noexcept {
        if (endpoint_) service_->destroy(endpoint_);
        endpoint_ = nullptr;
    }
    std::expected<void, AgentFailure> start() { return service_->start(*endpoint_); }
    void stop() { service_->stop(*endpoint_); }
    std::size_t pump() { return service_->pump(*endpoint_); }
    [[nodiscard]] std::optional<AgentInput> take_input() { return service_->take_input(*endpoint_); }

private:
    const EndpointService* service_ = nullptr;
    Endpoint*              endpoint_ = nullptr;
};

}  // namespace omui::agent
