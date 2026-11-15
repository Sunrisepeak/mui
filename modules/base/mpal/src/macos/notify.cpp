// The notification centre of macOS (mpal:native): UserNotifications, spoken
// through mcpplibs.os's Objective-C runtime (macos::objc) — no SDK header.
// Built for macOS alone (the manifest). The centre serves an application with
// a bundle identifier (an .app); a bare program is told so (unavailable). The
// person is asked once whether notifications are allowed (the system
// remembers); a refusal is said with the system's reason.
module mpal;

import std;
import mcpplibs.os;
import :native;

namespace mpal::native {

namespace objc = mcpplibs::os::macos::objc;

namespace {

constexpr unsigned long asked_options = (1UL << 1) | (1UL << 2);              // UNAuthorizationOptionSound | Alert
constexpr unsigned long presented = (1UL << 1) | (1UL << 3) | (1UL << 4);     // UNNotificationPresentationOptionSound | List | Banner
constexpr unsigned long custom_dismiss = 1UL << 0;                            // UNNotificationCategoryOptionCustomDismissAction
constexpr std::string_view default_action = "com.apple.UNNotificationDefaultActionIdentifier";
constexpr std::string_view dismiss_action = "com.apple.UNNotificationDismissActionIdentifier";
constexpr auto answer_wait = std::chrono::seconds(10);

}  // namespace

class Centre {
public:
    objc::id                                           centre, delegate;
    std::mutex                                         mutex;
    std::condition_variable                            changed;
    bool                                               asked = false, granted = false;
    std::string                                        refusal;
    std::deque<Answer>                                 answers;
    std::uint32_t                                      last = 0;
    std::map<std::string, std::uint32_t, std::less<>>  ids;         // a request's identifier → its id
    std::vector<objc::id>                              categories;  // each notification's choices, all registered

