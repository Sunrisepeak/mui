module openmui.image;

import std;

namespace omui::imageio {

namespace {

// UTF-8 text of a path (generic separators for `generic_utf8`), never
// through the process's code page: std::filesystem::path::string() throws on
// Windows for a name the ANSI code page cannot spell (lint rule `paths`).
[[maybe_unused]] std::string utf8(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}
[[maybe_unused]] std::string generic_utf8(const std::filesystem::path& p) {
    const std::u8string u = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

}  // namespace

View view(const Image& img) noexcept { return {img.rgba.data(), img.width, img.height, img.width * 4, img.alpha}; }

void premultiply(Image& img) {
    if (img.alpha == Alpha::premultiplied) return;
    for (std::size_t i = 0; i + 3 < img.rgba.size(); i += 4) {
        std::uint32_t a = img.rgba[i + 3];
        for (int c = 0; c < 3; ++c) {
            std::uint32_t t = img.rgba[i + static_cast<std::size_t>(c)] * a + 128;
            img.rgba[i + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>((t + (t >> 8)) >> 8);
        }
    }
    img.alpha = Alpha::premultiplied;
}

void unpremultiply(Image& img) {
    if (img.alpha == Alpha::straight) return;
    for (std::size_t i = 0; i + 3 < img.rgba.size(); i += 4) {
        std::uint32_t a = img.rgba[i + 3];
        for (int c = 0; c < 3; ++c) {
            std::uint32_t v = img.rgba[i + static_cast<std::size_t>(c)];
            img.rgba[i + static_cast<std::size_t>(c)] = a == 0 ? 0 : static_cast<std::uint8_t>(std::min<std::uint32_t>(255, (v * 255 + a / 2) / a));
        }
    }
    img.alpha = Alpha::straight;
}

Straight straight(const View& pixels) {
    if (pixels.alpha == Alpha::straight) return {{}, pixels.rgba, pixels.stride};
    Image tmp;
    tmp.width = pixels.width;
    tmp.height = pixels.height;
    tmp.alpha = Alpha::premultiplied;
    tmp.rgba.resize(static_cast<std::size_t>(pixels.width) * pixels.height * 4);
    for (int y = 0; y < pixels.height; ++y)
        std::memcpy(tmp.rgba.data() + static_cast<std::size_t>(y) * pixels.width * 4, pixels.rgba + static_cast<std::size_t>(y) * pixels.stride,
                    static_cast<std::size_t>(pixels.width) * 4);
    unpremultiply(tmp);
    Straight out{std::move(tmp.rgba), nullptr, pixels.width * 4};
    out.rgba = out.copy.data();
    return out;
}

namespace {

std::expected<void, std::string> write_file(const std::filesystem::path& file, std::expected<std::vector<std::uint8_t>, std::string> bytes) {
    if (!bytes) return std::unexpected(bytes.error());
    std::ofstream out(file, std::ios::binary);
    if (!out) return std::unexpected(std::format("cannot write {}", generic_utf8(file)));
    out.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
    if (!out) return std::unexpected(std::format("cannot write {}", generic_utf8(file)));
    return {};
}

std::expected<std::vector<std::uint8_t>, std::string> read_file(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::unexpected(std::format("cannot read {}", generic_utf8(file)));
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool is_png(std::span<const std::uint8_t> b) {
    constexpr std::array<std::uint8_t, 8> sig{0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    return b.size() >= sig.size() && std::equal(sig.begin(), sig.end(), b.begin());
}
bool is_webp(std::span<const std::uint8_t> b) {
    auto at = [&](std::size_t i, std::string_view tag) { return std::equal(tag.begin(), tag.end(), b.begin() + static_cast<std::ptrdiff_t>(i)); };
    return b.size() >= 12 && at(0, "RIFF") && at(8, "WEBP");
}

}  // namespace

std::expected<void, std::string> save_png(const std::filesystem::path& file, const View& pixels) { return write_file(file, encode_png(pixels)); }

std::expected<void, std::string> save_apng(const std::filesystem::path& file, std::span<const View> frames, ApngOptions options) {
    return write_file(file, encode_apng(frames, options));
}

std::expected<void, std::string> save_webp(const std::filesystem::path& file, const View& pixels, WebpOptions options) {
    return write_file(file, encode_webp(pixels, options));
}

std::expected<Image, std::string> load_png(const std::filesystem::path& file) { return read_file(file).and_then([](const auto& b) { return decode_png(b); }); }

std::expected<Image, std::string> load_webp(const std::filesystem::path& file) { return read_file(file).and_then([](const auto& b) { return decode_webp(b); }); }

std::expected<Image, std::string> decode(std::span<const std::uint8_t> bytes) {
    if (is_png(bytes)) return decode_png(bytes);
    if (is_webp(bytes)) return decode_webp(bytes);
    return std::unexpected("not a PNG or WebP image");
}

std::expected<Image, std::string> load(const std::filesystem::path& file) { return read_file(file).and_then([](const auto& b) { return decode(b); }); }

namespace {

std::uint32_t crc32(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b = {}) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    std::uint32_t c = 0xffffffffu;
    for (const auto part : {a, b})
        for (const std::uint8_t x : part) c = table[(c ^ x) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

void be32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<std::uint8_t>(v >> s));
}
void be16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

void chunk(std::vector<std::uint8_t>& out, std::string_view type, std::span<const std::uint8_t> data) {
    be32(out, static_cast<std::uint32_t>(data.size()));
    const std::array<std::uint8_t, 4> t{static_cast<std::uint8_t>(type[0]), static_cast<std::uint8_t>(type[1]), static_cast<std::uint8_t>(type[2]),
                                        static_cast<std::uint8_t>(type[3])};
    out.insert(out.end(), t.begin(), t.end());
    out.insert(out.end(), data.begin(), data.end());
    be32(out, crc32(t, data));
}

// A PNG's chunks of one type, their data in order.
std::vector<std::span<const std::uint8_t>> chunks(std::span<const std::uint8_t> png, std::string_view type) {
    std::vector<std::span<const std::uint8_t>> found;
    for (std::size_t at = 8; at + 12 <= png.size();) {
        const std::uint32_t n = (std::uint32_t{png[at]} << 24) | (std::uint32_t{png[at + 1]} << 16) | (std::uint32_t{png[at + 2]} << 8) | png[at + 3];
        if (at + 12 + n > png.size()) break;
        if (std::string_view(reinterpret_cast<const char*>(png.data() + at + 4), 4) == type) found.push_back(png.subspan(at + 8, n));
        at += 12 + n;
    }
    return found;
}

struct Box {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // [x0, x1) × [y0, y1); empty: nothing changed
    [[nodiscard]] bool empty() const noexcept { return x1 <= x0 || y1 <= y0; }
};

// Where `b` differs from `a`.
Box changed(const View& a, const View& b) {
    Box box{b.width, b.height, 0, 0};
    for (int y = 0; y < b.height; ++y) {
        const std::uint8_t* ra = a.rgba + static_cast<std::size_t>(y) * a.stride;
        const std::uint8_t* rb = b.rgba + static_cast<std::size_t>(y) * b.stride;
        if (std::memcmp(ra, rb, static_cast<std::size_t>(b.width) * 4) == 0) continue;
        int l = 0, r = b.width;
        while (l < r && std::memcmp(ra + l * 4, rb + l * 4, 4) == 0) ++l;
        while (r > l && std::memcmp(ra + (r - 1) * 4, rb + (r - 1) * 4, 4) == 0) --r;
        box.x0 = std::min(box.x0, l), box.x1 = std::max(box.x1, r);
        box.y0 = std::min(box.y0, y), box.y1 = std::max(box.y1, y + 1);
    }
    return box;
}

}  // namespace

std::expected<std::vector<std::uint8_t>, std::string> encode_apng(std::span<const View> frames, ApngOptions options) {
    if (frames.empty()) return std::unexpected("no frames");
    const View& first = frames.front();
    for (const View& f : frames)
        if (f.width != first.width || f.height != first.height || !f.rgba) return std::unexpected("every frame is the size of the first");
    if (options.delay_den == 0) return std::unexpected("a delay's denominator is not 0");
    // The frames as shown: each with the rectangle it changes and how many delays it lasts.
    struct Shown {
        std::size_t   frame;
        Box           box;
        std::uint32_t length;
    };
    std::vector<Shown> shown{{0, Box{0, 0, first.width, first.height}, 1}};
    for (std::size_t i = 1; i < frames.size(); ++i) {
        const Box b = changed(frames[i - 1], frames[i]);
        if (b.empty()) ++shown.back().length;
        else shown.push_back({i, b, 1});
    }
    auto png = encode_png(first);
    if (!png) return std::unexpected(png.error());
    std::vector<std::uint8_t> out{0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    const auto ihdr = chunks(*png, "IHDR");
    if (ihdr.empty()) return std::unexpected("png: no IHDR");
    chunk(out, "IHDR", ihdr.front());
    std::vector<std::uint8_t> data;
    be32(data, static_cast<std::uint32_t>(shown.size()));
    be32(data, options.loops);
    chunk(out, "acTL", data);
    std::uint32_t sequence = 0;
    for (std::size_t k = 0; k < shown.size(); ++k) {
        const Shown& s = shown[k];
        const View& f = frames[s.frame];
        const View part{f.rgba + static_cast<std::size_t>(s.box.y0) * f.stride + static_cast<std::size_t>(s.box.x0) * 4, s.box.x1 - s.box.x0,
                        s.box.y1 - s.box.y0, f.stride, f.alpha};
        if (k > 0) {
            png = encode_png(part);
            if (!png) return std::unexpected(png.error());
        }
        const std::uint32_t num = static_cast<std::uint32_t>(options.delay_num) * s.length;
        if (num > 0xffff) return std::unexpected("a frame lasts too long for its delay's numerator");
        data.clear();
        be32(data, sequence++);
        be32(data, static_cast<std::uint32_t>(part.width));
        be32(data, static_cast<std::uint32_t>(part.height));
        be32(data, static_cast<std::uint32_t>(s.box.x0));
        be32(data, static_cast<std::uint32_t>(s.box.y0));
        be16(data, static_cast<std::uint16_t>(num));
        be16(data, options.delay_den);
        data.push_back(0);  // dispose: none (the next frame draws over this one)
        data.push_back(0);  // blend: source (its pixels replace what is there)
        chunk(out, "fcTL", data);
        for (const auto idat : chunks(*png, "IDAT")) {
            if (k == 0) {
                chunk(out, "IDAT", idat);
                continue;
            }
            data.clear();
            be32(data, sequence++);
            data.insert(data.end(), idat.begin(), idat.end());
            chunk(out, "fdAT", data);
        }
    }
    chunk(out, "IEND", {});
    return out;
}

// ── cutting ─────────────────────────────────────────────────────────────────

namespace {

// BT.601's chroma, in 0–255 units (the same arithmetic everywhere: no libm).
std::pair<float, float> chroma(float r, float g, float b) {
    return {-0.168736f * r - 0.331264f * g + 0.5f * b, 0.5f * r - 0.418688f * g - 0.081312f * b};
}

}  // namespace

void key_out(Image& img, Key key) {
    if (img.alpha == Alpha::premultiplied) unpremultiply(img);
    const auto [kcb, kcr] = chroma(key.r, key.g, key.b);
    const float span = std::max(1.0f, key.outer - key.inner);
    // Which channel the key is strongest in: what despill takes back.
    const int strong = key.g >= key.r && key.g >= key.b ? 1 : key.b >= key.r ? 2 : 0;
    for (std::size_t i = 0; i + 3 < img.rgba.size(); i += 4) {
        const float r = img.rgba[i], g = img.rgba[i + 1], b = img.rgba[i + 2];
        const auto [cb, cr] = chroma(r, g, b);
        const float d = std::sqrt((cb - kcb) * (cb - kcb) + (cr - kcr) * (cr - kcr));
        const float a = std::clamp((d - key.inner) / span, 0.0f, 1.0f);
        img.rgba[i + 3] = static_cast<std::uint8_t>(std::lround(a * (img.rgba[i + 3] / 255.0f) * 255.0f));
        // Despill: the key's channel no stronger than the other two's larger.
        std::array<float, 3> c{r, g, b};
        const float other = strong == 1 ? std::max(r, b) : strong == 2 ? std::max(r, g) : std::max(g, b);
        if (c[static_cast<std::size_t>(strong)] > other) c[static_cast<std::size_t>(strong)] = other;
        for (std::size_t k = 0; k < 3; ++k) img.rgba[i + k] = static_cast<std::uint8_t>(std::lround(c[k]));
    }
}

std::vector<Piece> pieces(const Image& img, std::uint8_t alpha, std::int32_t gap, std::int64_t min_area, std::vector<std::int32_t>* labels) {
    const std::int32_t w = img.width, h = img.height;
    const auto n = static_cast<std::size_t>(w) * h;
    std::vector<std::uint8_t> solid(n), near(n);
    for (std::size_t i = 0; i < n; ++i) solid[i] = img.rgba[i * 4 + 3] > alpha ? 1 : 0;
    // Pixels within gap/2 of a solid one (a box dilation, rows then columns): pieces closer than `gap` touch.
    const std::int32_t r = std::max(0, gap / 2);
    std::vector<std::uint8_t> rows(n);
    for (std::int32_t y = 0; y < h; ++y) {
        std::int32_t last = -1000000;  // the latest solid pixel at or left of x
        for (std::int32_t x = 0; x < w; ++x) {
            if (solid[static_cast<std::size_t>(y) * w + x]) last = x;
            rows[static_cast<std::size_t>(y) * w + x] = x - last <= r;
        }
        last = 1000000;
        for (std::int32_t x = w - 1; x >= 0; --x) {
            if (solid[static_cast<std::size_t>(y) * w + x]) last = x;
            if (last - x <= r) rows[static_cast<std::size_t>(y) * w + x] = 1;
        }
    }
    for (std::int32_t x = 0; x < w; ++x) {
        std::int32_t last = -1000000;
        for (std::int32_t y = 0; y < h; ++y) {
            if (rows[static_cast<std::size_t>(y) * w + x]) last = y;
            near[static_cast<std::size_t>(y) * w + x] = y - last <= r;
        }
        last = 1000000;
        for (std::int32_t y = h - 1; y >= 0; --y) {
            if (rows[static_cast<std::size_t>(y) * w + x]) last = y;
            if (last - y <= r) near[static_cast<std::size_t>(y) * w + x] = 1;
        }
    }
    // Regions of the dilated mask; each piece's bounds and area from its solid pixels alone.
    std::vector<std::int32_t> label(n, -1);
    std::vector<Piece> found;
    std::vector<std::int32_t> stack;
    for (std::int32_t y = 0; y < h; ++y)
        for (std::int32_t x = 0; x < w; ++x) {
            const auto at0 = static_cast<std::size_t>(y) * w + x;
            if (!solid[at0] || label[at0] >= 0) continue;
            const auto id = static_cast<std::int32_t>(found.size());
            Piece p{x, y, 1, 1, 0};
            std::int32_t x1 = x, y1 = y;
            stack.assign(1, y * w + x);
            label[at0] = id;
            while (!stack.empty()) {
                const std::int32_t at = stack.back();
                stack.pop_back();
                const std::int32_t px = at % w, py = at / w;
                if (solid[static_cast<std::size_t>(at)]) {
                    ++p.area;
                    p.x = std::min(p.x, px), p.y = std::min(p.y, py), x1 = std::max(x1, px), y1 = std::max(y1, py);
                }
                for (const auto [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                    const std::int32_t nx = px + dx, ny = py + dy;
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    auto& l = label[static_cast<std::size_t>(ny) * w + nx];
                    if (l >= 0 || !near[static_cast<std::size_t>(ny) * w + nx]) continue;
                    l = id;
                    stack.push_back(ny * w + nx);
                }
            }
            p.width = x1 - p.x + 1, p.height = y1 - p.y + 1;
            found.push_back(p);
        }
    // Each found piece keeps its id (its index before filtering and ordering) to map labels at the end.
    std::vector<std::int32_t> ids(found.size());
    std::iota(ids.begin(), ids.end(), 0);
    std::vector<std::pair<Piece, std::int32_t>> kept;
    for (std::size_t i = 0; i < found.size(); ++i)
        if (found[i].area >= min_area) kept.emplace_back(found[i], static_cast<std::int32_t>(i));
    found.clear();
    for (const auto& [p, id] : kept) found.push_back(p);
    // Reading order: a piece belongs to the row of the first piece it overlaps vertically.
    std::ranges::sort(kept, [](const auto& a, const auto& b) { return a.first.y != b.first.y ? a.first.y < b.first.y : a.first.x < b.first.x; });
    std::vector<std::pair<Piece, std::int32_t>> ordered;
    std::vector<bool> taken(kept.size(), false);
    for (std::size_t i = 0; i < kept.size(); ++i) {
        if (taken[i]) continue;
        const Piece& head = kept[i].first;
        const std::int32_t top = head.y, bottom = head.y + head.height;
        std::vector<std::pair<Piece, std::int32_t>> row;
        for (std::size_t j = i; j < kept.size(); ++j) {
            const Piece& q = kept[j].first;
            if (!taken[j] && q.y < bottom && q.y + q.height > top && q.y < top + head.height / 2) {
                taken[j] = true;
                row.push_back(kept[j]);
            }
        }
        std::ranges::sort(row, [](const auto& a, const auto& b) { return a.first.x < b.first.x; });
        ordered.insert(ordered.end(), row.begin(), row.end());
    }
    std::vector<Piece> out;
    if (labels) {
        std::vector<std::int32_t> remap(ids.size(), -1);
        for (std::size_t k = 0; k < ordered.size(); ++k) remap[static_cast<std::size_t>(ordered[k].second)] = static_cast<std::int32_t>(k);
        labels->assign(label.size(), -1);
        for (std::size_t i = 0; i < label.size(); ++i)
            if (solid[i] && label[i] >= 0) (*labels)[i] = remap[static_cast<std::size_t>(label[i])];
    }
    for (const auto& [p, id] : ordered) out.push_back(p);
    return out;
}

Image scaled_down(const Image& in, std::int32_t width, std::int32_t height) {
    Image img = in;
    if (img.alpha == Alpha::straight) premultiply(img);  // averaged premultiplied: no fringe from clear pixels' colour
    Image out;
    out.width = std::max(1, std::min(width, img.width)), out.height = std::max(1, std::min(height, img.height));
    out.alpha = Alpha::premultiplied;
    out.rgba.assign(static_cast<std::size_t>(out.width) * out.height * 4, 0);
    const double sx = static_cast<double>(img.width) / out.width, sy = static_cast<double>(img.height) / out.height;
    for (std::int32_t y = 0; y < out.height; ++y)
        for (std::int32_t x = 0; x < out.width; ++x) {
            const double x0 = x * sx, x1 = (x + 1) * sx, y0 = y * sy, y1 = (y + 1) * sy;
            std::array<double, 4> sum{};
            double area = 0;
            for (auto yy = static_cast<std::int32_t>(y0); yy < std::min(static_cast<double>(img.height), std::ceil(y1)); ++yy)
                for (auto xx = static_cast<std::int32_t>(x0); xx < std::min(static_cast<double>(img.width), std::ceil(x1)); ++xx) {
                    const double wx = std::min<double>(xx + 1, x1) - std::max<double>(xx, x0), wy = std::min<double>(yy + 1, y1) - std::max<double>(yy, y0);
                    if (wx <= 0 || wy <= 0) continue;
                    const double wgt = wx * wy;
                    area += wgt;
                    for (std::size_t c = 0; c < 4; ++c) sum[c] += wgt * img.rgba[(static_cast<std::size_t>(yy) * img.width + xx) * 4 + c];
                }
            for (std::size_t c = 0; c < 4; ++c)
                out.rgba[(static_cast<std::size_t>(y) * out.width + x) * 4 + c] = static_cast<std::uint8_t>(std::lround(area > 0 ? sum[c] / area : 0));
        }
    if (in.alpha == Alpha::straight) unpremultiply(out);
    return out;
}

Packed pack(std::span<const Image> images, std::int32_t width, std::int32_t padding) {
    Packed out;
    out.at.resize(images.size());
    std::vector<std::size_t> order(images.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::ranges::stable_sort(order, [&](std::size_t a, std::size_t b) { return images[a].height > images[b].height; });
    std::int32_t x = padding, y = padding, shelf = 0, used = 0;
    for (const std::size_t i : order) {
        const Image& img = images[i];
        if (x + img.width + padding > width && x > padding) x = padding, y += shelf + padding, shelf = 0;
        out.at[i] = {x, y, img.width, img.height, static_cast<std::int64_t>(img.width) * img.height};
        x += img.width + padding;
        shelf = std::max(shelf, img.height);
        used = std::max(used, x);
    }
    out.atlas.width = std::max(1, std::min(width, used));
    out.atlas.height = std::max(1, y + shelf + padding);
    out.atlas.alpha = images.empty() ? Alpha::straight : images[0].alpha;
    out.atlas.rgba.assign(static_cast<std::size_t>(out.atlas.width) * out.atlas.height * 4, 0);
    for (std::size_t i = 0; i < images.size(); ++i) {
        const Image& img = images[i];
        for (std::int32_t r = 0; r < img.height; ++r)
            std::memcpy(out.atlas.rgba.data() + (static_cast<std::size_t>(out.at[i].y + r) * out.atlas.width + out.at[i].x) * 4,
                        img.rgba.data() + static_cast<std::size_t>(r) * img.width * 4, static_cast<std::size_t>(img.width) * 4);
    }
    return out;
}

Image crop(const Image& img, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) {
    Image out;
    x = std::clamp(x, 0, img.width), y = std::clamp(y, 0, img.height);
    width = std::clamp(width, 0, img.width - x), height = std::clamp(height, 0, img.height - y);
    out.width = width, out.height = height, out.alpha = img.alpha;
    out.rgba.resize(static_cast<std::size_t>(width) * height * 4);
    for (std::int32_t r = 0; r < height; ++r)
        std::memcpy(out.rgba.data() + static_cast<std::size_t>(r) * width * 4, img.rgba.data() + (static_cast<std::size_t>(y + r) * img.width + x) * 4,
                    static_cast<std::size_t>(width) * 4);
    return out;
}

}  // namespace omui::imageio
