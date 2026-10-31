// The adapter over AccessKit: nodes as AccessKit's, the platform's requests
// as the mirror's, and the platform service of this build (native.cppm).
module openmui.accessibility;

import std;
import openmui.core;
import openmui.trace;
import mcpplibs.mwrap.accesskit;
import :native;

namespace omui::accessibility {

namespace {

constexpr Log log_{"accessibility"};

struct RoleName {
    std::string_view name;
    accesskit_role   role;
};

// The names roles.toml's `accesskit` column uses, as AccessKit's roles.
constexpr RoleName role_names[] = {
    {"AlertDialog", ACCESSKIT_ROLE_ALERT_DIALOG},
    {"Application", ACCESSKIT_ROLE_APPLICATION},
    {"Article", ACCESSKIT_ROLE_ARTICLE},
    {"Blockquote", ACCESSKIT_ROLE_BLOCKQUOTE},
    {"Button", ACCESSKIT_ROLE_BUTTON},
    {"Canvas", ACCESSKIT_ROLE_CANVAS},
    {"Cell", ACCESSKIT_ROLE_CELL},
    {"CheckBox", ACCESSKIT_ROLE_CHECK_BOX},
    {"Code", ACCESSKIT_ROLE_CODE},
    {"ColumnHeader", ACCESSKIT_ROLE_COLUMN_HEADER},
    {"ComboBox", ACCESSKIT_ROLE_COMBO_BOX},
    {"Dialog", ACCESSKIT_ROLE_DIALOG},
    {"Document", ACCESSKIT_ROLE_DOCUMENT},
    {"Figure", ACCESSKIT_ROLE_FIGURE},
    {"GenericContainer", ACCESSKIT_ROLE_GENERIC_CONTAINER},
    {"Grid", ACCESSKIT_ROLE_GRID},
    {"Group", ACCESSKIT_ROLE_GROUP},
    {"Heading", ACCESSKIT_ROLE_HEADING},
    {"Image", ACCESSKIT_ROLE_IMAGE},
    {"Label", ACCESSKIT_ROLE_LABEL},
    {"Link", ACCESSKIT_ROLE_LINK},
    {"List", ACCESSKIT_ROLE_LIST},
    {"ListBox", ACCESSKIT_ROLE_LIST_BOX},
    {"ListBoxOption", ACCESSKIT_ROLE_LIST_BOX_OPTION},
    {"ListItem", ACCESSKIT_ROLE_LIST_ITEM},
    {"Menu", ACCESSKIT_ROLE_MENU},
    {"MenuBar", ACCESSKIT_ROLE_MENU_BAR},
    {"MenuItem", ACCESSKIT_ROLE_MENU_ITEM},
    {"MenuItemCheckBox", ACCESSKIT_ROLE_MENU_ITEM_CHECK_BOX},
    {"MenuItemRadio", ACCESSKIT_ROLE_MENU_ITEM_RADIO},
    {"Meter", ACCESSKIT_ROLE_METER},
    {"Paragraph", ACCESSKIT_ROLE_PARAGRAPH},
    {"ProgressIndicator", ACCESSKIT_ROLE_PROGRESS_INDICATOR},
    {"RadioButton", ACCESSKIT_ROLE_RADIO_BUTTON},
    {"RadioGroup", ACCESSKIT_ROLE_RADIO_GROUP},
    {"Region", ACCESSKIT_ROLE_REGION},
    {"Row", ACCESSKIT_ROLE_ROW},
    {"RowHeader", ACCESSKIT_ROLE_ROW_HEADER},
    {"ScrollBar", ACCESSKIT_ROLE_SCROLL_BAR},
    {"ScrollView", ACCESSKIT_ROLE_SCROLL_VIEW},
    {"SearchInput", ACCESSKIT_ROLE_SEARCH_INPUT},
    {"Slider", ACCESSKIT_ROLE_SLIDER},
    {"SpinButton", ACCESSKIT_ROLE_SPIN_BUTTON},
    {"Splitter", ACCESSKIT_ROLE_SPLITTER},
    {"Status", ACCESSKIT_ROLE_STATUS},
    {"Switch", ACCESSKIT_ROLE_SWITCH},
    {"Tab", ACCESSKIT_ROLE_TAB},
    {"Table", ACCESSKIT_ROLE_TABLE},
    {"TabList", ACCESSKIT_ROLE_TAB_LIST},
    {"TabPanel", ACCESSKIT_ROLE_TAB_PANEL},
    {"TextInput", ACCESSKIT_ROLE_TEXT_INPUT},
    {"Timer", ACCESSKIT_ROLE_TIMER},
    {"Toolbar", ACCESSKIT_ROLE_TOOLBAR},
    {"Tooltip", ACCESSKIT_ROLE_TOOLTIP},
    {"Tree", ACCESSKIT_ROLE_TREE},
    {"TreeItem", ACCESSKIT_ROLE_TREE_ITEM},
    {"Video", ACCESSKIT_ROLE_VIDEO},
    {"Window", ACCESSKIT_ROLE_WINDOW},
};

constexpr std::optional<accesskit_role> role_of(std::string_view name) {
    for (const RoleName& r : role_names)
        if (r.name == name) return r.role;
    return std::nullopt;
}

// Every role of the spec is one of AccessKit's.
consteval bool every_role_maps() {
    for (const spec::RoleInfo& r : spec::roles)
        if (!role_of(r.accesskit)) return false;
    return role_of("Window").has_value();
}
static_assert(every_role_maps(), "a role in roles.toml names no AccessKit role");

// Action is AccessKit's order.
static_assert(static_cast<int>(Action::click) == ACCESSKIT_ACTION_CLICK && static_cast<int>(Action::focus) == ACCESSKIT_ACTION_FOCUS &&
              static_cast<int>(Action::custom) == ACCESSKIT_ACTION_CUSTOM_ACTION &&
              static_cast<int>(Action::replace_selected_text) == ACCESSKIT_ACTION_REPLACE_SELECTED_TEXT &&
              static_cast<int>(Action::scroll_into_view) == ACCESSKIT_ACTION_SCROLL_INTO_VIEW &&
              static_cast<int>(Action::set_scroll_offset) == ACCESSKIT_ACTION_SET_SCROLL_OFFSET &&
              static_cast<int>(Action::set_value) == ACCESSKIT_ACTION_SET_VALUE &&
              static_cast<int>(Action::show_context_menu) == ACCESSKIT_ACTION_SHOW_CONTEXT_MENU &&
              action_count == ACCESSKIT_ACTION_SHOW_CONTEXT_MENU + 1);

void set_text(accesskit_node* n, void (*set)(accesskit_node*, const char*, std::size_t), std::string_view text) {
    if (!text.empty()) set(n, text.data(), text.size());
}

// "dismiss" → "Dismiss": what a screen reader lists.
std::string spoken(std::string_view action) {
    std::string out(action);
    std::ranges::replace(out, '_', ' ');
    if (!out.empty() && out[0] >= 'a' && out[0] <= 'z') out[0] = static_cast<char>(out[0] - 'a' + 'A');
    return out;
}

accesskit_node* make(const Node& d, float scale, bool root) {
    accesskit_node* n = accesskit_node_new(*role_of(d.role));
    set_text(n, accesskit_node_set_label_with_length, d.label);
    set_text(n, accesskit_node_set_description_with_length, d.description);
    set_text(n, accesskit_node_set_value_with_length, d.value);
    set_text(n, accesskit_node_set_url_with_length, d.url);
    if (d.number) accesskit_node_set_numeric_value(n, *d.number);
    if (d.min) accesskit_node_set_min_numeric_value(n, *d.min);
    if (d.max) accesskit_node_set_max_numeric_value(n, *d.max);
    if (d.step) accesskit_node_set_numeric_value_step(n, *d.step);
    if (d.scroll_x) {
        accesskit_node_set_scroll_x(n, *d.scroll_x);
        accesskit_node_set_scroll_x_min(n, 0);
    }
    if (d.scroll_x_max) accesskit_node_set_scroll_x_max(n, *d.scroll_x_max);
    if (d.scroll_y) {
        accesskit_node_set_scroll_y(n, *d.scroll_y);
        accesskit_node_set_scroll_y_min(n, 0);
    }
    if (d.scroll_y_max) accesskit_node_set_scroll_y_max(n, *d.scroll_y_max);
    if (d.level) accesskit_node_set_level(n, *d.level);
    if (d.selected) accesskit_node_set_selected(n, *d.selected);
    if (d.expanded) accesskit_node_set_expanded(n, *d.expanded);
    switch (d.toggled) {
        case Toggled::none: break;
        case Toggled::off: accesskit_node_set_toggled(n, ACCESSKIT_TOGGLED_FALSE); break;
        case Toggled::on: accesskit_node_set_toggled(n, ACCESSKIT_TOGGLED_TRUE); break;
        case Toggled::mixed: accesskit_node_set_toggled(n, ACCESSKIT_TOGGLED_MIXED); break;
    }
    switch (d.live) {
        case Live::off: break;
        case Live::polite: accesskit_node_set_live(n, ACCESSKIT_LIVE_POLITE); break;
        case Live::assertive: accesskit_node_set_live(n, ACCESSKIT_LIVE_ASSERTIVE); break;
    }
    if (d.disabled) accesskit_node_set_disabled(n);
    if (d.readonly) accesskit_node_set_read_only(n);
    if (d.required) accesskit_node_set_required(n);
    if (d.invalid) accesskit_node_set_invalid(n, ACCESSKIT_INVALID_TRUE);
    if (d.busy) accesskit_node_set_busy(n);
    if (d.hidden) accesskit_node_set_hidden(n);
    if (d.modal) accesskit_node_set_modal(n);
    if (d.multiselectable) accesskit_node_set_multiselectable(n);
    if (d.visited) accesskit_node_set_visited(n);
    for (std::size_t a = 0; a < action_count; ++a)
        if (a != static_cast<std::size_t>(Action::custom) && d.has(static_cast<Action>(a)))
            accesskit_node_add_action(n, static_cast<accesskit_action>(a));
    for (std::size_t i = 0; i < d.custom.size(); ++i) {
        accesskit_custom_action* c = accesskit_custom_action_new(static_cast<std::int32_t>(i));
        const std::string said = spoken(d.custom[i]);
        accesskit_custom_action_set_description_with_length(c, said.data(), said.size());
        accesskit_node_push_custom_action(n, c);
    }
    if (d.bounds)
        accesskit_node_set_bounds(n, accesskit_rect{d.bounds->x, d.bounds->y, d.bounds->x + d.bounds->width,
                                                    d.bounds->y + d.bounds->height});
    // Bounds are logical pixels; AccessKit's are the window's physical ones.
    if (root && scale != 1) accesskit_node_set_transform(n, accesskit_affine{{scale, 0, 0, scale, 0, 0}});
    if (!d.children.empty()) accesskit_node_set_children(n, d.children.size(), d.children.data());
    return n;
}

// The whole tree as one update (any thread).
accesskit_tree_update* whole(const Mirror& mirror, const AdapterOptions& options, float s) {
    const std::uint64_t root = mirror.root();
    accesskit_tree_update* u = accesskit_tree_update_with_capacity_and_focus(mirror.size(), mirror.focus());
    bool has_root = false;
    mirror.each([&](const Node& d) {
        has_root = has_root || d.key == root;
        accesskit_tree_update_push_node(u, d.key, make(d, s, d.key == root));
    });
    if (!has_root) {  // nothing read yet: the window alone
        accesskit_tree_update_free(u);
        constexpr std::uint64_t window = 1;
        u = accesskit_tree_update_with_focus(window);
        accesskit_tree_update_push_node(u, window, accesskit_node_new(ACCESSKIT_ROLE_WINDOW));
        accesskit_tree_update_set_tree_info(u, accesskit_tree_info_new(window));
        return u;
    }
    accesskit_tree_info* t = accesskit_tree_info_new(root);
    accesskit_tree_info_set_toolkit_name_with_length(t, options.toolkit.data(), options.toolkit.size());
    accesskit_tree_info_set_toolkit_version_with_length(t, options.toolkit_version.data(), options.toolkit_version.size());
    accesskit_tree_update_set_tree_info(u, t);
    return u;
}

}  // namespace

std::string_view service() noexcept { return native::name(); }

struct Adapter::Impl {
    Mirror&                 mirror;
    AdapterOptions          options;
    native::Service*        service = nullptr;
    std::atomic<float>      scale{1};
    std::atomic<bool>       active{false};
    Mirror::Changes         pending;   // what `update` tells the platform
    bool                    rescaled = false;

