// Web pages in WKWebView (openmui.host.sdl3:web), for macOS alone: the
// system's own web view, made and placed in the window's content view and
// spoken to through mcpplibs.os's Objective-C runtime — no SDK header. What a
// page is now (its address, title, loading, history) is read each turn of the
// loop and told where it changed.
module omui.host.sdl3;

import std;
import omui.core;
import omui.view;
import mcpplibs.os;
import :web;

namespace omui::web {

namespace objc = mcpplibs::os::macos::objc;

namespace {

// NSRect as AppKit lays it out (CGFloat is double on 64-bit).
struct NativeRect {
    double x, y, width, height;
};

struct Page {
    objc::id                             view;
    WebPage                              told;
    std::function<void(const WebPage&)> changed;
};

std::string text_of(void* string) { return string ? objc::utf8(string) : std::string(); }

}  // namespace

struct Pages {
    objc::id                        window;
    std::map<std::uint64_t, Page>   open;
    std::uint64_t                   next = 1;
};

std::unique_ptr<Pages, void (*)(Pages*)> open_pages(void* system_window) {
    std::unique_ptr<Pages, void (*)(Pages*)> none(nullptr, [](Pages* pages) { delete pages; });
    if (!system_window || !objc::available() || !objc::load_framework("WebKit") || !objc::class_named("WKWebView")) return none;
    auto made = std::unique_ptr<Pages, void (*)(Pages*)>(new Pages, [](Pages* pages) {
        const objc::autorelease_pool pool;
        for (auto& [number, page] : pages->open) objc::send(page.view, "removeFromSuperview");
        delete pages;
    });
    made->window = objc::id::borrow(system_window);
    return made;
}

std::uint64_t open(Pages& pages, std::string_view url, std::function<void(const WebPage&)> changed) {
    const objc::autorelease_pool pool;
    void* content = objc::send<void*>(pages.window, "contentView");
    if (!content) return 0;
    const objc::id configuration = objc::id::adopt(objc::send<void*>(objc::class_named("WKWebViewConfiguration"), "new"));
    void* allocated = objc::send<void*>(objc::class_named("WKWebView"), "alloc");
    const objc::id view = objc::id::adopt(objc::send<void*>(allocated, "initWithFrame:configuration:", NativeRect{0, 0, 1, 1}, configuration.raw()));
    if (!view) return 0;
    objc::send(view, "setHidden:", static_cast<signed char>(1));  // shown once placed
    objc::send(content, "addSubview:", view.raw());
    const std::uint64_t number = pages.next++;
    pages.open.emplace(number, Page{view, {.url = std::string(url)}, std::move(changed)});
    (void)command(pages, number, "navigate", url);
    return number;
}

void place(Pages& pages, std::uint64_t page, Rect bounds, bool shown, float height) {
    const auto found = pages.open.find(page);
    if (found == pages.open.end()) return;
    const objc::autorelease_pool pool;
    // AppKit's content view counts from the bottom left (in points: the interface's logical pixels); the
    // interface from the top left. The window's height is the host's (no struct comes back from a message:
    // on x86-64 that would need objc_msgSend_stret).
    const NativeRect frame{bounds.x, static_cast<double>(height) - bounds.y - bounds.height, bounds.width, bounds.height};
    objc::send(found->second.view, "setFrame:", frame);
    objc::send(found->second.view, "setHidden:", static_cast<signed char>(shown ? 0 : 1));
}

bool command(Pages& pages, std::uint64_t page, std::string_view what, std::string_view argument) {
    const auto found = pages.open.find(page);
    if (found == pages.open.end()) return false;
    const objc::autorelease_pool pool;
    const objc::id& view = found->second.view;
    if (what == "navigate") {
        void* address = objc::send<void*>(objc::class_named("NSURL"), "URLWithString:", objc::string(argument).raw());
        if (!address) return false;
        void* request = objc::send<void*>(objc::class_named("NSURLRequest"), "requestWithURL:", address);
        return objc::send<void*>(view, "loadRequest:", request) != nullptr;
    }
    if (what == "back") return objc::send<void*>(view, "goBack") != nullptr;
    if (what == "forward") return objc::send<void*>(view, "goForward") != nullptr;
    if (what == "reload") return objc::send<void*>(view, "reload") != nullptr;
    return false;
}

void close(Pages& pages, std::uint64_t page) {
    const auto found = pages.open.find(page);
    if (found == pages.open.end()) return;
    const objc::autorelease_pool pool;
    objc::send(found->second.view, "removeFromSuperview");
    pages.open.erase(found);
}

void poll(Pages& pages) {
    const objc::autorelease_pool pool;
    for (auto& [number, page] : pages.open) {
        WebPage now;
        void* address = objc::send<void*>(page.view, "URL");
        now.url = address ? text_of(objc::send<void*>(address, "absoluteString")) : page.told.url;
        now.title = text_of(objc::send<void*>(page.view, "title"));
        now.loading = objc::send<signed char>(page.view, "isLoading") != 0;
        now.can_go_back = objc::send<signed char>(page.view, "canGoBack") != 0;
        now.can_go_forward = objc::send<signed char>(page.view, "canGoForward") != 0;
        if (now.url == page.told.url && now.title == page.told.title && now.loading == page.told.loading && now.can_go_back == page.told.can_go_back &&
            now.can_go_forward == page.told.can_go_forward)
            continue;
        page.told = now;
        if (page.changed) page.changed(now);
    }
}

}  // namespace omui::web
