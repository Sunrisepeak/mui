module xrender.graph.api;

import std;
import mlibs.rhi;

namespace xrender {

TexturePool::TexturePool(TexturePool&& other) noexcept
    : device_(other.device_), keep_(other.keep_), entries_(std::move(other.entries_)), histories_(std::move(other.histories_)), frame_(other.frame_), made_(other.made_),
      released_(other.released_) {
    other.entries_.clear();
    other.histories_.clear();
}

TexturePool& TexturePool::operator=(TexturePool&& other) noexcept {
    if (this != &other) {
        destroy_all();
        device_ = other.device_;
        keep_ = other.keep_;
        entries_ = std::move(other.entries_);
        other.entries_.clear();
        histories_ = std::move(other.histories_);
        other.histories_.clear();
        frame_ = other.frame_;
        made_ = other.made_;
        released_ = other.released_;
    }
    return *this;
}

TexturePool::~TexturePool() { destroy_all(); }

void TexturePool::destroy_all() noexcept {
    for (const Entry& e : entries_) device_->destroy(e.texture);
    entries_.clear();
    for (const Kept& kept : histories_) device_->destroy(kept.textures[0]), device_->destroy(kept.textures[1]);
    histories_.clear();
}

void TexturePool::begin_frame() {
    ++frame_;
    std::erase_if(entries_, [&](const Entry& e) {
        if (frame_ - e.last <= keep_) return false;
        device_->destroy(e.texture);
        ++released_;
        return true;
    });
    for (Entry& e : entries_) e.used = false;
    std::erase_if(histories_, [&](const Kept& kept) {
        if (frame_ - kept.asked <= keep_) return false;
        device_->destroy(kept.textures[0]), device_->destroy(kept.textures[1]);
        released_ += 2;
        return true;
    });
}

mlibs::rhi::Result<History> TexturePool::history(std::string_view name, const TextureNeed& need, std::string_view label) {
    const auto found = std::ranges::find_if(histories_, [&](const Kept& kept) { return kept.name == name; });
    if (found != histories_.end() && found->need == need) {
        Kept& kept = *found;
        if (kept.asked != frame_) {
            // A later frame: what was written is now the past, and the other is written.
            kept.past = kept.written ? static_cast<std::uint32_t>(std::min<std::uint64_t>(frame_ - kept.asked, 0xffffffffu)) : 0;
            kept.now ^= 1u, kept.asked = frame_, kept.written = true;
        }
        return History{kept.textures[kept.now], kept.textures[kept.now ^ 1u], kept.past};
    }
    auto first = device_->create(mlibs::rhi::TextureDesc{need.format, need.width, need.height, need.usage, label});
    if (!first) return std::unexpected(first.error());
    auto second = device_->create(mlibs::rhi::TextureDesc{need.format, need.width, need.height, need.usage, label});
    if (!second) {
        device_->destroy(*first);
        return std::unexpected(second.error());
    }
    made_ += 2;
    if (found != histories_.end()) {
        // What it is changed: its past is of another kind, and goes.
        device_->destroy(found->textures[0]), device_->destroy(found->textures[1]);
        released_ += 2;
        *found = Kept{std::move(found->name), need, {*first, *second}, 0, frame_, 0, true};
        return History{*first, *second, 0};
    }
    histories_.push_back(Kept{std::string(name), need, {*first, *second}, 0, frame_, 0, true});
    return History{*first, *second, 0};
}

void TexturePool::forget(std::string_view name) noexcept {
    for (Kept& kept : histories_)
        if (kept.name == name) kept.written = false, kept.past = 0;
}

void TexturePool::restart_frame() noexcept {
    for (Entry& e : entries_)
        if (e.last == frame_) e.used = false;
}

mlibs::rhi::Result<mlibs::rhi::Texture> TexturePool::acquire(const TextureNeed& need, std::string_view label) {
    for (Entry& e : entries_)
        if (!e.used && e.need == need) {
            e.used = true;
            e.last = frame_;
            return e.texture;
        }
    auto t = device_->create(mlibs::rhi::TextureDesc{need.format, need.width, need.height, need.usage, label});
    if (!t) return std::unexpected(t.error());
    entries_.push_back({*t, need, true, frame_});
    ++made_;
    return *t;
}

bool TexturePool::owns(mlibs::rhi::Texture t) const noexcept {
    return std::ranges::any_of(entries_, [&](const Entry& e) { return e.texture == t; }) ||
           std::ranges::any_of(histories_, [&](const Kept& kept) { return kept.textures[0] == t || kept.textures[1] == t; });
}

}  // namespace xrender
