// openmui.ui.base:external — what the system draws inside the interface
// (D-141): a web page in the system's own web view, which the host places
// over this node; the tree says where the page is, its title and whether it
// is loading. Where the host has none, a card that opens the system's browser.
export module omui.ui.base:external;

import std;
import omui.core;
import omui.view;
import :parts;
import :web_view_contract;

export namespace omui {

class WebView : public WebViewContract<WebView> {
public:
    static constexpr bool focusable = true;

    ~WebView();
    void mounted();
    void prop_changed(WebViewProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    void paint(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    ActResult navigate(ActContext& ctx, const Value& url);
    ActResult back(ActContext& ctx);
    ActResult forward(ActContext& ctx);
    ActResult reload(ActContext& ctx);
    ActResult open_external(ActContext& ctx);

    // The page as the system's view last told it (none: the address asked for).
    [[nodiscard]] const WebPage& page() const noexcept { return page_; }
    // Whether the system's own view shows it (else this draws a card).
    [[nodiscard]] bool in_system_view() const noexcept { return number_ != 0; }

private:
    void heard(const WebPage& now);
    std::uint64_t        number_ = 0;  // the host's page, 0: none
    WebPage              page_;
    mutable Rect         placed_;      // where the host was told it is
    mutable parts::Label title_, address_;
};

}  // namespace omui
