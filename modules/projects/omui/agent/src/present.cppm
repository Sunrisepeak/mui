// omui.agent:present — an application drawn in another process's window
// (W16, D-154: in development the host owns the window, the input, the
// designer and its history; the application is a render target). The
// application sends each frame's display list (xrender.vector:wire) and the
// images it refers to, once per revision; the host draws them with the same
// xrender — the same pixels as the application drawing itself — and sends
// the person's input back. Fonts and effects are the same on both sides.
//
// Over a local connection (mcpplibs.os), framed: a length, a kind, its bytes.
// A new build takes over from the one before: the host asks the old for what
// the person set (ui.state) and gives it to the new (ui.restore) before the
// new is shown.
//
//     // the application                         // the host
//     agent::Presenter out(rt, std::move(c));    agent::Presentation in(std::move(c));
//     out.send(rt.frame(…));                     while (auto list = in.receive(wait)) draw(*list, in.images());
//     rt.frame({t, out.input()});                in.send_input(events);
export module omui.agent:present;

import std;
import mcpplibs.os;
import omui.core;
import omui.view;
import xrender.vector;

export namespace omui::agent {

class Presenter {
public:
    Presenter(Runtime& runtime, mcpplibs::os::ipc::connection connection);
    // The frame's list, and the images it refers to that the host has not
    // got at their revision; false when the host is gone.
    bool send(const FrameOutput& frame);
    // What the host's person did since the last call (input for the next frame).
    [[nodiscard]] std::vector<InputEvent> input();
    // The window's size the host asked for, when it changed.
    [[nodiscard]] std::optional<std::pair<Size, float>> resized();

private:
    Runtime&                                       runtime_;
    mcpplibs::os::ipc::connection                  connection_;
    std::unordered_map<std::uint32_t, std::uint64_t> sent_;  // image id → revision the host has
    std::string                                    pending_;
    std::vector<InputEvent>                        input_;
    std::optional<std::pair<Size, float>>          size_;
    void read_available();
};

class Presentation {
public:
    explicit Presentation(mcpplibs::os::ipc::connection connection);
    // The next frame's list (the images it refers to stored first); none when
    // nothing came within `wait`, or the application is gone (gone()).
    [[nodiscard]] const xrender::DisplayList* receive(std::chrono::milliseconds wait);
    [[nodiscard]] const xrender::DisplayList& list() const noexcept { return list_; }
    [[nodiscard]] xrender::ImageStore& images() noexcept { return images_; }
    [[nodiscard]] bool gone() const noexcept { return gone_; }
    [[nodiscard]] std::uint64_t frames() const noexcept { return frames_; }
    bool send_input(std::span<const InputEvent> events);
    bool resize(Size size, float scale);
    // What the person set in the application (its ui.state), asked for; the
    // answer arrives with a later receive (state()).
    bool ask_state();
    [[nodiscard]] const std::optional<Value>& state() const noexcept { return state_; }
    // ui.state's values given to the application (ui.restore), before its next frame.
    bool restore(const Value& state);

private:
    mcpplibs::os::ipc::connection connection_;
    std::string                   pending_;
    xrender::DisplayList          list_;
    xrender::ImageStore           images_;
    bool                          gone_ = false;
    std::uint64_t                 frames_ = 0;
    std::optional<Value>          state_;
};

}  // namespace omui::agent
