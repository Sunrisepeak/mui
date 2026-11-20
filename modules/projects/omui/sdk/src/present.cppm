// omui:present — an application drawn in another process's window
// (W16, D-154: in development the host owns the window, the input, the
// designer and its history; the application is a render target). The
// application sends each frame's display list (xrender.vector:wire) and the
// images it refers to, once per revision; the host draws them with the same
// xrender — the same pixels as the application drawing itself — and sends
// the person's input back. A font is named by what it is (its face's bytes'
// hash, weight, slant); the host says which faces it has, and a face it lacks
// (one the application loaded itself) is sent once.
//
// Over a local connection (mcpplibs.os), framed: a length, a kind, its bytes.
// A new build takes over from the one before: the host asks the old for what
// the person set (ui.state) and gives it to the new (ui.restore) before the
// new is shown. A build that fails is about places in the sources: the host
// asks the one running where the elements built there are (W16: a problem
// shown beside its element).
//
// The development host's transport (D-161's T0.5): it carries display lists
// and input between two processes, and is neither the agent endpoint nor the
// interface's subject — so it lives with the two ends that use it, the
// application's `run` and the development host (omui:devhost), not in
// omui.agent.
//
//     // the application                         // the host
//     omui::Presenter out(rt, std::move(c));     omui::Presentation in(std::move(c));
//     out.send(rt.frame(…));                     while (auto list = in.receive(wait)) draw(*list, in.images());
//     rt.frame({t, out.input()});                in.send_input(events);
export module omui:present;

import std;
import mcpplibs.os;
import omui.core;
import omui.view;
import xrender.vector;
import mlibs.text;

export namespace omui {

// The element built nearest above `line` in `file` (matched by its trailing
// components: a compiler and std::source_location may name it from different
// directories), drawn with a size; none when none was built in that file.
[[nodiscard]] NodeId built_near(const Runtime& runtime, std::string_view file, int line);

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
    std::set<std::uint32_t>                        fonts_sent_;  // font references the host knows
    std::set<std::uint64_t>                        host_faces_;  // the faces the host has, by identity
    bool                                           host_said_ = false;  // whether it has said which
    std::set<std::uint64_t>                        faces_sent_;  // the faces sent to it
    std::unordered_map<std::uint32_t, std::uint64_t> identities_;  // a face's identity, once computed
    std::string                                    pending_;
    std::vector<InputEvent>                        input_;
    std::optional<std::pair<Size, float>>          size_;
    void read_available();
};

class Presentation {
public:
    // `fonts`: where the application's font references are interned again, by
    // their faces' identities; a face it lacks is sent by the application and
    // added (its own fonts). None: their numbers taken as they are.
    explicit Presentation(mcpplibs::os::ipc::connection connection, mlibs::text::FontCollection* fonts = nullptr);
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
    // Where the elements built at these places are on the application's window
    // (a list of {file, line}, as a compiler names them), asked; the answer
    // arrives with a later receive (places()): for each, in order, the drawn
    // rect of the element built nearest above that line in that file, or null.
    bool ask_places(const Value& places);
    [[nodiscard]] const std::optional<Value>& places() const noexcept { return places_; }

private:
    mcpplibs::os::ipc::connection connection_;
    std::string                   pending_;
    xrender::DisplayList          list_;
    xrender::ImageStore           images_;
    bool                          gone_ = false;
    std::uint64_t                 frames_ = 0;
    std::optional<Value>          state_;
    std::optional<Value>          places_;
    mlibs::text::FontCollection*  fonts_ = nullptr;
    std::unordered_map<std::uint32_t, std::uint32_t> font_map_;  // the application's reference → this side's
    std::unordered_map<std::uint64_t, std::uint32_t> faces_;     // a face's identity → this side's face
};

}  // namespace omui
