// Nested effect-bearing Morph draws must never share their mutable scratch.
import std;
import hanim.core;
import mlibs.geom;
import xrender.vector;
import hanim.visual;
import mlibs.testing;
import mcpplibs.mtest;

using namespace hanim;
using namespace mlibs::testing;
using namespace mcpplibs::mtest;
namespace mo = hanim::visual;
namespace { std::atomic<std::size_t> allocations{0}; }
void* operator new(std::size_t size) {
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (auto* p = std::malloc(size ? size : 1)) return p;
    std::abort();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {
constexpr std::size_t fills_per_level = 40;

mo::Resolver names() {
    return {.keys = [](std::uint32_t marker, std::string& key, std::string&) {
                key = std::format("node/{}", marker);
            }, .nested = [](std::uint32_t marker) { return marker == 2; }};
}

xrender::DisplayList frame(float offset, std::size_t level) {
    xrender::DisplayList list;
    list.begin_node(1);
    for (std::size_t i = 0; i < fills_per_level; ++i)
        list.fill_rect({offset + static_cast<float>(i), static_cast<float>(level * 4), 2, 3},
                       mlibs::geom::Color::hex(0x527da8));
    list.begin_node(2);
    list.fill_rect({0, 0, 1, 1}, mlibs::geom::Color::black());
    list.end_node();
    list.end_node();
    return list;
}

struct Chain {
    std::vector<std::unique_ptr<mo::Morph>> motions;
    bool reuse_same = false;
    explicit Chain(std::size_t levels, bool effects, bool reuse = false) : reuse_same(reuse) {
        for (std::size_t level = 0; level < levels; ++level) {
            mo::Options options;
            options.curve.kind = mo::Curve::Kind::tween;
            options.curve.duration = 1;
            options.curve.ease = hanim::ease::linear;
            if (effects) options.travel = mo::Options::Travel{
                .effect = xrender::EffectRef{static_cast<std::uint32_t>(900 + level)},
                .params = {static_cast<float>(level + 1), static_cast<float>(level + 20)},
                .param = 0, .peak = static_cast<float>(level + 4)};
            auto a = frame(0, level), b = frame(10, level);
            motions.push_back(std::make_unique<mo::Morph>(mo::Matter::extract(a, names()),
                                                        mo::Matter::extract(b, names()), options, 0));
        }
    }
    void draw_level(std::size_t level, double time, xrender::DisplayList& out) const {
        struct Call { const Chain* chain; std::size_t level; double time; } call{this, level, time};
        // One-pointer callback stays in std::function's small buffer. Thus the
        // warm allocation probe includes traversal instead of hiding callback
        // heap allocations outside the measured region.
        motions[reuse_same ? 0 : level]->draw(time, out, [&call](std::uint32_t marker, xrender::DisplayList& into) {
            if (marker == 2 && call.level + 1 < call.chain->motions.size())
                call.chain->draw_level(call.level + 1, call.time, into);
        });
    }
    xrender::DisplayList draw(double time) const {
        xrender::DisplayList list;
        draw_level(0, time, list);
        return list;
    }
};

std::vector<xrender::FillRect> fills(const xrender::DisplayList& list) {
    std::vector<xrender::FillRect> found;
    for (const auto& command : list.commands())
        if (const auto* fill = std::get_if<xrender::FillRect>(&command)) found.push_back(*fill);
    return found;
}

void verify(const Chain& effected, const Chain& plain, double time, bool moving) {
    const auto actual = effected.draw(time), reference = plain.draw(time);
    expect(actual.balanced()) << "every nested travel layer and node is balanced";
    const auto got = fills(actual), want = fills(reference);
    // Before the first change the public contract draws source nested
    // content, not the current child callback. At later seeks it delegates.
    expect(eq(got.size(), time <= 0 ? fills_per_level + 1 : fills_per_level * effected.motions.size()));
    expect(eq(got.size(), want.size()));
    for (std::size_t i = 0; i < std::min(got.size(), want.size()); ++i) {
        expect(eq(got[i].rect, want[i].rect));
        expect(eq(got[i].paint.color, want[i].paint.color));
    }
    std::size_t layers = 0;
    for (const auto& command : actual.commands()) {
        const auto* layer = std::get_if<xrender::PushLayer>(&command);
        if (!layer || layer->effect.id < 900) continue;
        const std::size_t level = layer->effect.id - 900;
        expect(level < effected.motions.size()) << "travel identity belongs to this chain";
        expect(eq(layer->params.size(), std::size_t{2}));
        if (level < effected.motions.size() && layer->params.size() == 2) {
            const auto& motion = *effected.motions[level];
            const float expected = static_cast<float>(level + 1 + 3 * motion.travel(time));
            expect(std::abs(layer->params[0] - expected) < 0.00001f) << "each recursive layer keeps its own animated parameter";
            expect(eq(layer->params[1], static_cast<float>(level + 20)));
        }
        ++layers;
    }
    expect(eq(layers, moving ? effected.motions.size() : std::size_t{0}));
}
}

UTest cases{"nested travel scratch lifetime", [] {
    "two effect layers retain all current geometry while command storage grows"_test = [] {
        Chain actual(2, true), plain(2, false);
        verify(actual, plain, 0.5, true);
    };
    "twelve reentrant layers keep distinct payloads and release all scratch leases"_test = [] {
        Chain actual(12, true), plain(12, false);
        verify(actual, plain, 0.5, true);
        verify(actual, plain, 0.2, true);
        verify(actual, plain, 0.8, true);
    };
    "arbitrary seek and reverse preserve exact geometry and endpoint effect absence"_test = [] {
        Chain actual(3, true), plain(3, false);
        for (double time : {1.0, 0.8, 0.2, 0.5, 0.0, 0.5, 1.0})
            verify(actual, plain, time, time > 0 && time < 1);
    };
    "the same immutable Morph can reenter its own travel draw"_test = [] {
        Chain actual(5, true, true), plain(5, false, true);
        verify(actual, plain, 0.5, true);
        verify(actual, plain, 0.3, true);
    };
    "warmed recursive travel reuses every depth list and parameter buffer"_test = [] {
        Chain actual(4, true);
        xrender::DisplayList out;
        for (double time : {0.2, 0.5, 0.8}) { out.clear(); actual.draw_level(0, time, out); }
        const auto before = allocations.load(std::memory_order_relaxed);
        for (int i = 0; i < 40; ++i) {
            out.clear();
            actual.draw_level(0, i % 2 ? 0.25 : 0.75, out);
        }
        const auto after = allocations.load(std::memory_order_relaxed);
        expect(eq(after, before));
        expect(out.balanced());
        expect(eq(fills(out).size(), fills_per_level * actual.motions.size()));
    };
    mcpplibs::mtest::test(std::string_view(threads ? "concurrent seeks share only immutable Morph data"
                       : "threadless seeks share only immutable Morph data")) = [] {
        Chain actual(4, true);
        auto sample = [&](double time) {
            std::array<std::uint64_t, 8> hashes{};
            for (auto& hash : hashes) hash = actual.draw(time).hash();
            return hashes;
        };
        const auto a = sample(0.25), b = sample(0.75);
        if constexpr (threads) {
            auto first = std::async(std::launch::async, [&] { return sample(0.25); });
            auto second = std::async(std::launch::async, [&] { return sample(0.75); });
            expect(eq(first.get(), a));
            expect(eq(second.get(), b));
        } else {
            expect(eq(sample(0.25), a));
            expect(eq(sample(0.75), b));
        }
    };
}};
