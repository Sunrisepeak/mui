// mlibs.error — one shape for every engine's errors (convergence plan §5.2):
// a domain's code, a message, where it concerns, and the errors beneath it.
//
//     enum class Errc : std::uint8_t { unsupported, invalid };
//     constexpr mlibs::error::Description describe(Errc c) noexcept {   // found by ADL, beside the enum
//         using Kind = mlibs::error::Kind;   // (a using-declaration of it here is an ICE in GCC 16)
//         switch (c) {
//             case Errc::unsupported: return {"rhi.unsupported", Kind::refused};
//             case Errc::invalid: return {"rhi.invalid", Kind::invalid_argument};
//         }
//         return {"rhi.?", Kind::internal};
//     }
//     using Error = mlibs::error::Error<Errc>;
//     return std::unexpected(Error{Errc::invalid, "a pass inside a pass"});
//     return std::unexpected(Error{EngineErrc::program, "", "track 2"}.because(program_error));
//
// Codes stay their domain's (the domain names them and says what kind each
// is); the shape, the names' form (`domain.code`), the text and the kinds are
// everyone's. A kind is openas's error code by the same name (SPEC §6.3), so
// an error reaches an agent as what it is.
export module mlibs.error;

import std;

export namespace mlibs::error {

// How a caller — or an agent, over openas — must take an error: openas's
// error codes, the same names.
enum class Kind : std::uint8_t { invalid_argument, not_found, refused, stale, timeout, needs_answer, failed, internal };

[[nodiscard]] constexpr std::string_view name(Kind k) noexcept {
    switch (k) {
        case Kind::invalid_argument: return "invalid-argument";
        case Kind::not_found: return "not-found";
        case Kind::refused: return "refused";
        case Kind::stale: return "stale";
        case Kind::timeout: return "timeout";
        case Kind::needs_answer: return "needs-answer";
        case Kind::failed: return "failed";
        case Kind::internal: return "internal";
    }
    return "internal";
}

// What a domain says of one of its codes: its name (`domain.code`), its kind,
// and a sentence a person reads.
struct Description {
    std::string_view name;
    Kind             kind = Kind::failed;
    std::string_view text = {};
};

// A domain's code: an enum described by `describe(code)`, found beside it.
template <class C>
concept Code = std::is_enum_v<C> && requires(C c) {
    { describe(c) } -> std::same_as<Description>;
};

// An error beneath another, as it was: its code's name, its kind, where, its message.
struct Cause {
    std::string code;
    Kind        kind = Kind::failed;
    std::string where;
    std::string message;
};

// A location as text: itself when it is text, else the domain's
// `describe_where(where)`, found beside its type.
template <class Where>
[[nodiscard]] std::string where_text(const Where& where) {
    if constexpr (std::is_convertible_v<const Where&, std::string_view>) return std::string(std::string_view(where));
    else return describe_where(where);
}

// An error: the code, a message beyond the code's text, where it concerns
// (`Where`: text by default; a domain may keep a structured location and say
// it with `describe_where`), and what failed beneath it, outermost first.
template <Code C, class Where = std::string>
struct Error {
    C                  code{};
    std::string        message;
    Where              where{};
    std::vector<Cause> causes;

    [[nodiscard]] Description description() const noexcept { return describe(code); }
    [[nodiscard]] Kind kind() const noexcept { return describe(code).kind; }

    // This error as a cause of another.
    [[nodiscard]] Cause cause() const { return {std::string(describe(code).name), kind(), where_text(where), message}; }

    // `inner` (and what failed beneath it) beneath this error.
    template <Code D, class W>
    Error& because(const Error<D, W>& inner) & {
        causes.push_back(inner.cause());
        causes.insert(causes.end(), inner.causes.begin(), inner.causes.end());
        return *this;
    }
    template <Code D, class W>
    Error&& because(const Error<D, W>& inner) && {
        return std::move(because(inner));
    }

    // `rhi.invalid (invalid-argument) at track 2: a pass inside a pass; because …`
    [[nodiscard]] std::string text() const {
        const Description d = describe(code);
        std::string out = std::format("{} ({})", d.name, name(d.kind));
        if (const std::string w = where_text(where); !w.empty()) out += " at " + w;
        if (!message.empty()) out += ": " + message;
        else if (!d.text.empty()) out += ": " + std::string(d.text);
        for (const Cause& c : causes) {
            out += std::format("; because {} ({})", c.code, name(c.kind));
            if (!c.where.empty()) out += " at " + c.where;
            if (!c.message.empty()) out += ": " + c.message;
        }
        return out;
    }
};

template <class T, Code C, class Where = std::string>
using Result = std::expected<T, Error<C, Where>>;

}  // namespace mlibs::error