    ~Centre();
};

namespace {

// The centre a delegate answers for.
std::mutex                                     routes_guard;
std::map<void*, std::weak_ptr<Centre>>         routes;

std::shared_ptr<Centre> route(void* delegate) {
    const std::scoped_lock lock(routes_guard);
    const auto found = routes.find(delegate);
    return found == routes.end() ? nullptr : found->second.lock();
}

// A block the system gave, called as its signature says.
template <class... A>
void call_block(void* block, A... args) {
    if (!block) return;
    const auto* literal = static_cast<const objc::detail::block_literal*>(block);
    reinterpret_cast<void (*)(void*, A...)>(literal->invoke)(block, args...);
}

void push(Centre& centre, Answer answer) {
    {
        const std::scoped_lock lock(centre.mutex);
        centre.answers.push_back(std::move(answer));
    }
    centre.changed.notify_all();
}

// userNotificationCenter:didReceiveNotificationResponse:withCompletionHandler:
void received(void* self, void*, void*, void* response, void* completion) {
    if (auto centre = route(self)) {
        void* request = objc::send<void*>(objc::send<void*>(response, "notification"), "request");
        const std::string identifier = objc::utf8(objc::send<void*>(request, "identifier"));
        const std::string action = objc::utf8(objc::send<void*>(response, "actionIdentifier"));
        std::uint32_t id = 0;
        {
            const std::scoped_lock lock(centre->mutex);
            if (const auto found = centre->ids.find(identifier); found != centre->ids.end()) id = found->second;
        }
        if (id != 0) {
            if (action == dismiss_action) push(*centre, {id, Answer::Kind::dismissed, {}});
            else push(*centre, {id, Answer::Kind::chosen, action == default_action ? std::string("default") : action});
        }
    }
    call_block(completion);
}

// userNotificationCenter:willPresentNotification:withCompletionHandler: — shown while the application is in front too.
void in_front(void*, void*, void*, void*, void* completion) { call_block(completion, presented); }

void* delegate_class() {
    static void* const made = []() -> void* {
        if (void* existing = objc::class_named("MpalNotificationDelegate")) return existing;
        void (*on_response)(void*, void*, void*, void*, void*) = &received;
        void (*on_front)(void*, void*, void*, void*, void*) = &in_front;
        const std::array methods{
            objc::method{"userNotificationCenter:didReceiveNotificationResponse:withCompletionHandler:", reinterpret_cast<void*>(on_response), "v@:@@@?"},
            objc::method{"userNotificationCenter:willPresentNotification:withCompletionHandler:", reinterpret_cast<void*>(on_front), "v@:@@@?"}};
        const std::array<std::string_view, 1> protocols{"UNUserNotificationCenterDelegate"};
        auto defined = objc::define_class("MpalNotificationDelegate", "NSObject", methods, protocols);
        return defined ? *defined : nullptr;
    }();
    return made;
}

// An NSArray of these objects (autoreleased).
void* array_of(const std::vector<void*>& objects) {
    return objc::send<void*>(objc::class_named("NSArray"), "arrayWithObjects:count:", objects.data(), static_cast<unsigned long>(objects.size()));
}

std::string description_of(void* error) { return error ? objc::utf8(objc::send<void*>(error, "localizedDescription")) : std::string(); }

}  // namespace

Centre::~Centre() {
    {
        const std::scoped_lock lock(routes_guard);
        routes.erase(delegate.raw());
    }
    if (centre && delegate && objc::send<void*>(centre, "delegate") == delegate.raw()) objc::send(centre, "setDelegate:", static_cast<void*>(nullptr));
}

std::expected<std::shared_ptr<Centre>, Error> open() {
    if (auto here = objc::available(); !here) return failure(Error::Code::unsupported, here.error().message());
    const objc::autorelease_pool pool;
    void* bundle = objc::send<void*>(objc::class_named("NSBundle"), "mainBundle");
    if (!objc::send<void*>(bundle, "bundleIdentifier"))
        return failure(Error::Code::unavailable, "macOS shows notifications for an application with a bundle identifier (an .app); this program has none");
    if (auto loaded = objc::load_framework("UserNotifications"); !loaded) return failure(Error::Code::unavailable, loaded.error().message());
    void* kind = objc::class_named("UNUserNotificationCenter");
    void* delegate_kind = delegate_class();
    if (!kind || !delegate_kind) return failure(Error::Code::unavailable, "the notification centre's classes are not here");

    auto made = std::make_shared<Centre>();
    made->centre = objc::id::borrow(objc::send<void*>(kind, "currentNotificationCenter"));
    made->delegate = objc::id::adopt(objc::send<void*>(objc::send<void*>(delegate_kind, "alloc"), "init"));
    {
        const std::scoped_lock lock(routes_guard);
        routes[made->delegate.raw()] = made;
    }
    objc::send(made->centre, "setDelegate:", made->delegate.raw());
    // Whether notifications are allowed: asked once, the answer arriving on the centre's queue.
    const std::weak_ptr<Centre> weak = made;
    const objc::id answered = objc::block(std::function<void(signed char, void*)>([weak](signed char granted, void* error) {
        auto held = weak.lock();
        if (!held) return;
        const std::string reason = description_of(error);
        {
            const std::scoped_lock lock(held->mutex);
            held->asked = true;
            held->granted = granted != 0;
            if (!held->granted) held->refusal = reason.empty() ? std::string("notifications are not allowed for this application") : reason;
        }
        held->changed.notify_all();
    }));
    objc::send(made->centre, "requestAuthorizationWithOptions:completionHandler:", asked_options, answered.raw());
    return made;
}

std::expected<std::uint32_t, Error> show(Centre& centre, const Notification& notification) {
    std::uint32_t id = 0;
    std::string identifier;
    {
        std::unique_lock lock(centre.mutex);
        if (!centre.changed.wait_for(lock, answer_wait, [&] { return centre.asked; }))
            return failure(Error::Code::unavailable, "the notification centre did not say whether notifications are allowed");
        if (!centre.granted) return failure(Error::Code::refused, centre.refusal);
        id = ++centre.last;
        identifier = std::format("mpal.{}", id);
        centre.ids[identifier] = id;
    }
    const objc::autorelease_pool pool;
    // Its choices: a category of its own, which also asks to hear the person's dismissal.
    std::vector<objc::id> actions;
    for (const auto& [key, label] : notification.actions)
        actions.push_back(objc::id::borrow(objc::send<void*>(objc::class_named("UNNotificationAction"), "actionWithIdentifier:title:options:",
                                                             objc::string(key).raw(), objc::string(label).raw(), 0UL)));
    std::vector<void*> action_objects;
    for (const objc::id& action : actions) action_objects.push_back(action.raw());
    const objc::id category = objc::id::borrow(objc::send<void*>(
        objc::class_named("UNNotificationCategory"), "categoryWithIdentifier:actions:intentIdentifiers:options:", objc::string(identifier).raw(),
        array_of(action_objects), objc::send<void*>(objc::class_named("NSArray"), "array"), custom_dismiss));
    std::vector<void*> category_objects;
    {
        const std::scoped_lock lock(centre.mutex);
        centre.categories.push_back(category);
        for (const objc::id& each : centre.categories) category_objects.push_back(each.raw());
    }
    objc::send(centre.centre, "setNotificationCategories:", objc::send<void*>(objc::class_named("NSSet"), "setWithArray:", array_of(category_objects)));

    const objc::id content = objc::id::adopt(objc::send<void*>(objc::send<void*>(objc::class_named("UNMutableNotificationContent"), "alloc"), "init"));
    objc::send(content, "setTitle:", objc::string(notification.title).raw());
    objc::send(content, "setBody:", objc::string(notification.body).raw());
    objc::send(content, "setCategoryIdentifier:", objc::string(identifier).raw());
    objc::send(content, "setSound:", objc::send<void*>(objc::class_named("UNNotificationSound"), "defaultSound"));
    void* request = objc::send<void*>(objc::class_named("UNNotificationRequest"), "requestWithIdentifier:content:trigger:", objc::string(identifier).raw(),
                                      content.raw(), static_cast<void*>(nullptr));

    // Added, or why not: the centre answers on its queue.
    struct Added {
        std::mutex              mutex;
        std::condition_variable done;
        bool                    answered = false;
        std::string             error;
    };
    const auto added = std::make_shared<Added>();
    const objc::id finished = objc::block(std::function<void(void*)>([added](void* error) {
        const std::string reason = description_of(error);
        {
            const std::scoped_lock lock(added->mutex);
            added->answered = true;
            added->error = reason;
        }
        added->done.notify_all();
    }));
    objc::send(centre.centre, "addNotificationRequest:withCompletionHandler:", request, finished.raw());
    std::unique_lock lock(added->mutex);
    if (!added->done.wait_for(lock, answer_wait, [&] { return added->answered; }))
        return failure(Error::Code::failed, "the notification centre did not answer whether it added the notification");
    if (!added->error.empty()) return failure(Error::Code::refused, added->error);
    return id;
}

std::expected<void, Error> close(Centre& centre, std::uint32_t id) {
    const objc::autorelease_pool pool;
    const objc::id identifier = objc::string(std::format("mpal.{}", id));
    void* one = array_of({identifier.raw()});
    objc::send(centre.centre, "removeDeliveredNotificationsWithIdentifiers:", one);
    objc::send(centre.centre, "removePendingNotificationRequestsWithIdentifiers:", one);
    push(centre, {id, Answer::Kind::closed, {}});
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
