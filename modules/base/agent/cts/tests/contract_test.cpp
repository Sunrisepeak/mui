// The contract's page (mlibs.agent's CONTRACT.md) held to the code: the contract's name and version, every kind of
// record, exit code and error code openas defines, every capability a subject says it has, and every request the
// session names as its own, as a session over a subject answers them. A page that says less, or other, fails here.
import std;
import openas;
import mlibs.agent.cts;
import mlibs.control;
import mlibs.json;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace agent = mlibs::agent;
namespace control = mlibs::control;

namespace {

// A subject with nothing to it: what the session says of any subject.
class Bare final : public agent::Subject {
public:
    std::string_view name() const noexcept override { return "bare"; }
    std::string_view interface_name() const noexcept override { return "example.bare@1"; }
    agent::Capabilities capabilities() const noexcept override { return {}; }
    std::span<const control::CatalogEntry> operations() const noexcept override {
        static constexpr std::array<control::CatalogEntry, 1> all{{
            {{"bare.read", openas::Effect::read, false}, "Nothing", {}, "json", control::Cost::instant, "", false, false, false},
        }};
        return all;
    }
    void operate(std::string_view, const agent::Request&, agent::Answer& answer) override { answer.data("{}"); }
};

std::string page() {
    const auto file = std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path().parent_path() / "CONTRACT.md";
    std::ifstream in(file, std::ios::binary);
    expect(fatal(in.good())) << file.generic_string();
    return std::string(std::istreambuf_iterator<char>(in), {});
}

}  // namespace

UTest cases{"agent.contract.page", [] {
    "the page names the contract and its version, as the cases check it"_test = [] {
        expect(page().contains(std::format("`{}`", mlibs::agent::cts::contract))) << mlibs::agent::cts::contract;
    };

    "every kind of record, exit code and error code of openas's envelope is on the page"_test = [] {
        const std::string text = page();
        for (std::uint8_t kind = 0; kind <= static_cast<std::uint8_t>(openas::RecordKind::result); ++kind) {
            const auto name = openas::name(static_cast<openas::RecordKind>(kind));
            expect(text.contains(std::format("`{}`", name))) << "record kind " << name;
        }
        for (const int code : {openas::exit_code::done, openas::exit_code::failed, openas::exit_code::needs_answer, openas::exit_code::not_ready})
            expect(text.contains(std::format("| {} |", code))) << "exit code " << code;
        for (const std::string_view code : {openas::error_code::needs_answer, openas::error_code::refused, openas::error_code::not_found, openas::error_code::invalid_argument,
                                            openas::error_code::stale, openas::error_code::timeout, openas::error_code::failed, openas::error_code::internal})
            expect(text.contains(std::format("`{}`", code))) << "error code " << code;
    };

    "every capability a subject says, and every request the session names as its own, is on the page"_test = [] {
        Bare bare;
        agent::SubjectSession session({&bare});
        std::optional<mlibs::json::Document> data;
        session.request(R"({"op":"subjects"})", [&](std::string_view record) {
            auto parsed = mlibs::json::Document::parse(std::string(record), "record");
            if (parsed && parsed->root()["kind"].text() == "data") data.emplace(std::move(*parsed));
        });
        expect(fatal(data.has_value()));
        const std::string text = page();
        const auto capabilities = data->root()["data"]["subjects"][0]["capabilities"];
        expect(fatal(capabilities.is_object())) << capabilities.dump();
        std::size_t seen = 0;
        for (const auto& [name, value] : capabilities.members()) {
            ++seen;
            expect(text.contains(std::format("| `{}` |", name))) << "capability " << name;
        }
        expect(eq(seen, std::size_t{8})) << "the eight a subject says";
        expect(text.contains(R"(`{"op":"subjects"}`)")) << "subjects";
        const auto own = data->root()["data"]["session"];
        expect(fatal(gt(own.size(), std::size_t{0})));
        for (std::size_t index = 0; index < own.size(); ++index) {
            const std::string request = own[index]["request"].dump();
            auto sent = mlibs::json::Document::parse(own[index]["request"].text().value_or(request), "request");
            const std::string op = sent ? sent->root()["op"].text().value_or("") : std::string{};
            expect(!op.empty()) << request;
            expect(text.contains(std::format(R"(`{{"op":"{}")", op))) << "the session's request " << op;
        }
    };
}};
