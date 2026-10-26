module openmui.inspector;

import std;
import openmui.core;
import openmui.view;
import openmui.agent;

namespace omui::inspector {

// One connection to one application: requests take turns on its client.
struct Session {
    agent::Client client;
    std::string   app;
    std::mutex    call;  // one request at a time on the client
};

struct Connection::Impl {
    Runtime*                    ui;
    std::filesystem::path       directory;
    // Which session is current: held only to read or swap the pointer, never
    // across a request, so the UI thread never waits for the other process.
    mutable std::mutex          state;
    std::shared_ptr<Session>    session;
    std::atomic<std::size_t>    in_flight{0};
    std::atomic<bool>           closing{false};
    std::mutex                  idle_lock;
    std::condition_variable     idle;
};

Connection::Connection(Runtime& ui, std::filesystem::path directory) : impl_(std::make_shared<Impl>()) {
    impl_->ui = &ui;
    impl_->directory = std::move(directory);
}

Connection::~Connection() {
    impl_->closing = true;
    std::unique_lock g(impl_->idle_lock);
    impl_->idle.wait(g, [&] { return impl_->in_flight.load() == 0; });
}

std::vector<agent::EndpointInfo> Connection::applications() const { return agent::list(impl_->directory); }
const std::filesystem::path& Connection::directory() const noexcept { return impl_->directory; }

std::expected<void, std::string> Connection::open(std::string_view app) {
    auto client = agent::Client::connect(app, impl_->directory);
    if (!client) return std::unexpected(client.error());
    auto s = std::make_shared<Session>(std::move(*client), std::string(app));
    std::scoped_lock g(impl_->state);
    impl_->session = std::move(s);  // requests in flight finish on the old one
    return {};
}

void Connection::close() {
    std::scoped_lock g(impl_->state);
    impl_->session.reset();
}

bool Connection::connected() const noexcept {
    std::scoped_lock g(impl_->state);
    return impl_->session != nullptr;
}

std::string Connection::app() const {
    std::scoped_lock g(impl_->state);
    return impl_->session ? impl_->session->app : std::string();
}

std::size_t Connection::in_flight() const noexcept { return impl_->in_flight.load(); }

void Connection::call(Value request, Reply reply, std::chrono::milliseconds timeout) {
    ++impl_->in_flight;
    std::shared_ptr<Impl> d = impl_;
    (void)Tasks::shared().spawn([d, request = std::move(request), reply = std::move(reply), timeout](std::stop_token) mutable {
        std::vector<Value> records;
        std::shared_ptr<Session> s;
        {
            std::scoped_lock g(d->state);
            s = d->session;
        }
        {
            if (s) {
                std::scoped_lock g(s->call);
                records = s->client.call(request, timeout);
            } else {
                Value e;
                e.set("kind", "error").set("code", "not-connected").set("message", "no application is connected");
                records.push_back(std::move(e));
            }
        }
        if (!d->closing) {
            d->ui->queue().post([d, reply = std::move(reply), records = std::move(records)]() mutable {
                if (!d->closing) reply(std::move(records));
            });
        }
        {
            std::scoped_lock g(d->idle_lock);
            --d->in_flight;
        }
        d->idle.notify_all();
    });
}

const Value* data_of(const std::vector<Value>& records) noexcept {
    for (const Value& r : records)
        if (const Value* k = r.get("kind"); k && k->as_string() == "data") return r.get("data");
    return nullptr;
}

bool succeeded(const std::vector<Value>& records) noexcept {
    for (const Value& r : records)
        if (const Value* k = r.get("kind"); k && k->as_string() == "result")
            return r.get("exitCode") && r.get("exitCode")->as_int(1) == 0;
    return false;
}

std::string error_of(const std::vector<Value>& records) {
    for (const Value& r : records)
        if (const Value* k = r.get("kind"); k && k->as_string() == "error")
            return std::string(r.get("message") ? r.get("message")->as_string() : "an error");
    return succeeded(records) ? std::string() : std::string("no answer");
}

}  // namespace omui::inspector
