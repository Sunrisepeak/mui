module mlibs.compose;

import std;
import mlibs.error;

namespace mlibs::compose {

namespace {

struct Entry {
    const void* service = nullptr;
    std::string provider;
};

struct Record {
    SlotInfo           info;
    std::vector<Entry> entries;
    Absence            absence = Absence::unsaid;
};

}  // namespace

struct Composition::State {
    mutable std::shared_mutex                  mutex;
    std::atomic<bool>                          frozen{false};
    mutable std::map<std::string, Record, std::less<>> slots;

    // Read under the lock until frozen; a frozen composition changes no more.
    template <class Read>
    auto read(Read&& body) const {
        if (frozen.load(std::memory_order_acquire)) return body();
        std::shared_lock lock(mutex);
        return body();
    }
    [[nodiscard]] const Record* find(std::string_view id) const {
        const auto found = slots.find(id);
        return found == slots.end() ? nullptr : &found->second;
    }
    Record& record(const SlotInfo& slot) {
        Record& made = slots[std::string(slot.id)];
        made.info = slot;
        return made;
    }
};

Composition::Composition() : state_(std::make_unique<State>()) {}
Composition::~Composition() = default;

std::expected<void, Error> Composition::install_erased(const SlotInfo& slot, const void* service, std::string_view provider) {
    std::unique_lock lock(state_->mutex);
    if (state_->frozen.load(std::memory_order_acquire))
        return std::unexpected(Error{Errc::frozen, std::format("`{}` was not installed at {}", provider, slot.id), std::string(slot.id)});
    Record& record = state_->record(slot);
    if (slot.cardinality == Cardinality::one && !record.entries.empty())
        return std::unexpected(Error{Errc::occupied,
                                     std::format("{} has `{}`; `{}` was not installed", slot.id, record.entries.front().provider, provider),
                                     std::string(slot.id)});
    record.entries.push_back({service, std::string(provider)});
    return {};
}

const void* Composition::first(const SlotInfo& slot) const noexcept {
    return state_->read([&]() -> const void* {
        const Record* record = state_->find(slot.id);
        return record && !record->entries.empty() ? record->entries.front().service : nullptr;
    });
}

std::vector<const void*> Composition::every(const SlotInfo& slot) const {
    return state_->read([&] {
        std::vector<const void*> out;
        if (const Record* record = state_->find(slot.id))
            for (const Entry& entry : record->entries) out.push_back(entry.service);
        return out;
    });
}

std::string_view Composition::provider(const SlotInfo& slot) const noexcept {
    return state_->read([&]() -> std::string_view {
        const Record* record = state_->find(slot.id);
        return record && !record->entries.empty() ? std::string_view(record->entries.front().provider) : std::string_view();
    });
}

void Composition::leave_out(const SlotInfo& slot, Absence why) {
    std::unique_lock lock(state_->mutex);
    if (state_->frozen.load(std::memory_order_acquire)) return;
    state_->record(slot).absence = why;
}

Error Composition::missing(const SlotInfo& slot) const {
    Absence why = Absence::unsaid;
    if (!state_->frozen.load(std::memory_order_acquire)) {
        std::unique_lock lock(state_->mutex);
        why = state_->record(slot).absence;  // asked for and missing: `describe` says so
    } else if (const Record* record = state_->find(slot.id)) {
        why = record->absence;
    }
    std::string message = std::format("no provider of {}", slot.id);
    if (why == Absence::left_out) message += " (this program was built without one)";
    if (why == Absence::platform) message += " (this platform has none)";
    if (!slot.absent.empty() && why != Absence::platform) message += std::format(": {}", slot.absent);
    return Error{Errc::absent, std::move(message), std::string(slot.id)};
}

void Composition::freeze() noexcept {
    std::unique_lock lock(state_->mutex);
    state_->frozen.store(true, std::memory_order_release);
}

bool Composition::frozen() const noexcept { return state_->frozen.load(std::memory_order_acquire); }

std::vector<Described> Composition::describe() const {
    return state_->read([&] {
        std::vector<Described> out;
        for (const auto& [id, record] : state_->slots) {
            Described described{.slot = id,
                                .version = record.info.version,
                                .cardinality = record.info.cardinality,
                                .summary = std::string(record.info.summary),
                                .providers = {},
                                .absence = record.absence,
                                .way = {}};
            for (const Entry& entry : record.entries) described.providers.push_back(entry.provider);
            if (described.providers.empty()) described.way = std::string(record.info.absent);
            out.push_back(std::move(described));
        }
        return out;
    });
}

Composition& process() noexcept {
    static Composition composition;
    return composition;
}

}  // namespace mlibs::compose
