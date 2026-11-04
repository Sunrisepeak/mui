import std;
import hanim.effect;
import openmui.log;
import openmui.trace;
import openmui.testing;

using namespace hanim;
using namespace omui::testing;

int main(int argc, char** argv) {
    Suite s{"hanim.effect"};
    s.test("ordered recipes own endpoint metadata and sample after producer release", [] {
        PreparedEffects plan;
        {
            auto input = fx::chain({fx::blur(0, 8), fx::tone({1, 0, 0, 0}, {0, 0.5f, 1, 1}), fx::blur(10, 2)});
            const auto prepared = PreparedEffects::prepare(input); check(prepared.has_value()); if (!prepared) return;
            plan = *prepared;
            input[0].id = "mutated"; input[0].parameters[0].name = "mutated"; input[0].parameters[0].to = 999;
            input.clear(); input.shrink_to_fit();
        }
        const auto entries = plan.effects(); const auto parameters = plan.parameters();
        check(entries.size() == 3 && plan.parameter_count() == 6 && parameters.size() == 6);
        check(entries[0].id == "blur" && entries[1].id == "tone" && entries[2].id == "blur", "chain order and repeated ids are preserved");
        check(entries[0].offset == 0 && entries[0].count == 1 && entries[1].offset == 1 && entries[1].count == 4 && entries[2].offset == 5);
        check(parameters[0].name == "radius" && parameters[0].to == 8 && parameters[1].name == "color_r" && parameters[4].name == "amount");
        std::array<float, 7> output{}; output.back() = 123;
        check(plan.sample(0.5, output).has_value());
        check(output == std::array<float, 7>{4, 0.5f, 0.25f, 0.5f, 0.5f, 6, 123});
        check(plan.sample(0, output).has_value());
        check(output == std::array<float, 7>{0, 1, 0, 0, 0, 10, 123});
        check(plan.sample(1, output).has_value());
        check(output == std::array<float, 7>{8, 0, 0.5f, 1, 1, 2, 123});
        const auto shared = plan;
        check(shared.effects().data() == plan.effects().data(), "plan copies retain owned immutable data");
    });
    s.test("same progress replays independently without owning a clock or scratch", [] {
        const auto input = fx::chain({fx::blur(-20, 40), fx::tone({}, {1, 0, 0, 0})});
        const auto plan = PreparedEffects::prepare(input); check(plan.has_value()); if (!plan) return;
        std::array<float, 5> a{}, b{};
        for (double t : {0.0, 1.0, 0.25, 0.75, 0.5}) {
            check(plan->sample(t, a).has_value());
            check(plan->sample(1 - t, b).has_value());
            check(plan->sample(t, b).has_value());
            check(a == b && a[0] == hanim::lerp(-20.0f, 40.0f, t));
        }
        check(plan->sample(-10, a).has_value() && a[0] == -20);
        check(plan->sample(10, a).has_value() && a[0] == 40);
        // Generic plans represent finite endpoints; backend range validation
        // will reject this negative blur rather than silently clamp it.
        const std::array extreme{EffectSpec{"custom", {{"gain", -std::numeric_limits<float>::max(), std::numeric_limits<float>::max()}}}};
        const auto wide = PreparedEffects::prepare(extreme); check(wide.has_value());
        std::array<float, 1> value{};
        check(wide->sample(0.5, value).has_value() && value[0] == 0);
    });
    s.test("sampling rejects invalid progress and short buffers before writing", [] {
        const auto input = fx::chain({fx::tone({}, {})}); const auto plan = PreparedEffects::prepare(input); check(plan.has_value());
        std::array<float, 5> output{11, 12, 13, 14, 15}; const auto original = output;
        for (double progress : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()}) {
            check(plan->sample(progress, output).error() == EffectError::nonfinite_progress);
            check(output == original);
        }
        check(plan->sample(0.5, std::span(output).first(3)).error() == EffectError::buffer_too_small);
        check(output == original);
    });
    s.test("preparation validates ids duplicate parameter names and finite endpoints", [] {
        auto rejects = [](EffectSpec bad, EffectError error) {
            const std::array input{std::move(bad)};
            const auto result = PreparedEffects::prepare(input); check(!result && result.error() == error);
        };
        rejects({"", {}}, EffectError::invalid_id);
        rejects({std::string(129, 'x'), {}}, EffectError::invalid_id);
        rejects({std::string("bad\0key", 7), {}}, EffectError::invalid_id);
        rejects({"x", {{"", 0, 1}}}, EffectError::invalid_id);
        rejects({"x", {{"gain", 0, 1}, {"gain", 2, 3}}}, EffectError::duplicate_parameter);
        rejects({"x", {{"gain", std::numeric_limits<float>::quiet_NaN(), 1}}}, EffectError::nonfinite_value);
        rejects({"x", {{"gain", 0, std::numeric_limits<float>::infinity()}}}, EffectError::nonfinite_value);
        const std::array repeated{EffectSpec{"x", {{"gain", 0, 1}}}, EffectSpec{"x", {{"gain", 2, 3}}}};
        check(PreparedEffects::prepare(repeated).has_value(), "parameter namespaces are per chain entry");
    });
    s.test("per-plan limits and empty chains have explicit semantics", [] {
        const auto input = fx::chain({fx::blur(0, 1), fx::tone({}, {})});
        check(PreparedEffects::prepare(input, {.max_effects = 1}).error() == EffectError::capacity);
        check(PreparedEffects::prepare(input, {.max_parameters = 4}).error() == EffectError::capacity);
        check(PreparedEffects::prepare(input, {.max_effects = 2, .max_parameters = 5}).has_value());
        check(PreparedEffects::prepare({}, {.max_effects = 0}).error() == EffectError::invalid_limits);
        check(PreparedEffects::prepare({}, {.max_parameters = 65537}).error() == EffectError::invalid_limits);
        PreparedEffects empty;
        check(empty.effects().empty() && empty.parameters().empty() && empty.parameter_count() == 0);
        check(empty.sample(0.5, {}).has_value());
        check(PreparedEffects::prepare({}).has_value());
        const std::array no_controls{EffectSpec{"custom-zero-parameter", {}}};
        const auto custom = PreparedEffects::prepare(no_controls); check(custom.has_value());
        check(custom->effects().size() == 1 && custom->effects()[0].count == 0 && custom->sample(0, {}).has_value(),
            "unknown descriptions are data, not a claim that a renderer executed them");
    });
    s.test("existing independent logging and trace expose preparation and sampling", [] {
        struct Observer { int prepared = 0, refused = 0; } observer;
        omui::set_log_sink([](void* context, const omui::LogRecord& record) {
            if (record.category != "hanim.effect") return;
            auto& observer = *static_cast<Observer*>(context);
            if (record.message.starts_with("prepared")) ++observer.prepared;
            if (record.message.starts_with("prepare refused")) ++observer.refused;
        }, &observer);
        omui::trace::start();
        const auto input = fx::chain({fx::blur(0, 2)}); const auto plan = PreparedEffects::prepare(input);
        const auto failed = PreparedEffects::prepare(input, {.max_parameters = 0});
        std::array<float, 1> output{};
        bool sampled = true;
        for (int i = 0; i < 2000; ++i) sampled &= plan->sample(static_cast<double>(i % 101) / 100, output).has_value();
        const auto trace = omui::trace::stop();
        omui::set_log_sink(nullptr, nullptr);
        check(sampled && failed.error() == EffectError::invalid_limits && observer.prepared == 1 && observer.refused == 1);
        bool prepared = false, sampled_trace = false;
        for (const auto& event : trace.events) if (std::string_view(event.category) == "hanim.effect" && event.kind == omui::trace::Kind::begin) {
            prepared |= std::string_view(event.name) == "prepare";
            sampled_trace |= std::string_view(event.name) == "sample";
        }
        check(prepared && sampled_trace && trace.dropped == 0);
    });
    return s.run(argc, argv);
}
