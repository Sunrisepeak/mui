// A recording replayed (W12): what a person did in development mode — every
// frame's time and input — exported by the protocol's `record`, played in a
// fresh instance by `--replay`, ends as it did, the semantic tree and the
// pixels to the byte; a recording that lost its clicks does not.
import std;
import omui;
import mcpplibs.mtest;

using namespace omui;
using namespace mcpplibs::mtest;

namespace {

AppSpec counter(Signal<std::int64_t>& count) {
    AppSpec app;
    app.id = "org.example.replay";
    app.size = {240, 160};
    app.view = [&count] {
        return column({.padding = 12, .gap = 8}, {
                                                     box({.id = "add", .width = 80, .height = 28, .background = "primary", .label = "Add",
                                                          .on = {{"press", [&count](const Value&) { count.update([](std::int64_t n) { return n + 1; }); }}}}),
                                                     text({.id = "total", .text = derive([&count] { return std::format("{} added", count.get()); })}),
                                                 });
    };
    return app;
}

std::filesystem::path written(const Value& recording, std::string_view name) {
    const auto file = temp_dir() / name;
    std::ofstream(file, std::ios::binary) << recording.json();
    return file;
}

}  // namespace

UTest cases{"sdk.replay", [] {
    "a recording plays again in a fresh instance and ends as it did; without its clicks it does not"_test = [] {
        register_base_components();
        Signal<std::int64_t> live_count{0};
        AppSpec live_app = counter(live_count);
        Headless live(HeadlessOptions{.level = HeadlessLevel::pixels, .size = live_app.size, .app = live_app.id});
        live.runtime().set_record_level(RecordLevel::full);
        live.mount("main", live_app.view());
        for (int frame = 0; frame < 3; ++frame) (void)live.step();
        expect(live.click("main/add"));
        for (int frame = 0; frame < 5; ++frame) (void)live.step();
        expect(live.click("main/add"));
        live.settle();
        expect(eq(live_count.peek(), std::int64_t{2}));

        agent::Protocol protocol(live.runtime(), {.app = live_app.id, .capture = live.capture()});
        std::vector<Value> answer;
        // The person's own recording, shared: outside development they say yes (W12).
        protocol.request(R"({"op":"record","answer":"yes"})", [&](std::string_view line) { answer.push_back(parse_json(line).value()); });
        const bool recorded = !answer.empty() && answer.back().get("exitCode")->as_int() == 0 && answer.front().get("data");
        expect(recorded) << (answer.empty() ? std::string("none") : answer.back().json());
        if (!recorded) return;
        const Value& recording = *answer.front().get("data");
        expect(recording.get("expect")->get("tree") && recording.get("expect")->get("pixels")) << recording.get("expect")->json();

        RunOptions replay;
        replay.headless = true;
        replay.replay = written(recording, "add-twice.recording.json");
        Signal<std::int64_t> again_count{0};
        expect(eq(run(counter(again_count), replay), 0)) << "the tree and the pixels as they were";
        expect(eq(again_count.peek(), std::int64_t{2}));

        // The same recording without its input: it ends otherwise, and says so.
        Value::List bare;
        for (const Value& frame : *recording.get("frames")->list()) bare.push_back(Value{}.set("t", *frame.get("t")));
        Value lost = recording;
        lost.set("frames", Value(std::move(bare)));
        RunOptions without;
        without.headless = true;
        without.replay = written(lost, "no-clicks.recording.json");
        Signal<std::int64_t> idle_count{0};
        expect(eq(run(counter(idle_count), without), 1));
        expect(eq(idle_count.peek(), std::int64_t{0}));
    };
}};
