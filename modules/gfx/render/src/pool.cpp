module openmui.render;

import std;
import openmui.rhi;

namespace omui::render {

TexturePool::TexturePool(TexturePool&& other) noexcept
    : device_(other.device_), keep_(other.keep_), entries_(std::move(other.entries_)), frame_(other.frame_), made_(other.made_),
      released_(other.released_) {
    other.entries_.clear();
}

TexturePool& TexturePool::operator=(TexturePool&& other) noexcept {
    if (this != &other) {
        destroy_all();
        device_ = other.device_;
        keep_ = other.keep_;
        entries_ = std::move(other.entries_);
        other.entries_.clear();
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
}

void TexturePool::restart_frame() noexcept {
    for (Entry& e : entries_)
        if (e.last == frame_) e.used = false;
}

rhi::Result<rhi::Texture> TexturePool::acquire(const TextureNeed& need, std::string_view label) {
    for (Entry& e : entries_)
        if (!e.used && e.need == need) {
            e.used = true;
            e.last = frame_;
            return e.texture;
        }
    auto t = device_->create(rhi::TextureDesc{need.format, need.width, need.height, need.usage, label});
    if (!t) return std::unexpected(t.error());
    entries_.push_back({*t, need, true, frame_});
    ++made_;
    return *t;
}

bool TexturePool::owns(rhi::Texture t) const noexcept {
    return std::ranges::any_of(entries_, [&](const Entry& e) { return e.texture == t; });
}

}  // namespace omui::render
