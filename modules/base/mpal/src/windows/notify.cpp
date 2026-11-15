// The toasts of Windows (mpal:native): the Windows Runtime's
// ToastNotificationManager, spoken through mcpplibs.os's COM (windows::com) —
// no SDK header. Built for Windows alone (the manifest). A program that is not
// packaged is named to the system by a Start menu shortcut carrying its
// application identity (made when missing, as the shell's own API does it);
// the person's choice and dismissal come back through the toast's events.
module mpal;

import std;
import mcpplibs.os;
import :native;

namespace mpal::native {

namespace com = mcpplibs::os::windows::com;
namespace os = mcpplibs::os;

namespace {

// The Windows Runtime's interfaces, as the system's metadata names them.
constexpr auto manager_statics = com::id("50AC103F-D235-4598-BBEF-98FE4D1A3AD4");    // IToastNotificationManagerStatics
constexpr auto toast_factory = com::id("04124B20-82C6-4229-B109-FD9ED4662B53");      // IToastNotificationFactory
constexpr auto xml_document = com::id("F7F3A506-1E87-42D6-BCFB-B8C809FA5494");       // IXmlDocument
constexpr auto xml_io = com::id("6CD0E74E-EE65-4489-9EBF-CA43E87BA637");             // IXmlDocumentIO
constexpr auto activated_handler = com::id("AB54DE2D-97D9-5528-B6AD-105AFE156530");  // TypedEventHandler<ToastNotification, Object>
constexpr auto dismissed_handler = com::id("61C2402F-0ED0-5A18-AB69-59F4AA99A368");  // TypedEventHandler<ToastNotification, ToastDismissedEventArgs>
constexpr auto activated_args = com::id("E3BF92F3-C197-436F-8265-0625824F8DAC");     // IToastActivatedEventArgs
constexpr auto dismissed_args = com::id("3F89D935-D9CB-4538-A0F0-22F0A3B9D2E5");     // IToastDismissedEventArgs
// The shell's link and what it is saved with.
constexpr auto shell_link = com::id("00021401-0000-0000-C000-000000000046");         // CLSID_ShellLink
constexpr auto shell_link_wide = com::id("000214F9-0000-0000-C000-000000000046");    // IShellLinkW
constexpr auto property_store = com::id("886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99");     // IPropertyStore
constexpr auto persist_file = com::id("0000010B-0000-0000-C000-000000000046");       // IPersistFile

// Slots past IInspectable's six (the Windows Runtime) or IUnknown's three (the shell).
constexpr std::size_t create_notifier_with_id = 7, load_xml = 6, create_toast = 6, add_dismissed = 9, add_activated = 11, show_toast = 6,
                      hide_toast = 7, arguments = 6, reason = 6;
constexpr std::size_t set_path = 20, set_value = 6, commit = 7, save = 6;

// PKEY_AppUserModel_ID, and a PROPVARIANT holding a wide string (VT_LPWSTR), as 64-bit Windows lays them out.
struct property_key {
    com::guid     format;
    std::uint32_t id;
};
constexpr property_key app_user_model_id{com::id("9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3"), 5};
struct wide_variant {
    std::uint16_t   type = 31, reserved1 = 0, reserved2 = 0, reserved3 = 0;
    const char16_t* text = nullptr;
    void*           unused = nullptr;
};

std::u16string wide(std::string_view utf8) { return os::fs::from_utf8(utf8).u16string(); }

std::string escaped(std::string_view text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

// The application's identity: openmui.<name>, its letters, digits and dots.
std::string identity_of(std::string_view application) {
    std::string out = "openmui.";
    for (const char c : application) out.push_back(std::isalnum(static_cast<unsigned char>(c)) || c == '.' ? c : '-');
    return out;
}

// The shortcut that names this program to the system with `identity` (made when missing).
std::expected<void, Error> named_by_shortcut(std::string_view application, const std::string& identity) {
    const auto roaming = os::env::get("APPDATA");
    const auto program = os::env::image();
    if (!roaming || roaming->empty() || !program) return failure(Error::Code::unavailable, "no Start menu to name the application in (APPDATA)");
    const std::filesystem::path link =
        os::fs::from_utf8(*roaming) / "Microsoft" / "Windows" / "Start Menu" / "Programs" / os::fs::from_utf8(std::format("{}.lnk", application));
    std::error_code ignored;
    if (std::filesystem::exists(link, ignored)) return {};
    auto made = com::create(shell_link, shell_link_wide);
    if (!made) return failure(Error::Code::failed, made.error().message());
    const std::u16string target = program->u16string();
    if (auto set = made->call(set_path, target.c_str()); !set) return failure(Error::Code::failed, set.error().message());
    auto store = made->query(property_store);
    if (!store) return failure(Error::Code::failed, store.error().message());
    const std::u16string identity_text = wide(identity);
    const wide_variant value{.text = identity_text.c_str()};
    if (auto set = store->call(set_value, &app_user_model_id, &value); !set) return failure(Error::Code::failed, set.error().message());
    if (auto committed = store->call(commit); !committed) return failure(Error::Code::failed, committed.error().message());
    auto file = made->query(persist_file);
    if (!file) return failure(Error::Code::failed, file.error().message());
    std::filesystem::create_directories(link.parent_path(), ignored);
    const std::u16string where = link.u16string();
    if (auto saved = file->call(save, where.c_str(), 1); !saved) return failure(Error::Code::failed, saved.error().message());
    return {};
}

}  // namespace

class Centre : public std::enable_shared_from_this<Centre> {
public:
    std::optional<com::apartment>        apartment;
    com::object                          statics;    // IToastNotificationManagerStatics
    std::map<std::string, com::object>   notifiers;  // by application identity
    std::mutex                           mutex;
    std::condition_variable              changed;
    std::deque<Answer>                   answers;
    std::uint32_t                        last = 0;
    struct Shown {
        com::object toast, notifier, activated, dismissed;
    };
    std::map<std::uint32_t, Shown>       shown;
};

namespace {

void push(Centre& centre, Answer answer) {
    {
        const std::scoped_lock lock(centre.mutex);
        if (answer.kind != Answer::Kind::chosen) centre.shown.erase(answer.id);
        centre.answers.push_back(std::move(answer));
    }
    centre.changed.notify_all();
}

}  // namespace

std::expected<std::shared_ptr<Centre>, Error> open() {
    auto made = std::make_shared<Centre>();
    // The calling thread in the multi-threaded apartment; a thread already in another stays there.
    if (auto entered = com::apartment::enter(com::apartment::kind::multi_threaded)) made->apartment.emplace(std::move(*entered));
    auto statics = com::factory("Windows.UI.Notifications.ToastNotificationManager", manager_statics);
    if (!statics) return failure(Error::Code::unavailable, std::format("no toasts here: {}", statics.error().message()));
    made->statics = std::move(*statics);
    return made;
}

std::expected<std::uint32_t, Error> show(Centre& centre, const Notification& notification) {
    const std::string application = notification.application.empty() ? std::string("openmui application") : notification.application;
    const std::string identity = identity_of(application);
    if (auto named = named_by_shortcut(application, identity); !named) return std::unexpected(named.error());

    com::object notifier;
    {
        const std::scoped_lock lock(centre.mutex);
        if (const auto found = centre.notifiers.find(identity); found != centre.notifiers.end()) notifier = found->second;
    }
    if (!notifier) {
        auto name = com::hstring::of(identity);
        if (!name) return failure(Error::Code::failed, name.error().message());
        void* made = nullptr;
        if (auto created = centre.statics.call(create_notifier_with_id, name->raw(), &made); !created)
            return failure(Error::Code::unavailable, created.error().message());
        notifier = com::object::adopt(made);
        const std::scoped_lock lock(centre.mutex);
        centre.notifiers[identity] = notifier;
    }

    // The toast's XML: its texts, and its choices (each activates with its key; the toast itself with "default").
    std::string xml = std::format(R"(<toast launch="default"><visual><binding template="ToastGeneric"><text>{}</text><text>{}</text></binding></visual>)",
                                  escaped(notification.title), escaped(notification.body));
    if (!notification.actions.empty()) {
        xml += "<actions>";
        for (const auto& [key, label] : notification.actions)
            xml += std::format(R"(<action content="{}" arguments="{}" activationType="foreground"/>)", escaped(label), escaped(key));
        xml += "</actions>";
    }
    xml += "</toast>";
    auto document = com::activate("Windows.Data.Xml.Dom.XmlDocument");
    if (!document) return failure(Error::Code::unavailable, document.error().message());
    auto loader = document->query(xml_io);
    auto text = com::hstring::of(xml);
    if (!loader || !text) return failure(Error::Code::failed, "the toast's XML could not be made");
    if (auto loaded = loader->call(load_xml, text->raw()); !loaded) return failure(Error::Code::failed, loaded.error().message());
    auto as_document = document->query(xml_document);
    auto factory = com::factory("Windows.UI.Notifications.ToastNotification", toast_factory);
    if (!as_document || !factory) return failure(Error::Code::unavailable, "no toast notifications here");
    void* made = nullptr;
    if (auto created = factory->call(create_toast, as_document->raw(), &made); !created) return failure(Error::Code::failed, created.error().message());
    const com::object toast = com::object::adopt(made);

    std::uint32_t id = 0;
    {
        const std::scoped_lock lock(centre.mutex);
        id = ++centre.last;
    }
    // Its events: the person's choice (the activation's arguments), its dismissal and why.
    // The system holds the handlers as long as the toast: they reach the centre only while it lives.
    const std::weak_ptr<Centre> owner = centre.weak_from_this();
    auto activated = com::delegate(activated_handler, [owner, id](const com::object&, const com::object& args) {
        const auto held = owner.lock();
        if (!held) return;
        std::string key = "default";
        if (args)
            if (auto given = args.query(activated_args)) {
                void* raw = nullptr;
                if (given->call(arguments, &raw)) key = com::hstring::adopt(raw).utf8();
            }
        push(*held, {id, Answer::Kind::chosen, key.empty() ? std::string("default") : key});
    });
    auto dismissed = com::delegate(dismissed_handler, [owner, id](const com::object&, const com::object& args) {
        const auto held = owner.lock();
        if (!held) return;
        std::int32_t why = 0;  // ToastDismissalReason: UserCanceled 0, ApplicationHidden 1, TimedOut 2
        if (args)
            if (auto given = args.query(dismissed_args)) (void)given->call(reason, &why);
        {
            const std::scoped_lock lock(held->mutex);
            if (!held->shown.contains(id)) return;  // closed by the application, said already
        }
        push(*held, {id, why == 2 ? Answer::Kind::expired : why == 1 ? Answer::Kind::closed : Answer::Kind::dismissed, {}});
    });
    if (!activated || !dismissed) return failure(Error::Code::failed, "the toast's events could not be heard");
    std::int64_t token = 0;
    (void)toast.call(add_activated, activated->raw(), &token);
    (void)toast.call(add_dismissed, dismissed->raw(), &token);
    {
        const std::scoped_lock lock(centre.mutex);
        centre.shown[id] = {toast, notifier, *activated, *dismissed};
    }
    if (auto shown = notifier.call(show_toast, toast.raw()); !shown) {
        const std::scoped_lock lock(centre.mutex);
        centre.shown.erase(id);
        return failure(Error::Code::refused, shown.error().message());
    }
    return id;
}

std::expected<void, Error> close(Centre& centre, std::uint32_t id) {
    Centre::Shown taken;
    {
        const std::scoped_lock lock(centre.mutex);
        const auto found = centre.shown.find(id);
        if (found == centre.shown.end()) return {};
        taken = found->second;
    }
    push(centre, {id, Answer::Kind::closed, {}});
    if (auto hidden = taken.notifier.call(hide_toast, taken.toast.raw()); !hidden) return failure(Error::Code::failed, hidden.error().message());
    return {};
}

std::expected<std::optional<Answer>, Error> next(Centre& centre, std::chrono::milliseconds wait) {
    std::unique_lock lock(centre.mutex);
    if (!centre.changed.wait_for(lock, wait, [&] { return !centre.answers.empty(); })) return std::optional<Answer>{};
    Answer answer = std::move(centre.answers.front());
    centre.answers.pop_front();
    return std::optional(std::move(answer));
}

}  // namespace mpal::native