    Impl(Mirror& m, AdapterOptions o) : mirror(m), options(std::move(o)), scale(options.scale) {}

    // Everything (any thread).
    accesskit_tree_update* full() const { return whole(mirror, options, scale.load()); }

    // What changed (the runtime's thread, inside `update`).
    accesskit_tree_update* changes() {
        const float s = scale.load();
        const std::uint64_t root = mirror.root();
        const std::uint64_t focus = pending.focus ? pending.focus : mirror.focus();
        accesskit_tree_update* u = accesskit_tree_update_with_capacity_and_focus(pending.nodes.size() + 1, focus);
        bool root_sent = false;
        for (const Node& d : pending.nodes) {
            root_sent = root_sent || d.key == root;
            accesskit_tree_update_push_node(u, d.key, make(d, s, d.key == root));
        }
        if (rescaled && !root_sent)
            mirror.each([&](const Node& d) {
                if (d.key == root) accesskit_tree_update_push_node(u, d.key, make(d, s, true));
            });
        rescaled = false;
        return u;
    }

    static accesskit_tree_update* on_activate(void* self) {
        auto* impl = static_cast<Impl*>(self);
        impl->active = true;
        log_.info("assistive technology asked for the tree ({})", native::name());
        return impl->full();
    }
    static accesskit_tree_update* on_update(void* self) { return static_cast<Impl*>(self)->changes(); }
    static void on_deactivate(void* self) {
        static_cast<Impl*>(self)->active = false;
        log_.info("assistive technology let the tree go");
    }
    static void on_action(accesskit_action_request* request, void* self) {
        auto* impl = static_cast<Impl*>(self);
        Request r;
        r.target = request->target_node;
        r.action = static_cast<Action>(request->action);
        if (request->data.has_value) {
            const accesskit_action_data& data = request->data.value;
            switch (data.tag) {
                case ACCESSKIT_ACTION_DATA_CUSTOM_ACTION: r.custom = data.custom_action; break;
                case ACCESSKIT_ACTION_DATA_VALUE:
                    if (data.value) r.text = std::string(data.value);
                    break;
                case ACCESSKIT_ACTION_DATA_NUMERIC_VALUE: r.number = data.numeric_value; break;
                case ACCESSKIT_ACTION_DATA_SCROLL_UNIT: r.page = data.scroll_unit == ACCESSKIT_SCROLL_UNIT_PAGE; break;
                case ACCESSKIT_ACTION_DATA_SET_SCROLL_OFFSET:
                    r.point = Point{static_cast<float>(data.set_scroll_offset.x), static_cast<float>(data.set_scroll_offset.y)};
                    break;
                default: break;
            }
        }
        accesskit_action_request_free(request);
        impl->mirror.queue(std::move(r));
    }
};

std::expected<std::unique_ptr<Adapter>, std::string> Adapter::attach(Mirror& mirror, void* window, AdapterOptions options) {
    trace::Span span("accessibility.attach", "accessibility");
    auto impl = std::make_unique<Impl>(mirror, std::move(options));
    (void)mirror.refresh();  // the activation that may come at once reads it
    native::Handlers h{&Impl::on_activate, &Impl::on_action, &Impl::on_deactivate, impl.get()};
    auto service = native::open(window, h);
    if (!service) return std::unexpected(service.error());
    impl->service = *service;
    log_.info("serving {} nodes to {}", mirror.size(), native::name());
    return std::unique_ptr<Adapter>(new Adapter(std::move(impl)));
}

Adapter::Adapter(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Adapter::~Adapter() {
    if (impl_) native::close(impl_->service);
}

void Adapter::update() {
    trace::Span span("accessibility.update", "accessibility");
    impl_->pending = impl_->mirror.refresh();
    if (impl_->pending.empty() && !impl_->rescaled) return;
    native::update(impl_->service, &Impl::on_update, impl_.get());
    impl_->pending = {};
}

void Adapter::window_focus(bool focused) { native::focus(impl_->service, focused); }

void Adapter::set_scale(float scale) {
    if (impl_->scale.exchange(scale) != scale) impl_->rescaled = true;
}

void Adapter::window_bounds(Rect outer, Rect inner) {
    const double s = impl_->scale.load();
    auto rect = [s](Rect r) { return accesskit_rect{r.x * s, r.y * s, (r.x + r.width) * s, (r.y + r.height) * s}; };
    native::bounds(impl_->service, rect(outer), rect(inner));
}

bool Adapter::active() const noexcept { return impl_->active.load(); }

std::string debug(const Mirror& mirror, const AdapterOptions& options) {
    accesskit_tree_update* u = whole(mirror, options, options.scale);
    char* text = accesskit_tree_update_debug(u);
    std::string out = text ? text : "";
    accesskit_string_free(text);
    accesskit_tree_update_free(u);
    return out;
}

}  // namespace omui::accessibility
