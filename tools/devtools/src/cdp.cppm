// omui.devtools.cdp — headless Chromium driven over the Chrome DevTools Protocol, and a built site
// served as GitHub Pages serves it (moved from tools/ci/page.mjs, D-206): what the site's browser
// checks share.
//
// - Server: a directory on a free port of 127.0.0.1 (pages(): gzip for the kinds a host compresses —
//   mlibs.delivery's, the same a site's delivery is measured by — the rest as it is, keep-alive as
//   Pages' HTTP is), or any handler.
// - Browser: Chromium headless (or headed: a window on the display) on about:blank with
// --remote-debugging-port=0 (it writes the port it chose to
//   DevToolsActivePort in its profile), its page's WebSocket; `call` sends a method and waits for
//   its answer, each call with a deadline (OMUI_CDP_TIMEOUT_S, default 120 s: after one stall,
//   later calls fail at once), `value` evaluates an expression (awaited, by value), `on` hears
//   events. Events and answers are delivered in the order they came, on the caller's thread, while
//   it waits or sleeps (`pump`), as a page script's event loop has them.
// - Script: a check's report and its failure emulated as a page script's exception: the first
// failing call (or
//   `raise`) makes every later page operation a no-op and every check unrecorded until `guard`
//   catches it.
// - The JavaScript value helpers: a missing member read as undefined, truthiness, JSON.stringify.
//
// The framing is mlibs.http's (HTTP/1.1) and websocket here (RFC 6455: masked client frames,
// fragments, ping and pong, payloads of many MB); gzip is mlibs.delivery's.
export module omui.devtools.cdp;

import std;
import nlohmann.json;

export namespace omui::devtools::cdp {

using json = nlohmann::json;

// --- what Pages serves ---

// Whether an Accept-Encoding header asks for gzip (the word, \bgzip\b).
[[nodiscard]] bool accepts_gzip(std::string_view accept_encoding);
// The content type Pages' check server gives an extension (the eight it names;
// application/octet-stream else).
[[nodiscard]] std::string_view pages_type(std::string_view extension);
// A request path as page.mjs resolved it: decoded, its query and fragment dropped, `.` and `..`
// resolved without leaving the root; relative (no leading slash).
[[nodiscard]] std::string resolved_path(std::string_view target);

// --- WebSocket framing (RFC 6455) ---

namespace websocket {

enum class Opcode : std::uint8_t {
    continuation = 0,
    text = 1,
    binary = 2,
    close = 8,
    ping = 9,
    pong = 10
};

struct Frame {
    bool fin = true;
    Opcode opcode = Opcode::text;
    std::string payload;   // unmasked
    std::size_t size = 0;  // the frame's bytes in what arrived
};

// A client's frame: FIN set, masked with `mask` (a client masks every frame).
[[nodiscard]] std::string frame(Opcode opcode, std::string_view payload,
                                std::array<std::uint8_t, 4> mask, bool fin = true);
// The frame `arrived` starts with, once whole; nothing while it is not; an error for one over
// `most` bytes or a reserved opcode.
[[nodiscard]] std::expected<std::optional<Frame>, std::string>
parse(std::string_view arrived, std::size_t most = std::size_t{512} << 20);
// The Sec-WebSocket-Accept a server answers `key` with.
[[nodiscard]] std::string accept(std::string_view key);

// Fragments joined: a message once its last frame came (control frames pass through whole).
class Assembler {
public:
    // The message `frame` completes, as (opcode, payload); nothing while fragments are outstanding;
    // an error for a continuation with no message started.
    [[nodiscard]] std::expected<std::optional<std::pair<Opcode, std::string>>, std::string>
    add(Frame frame);
private:
    std::optional<Opcode> started_;
    std::string pending_;
};

}  // namespace websocket

// --- HTTP over the loopback ---

// GET `target` from 127.0.0.1:`port`: the body of a 2xx answer; or why not.
[[nodiscard]] std::expected<std::string, std::string>
get(std::uint16_t port, std::string_view target,
    std::chrono::milliseconds timeout = std::chrono::seconds(10));
// The same for an http://127.0.0.1:PORT/… or http://localhost:PORT/… URL.
[[nodiscard]] std::expected<std::string, std::string>
get(std::string_view url, std::chrono::milliseconds timeout = std::chrono::seconds(10));

struct Reply {
    int status = 200;
    std::string body;
    std::string type = "application/octet-stream";  // empty: none said
    std::vector<std::pair<std::string, std::string>> headers;
};

struct Arrived {
    std::string method, path, query;  // path and query as sent
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    [[nodiscard]] std::string_view header(std::string_view name) const;
};

using Handler = std::function<Reply(const Arrived&)>;

// GitHub Pages' way with `directory`: a directory's index.html, 404 for what is not there, the
// content types and `cache-control: max-age=600` page.mjs gave, gzip for compressed_kind() files
// when asked (each file's gzip made once, before the first request: the page's timings measure the
// page, not this server's deflate).
[[nodiscard]] Handler pages(std::filesystem::path directory);

// A server on a free port of 127.0.0.1; each connection on a thread of its own, kept alive between
// requests.
class Server {
public:
    [[nodiscard]] static std::expected<std::unique_ptr<Server>, std::string> start(Handler handler);
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    // http://127.0.0.1:PORT/
    [[nodiscard]] std::string url() const;
    [[nodiscard]] std::uint16_t port() const;
    void close();

    struct State;
    explicit Server(std::unique_ptr<State> state);
private:
    std::unique_ptr<State> state_;
};

// --- the browser ---

struct Launch {
    int width = 1280, height = 800;
    std::vector<std::string>
        flags;  // more switches; a --user-data-dir= among them is the caller's profile, kept
    // A window on the display there is, drawn by the machine's GPU, its viewport the window's own (no emulated
    // metrics, no software fallback); else headless.
    bool headed = false;
};

using Listener = std::function<void(const std::string& method, const json& params)>;

class Browser {
public:
    // Chromium at `chrome`, its page at about:blank, Page and Runtime enabled, the viewport
    // `width`×`height` at scale 1; or why not (it did not start, or opened no DevTools port within
    // a minute while it ran).
    [[nodiscard]] static std::expected<std::unique_ptr<Browser>, std::string>
    launch(const std::filesystem::path& chrome, const Launch& launch = {});
    ~Browser();
    Browser(const Browser&) = delete;
    Browser& operator=(const Browser&) = delete;

    // The answer to `method` (the whole message: its "result", or its "error"); or why there is
    // none (no answer within the deadline, an earlier stall, the connection gone).
    [[nodiscard]] std::expected<json, std::string> call(std::string_view method,
                                                        json params = json::object());
    // Sent, its answer not awaited (a listener's `void b.call(…)`).
    void post(std::string_view method, json params = json::object());
    // Runtime.evaluate with awaitPromise and returnByValue: result.result.value (null when it has
    // none).
    [[nodiscard]] std::expected<json, std::string> value(std::string_view expression);
    void on(Listener listener);
    // Time passing, the events that come delivered (a page script's sleep).
    void pump(std::chrono::milliseconds duration);
    // /json/version's Browser.
    [[nodiscard]] const std::string& version() const;
    // The browser ended (asked, then forced after 5 s) and its profile removed when it was this
    // one's.
    void close();

    struct State;
    explicit Browser(std::unique_ptr<State> state);
private:
    std::unique_ptr<State> state_;
};

// --- the JavaScript value helpers ---

// A member or element read as a page script reads it: null (undefined) when absent or not an object
// / array.
[[nodiscard]] const json& at(const json& value, std::string_view key);
[[nodiscard]] const json& at(const json& value, std::size_t index);
template <class... Keys>
[[nodiscard]] const json& dig(const json& value, Keys&&... keys) {
    const json* current = &value;
    ((current = &at(*current, std::forward<Keys>(keys))), ...);
    return *current;
}
// JavaScript's truthiness: false, 0, NaN, "", null are false; objects and arrays true.
[[nodiscard]] bool truthy(const json& value);
// A number's value; NaN when it is not one.
[[nodiscard]] double number(const json& value);
// A string's text; empty when it is not one.
[[nodiscard]] std::string text(const json& value);
// `array.includes(item)` / `string.includes(part)`.
[[nodiscard]] bool includes(const json& value, std::string_view item);
// JSON.stringify of a string or value (UTF-8 as it is).
[[nodiscard]] std::string q(std::string_view text);
[[nodiscard]] std::string stringify(const json& value);
// A value's JSON, for a report (two-space indent, as JSON.stringify(report, null, 2)).
[[nodiscard]] std::string pretty(const json& value, int indent = 2);
// The node of `nodes` (an array) whose id is `id`; null when none.
[[nodiscard]] const json& find_id(const json& nodes, std::string_view id);
// An [x, y, width, height] of four finite numbers.
[[nodiscard]] std::optional<std::array<double, 4>> box(const json& value);
// A number as a page script's template literal writes it (`${0.25}` is 0.25, `${3}` is 3).
[[nodiscard]] std::string js_number(double value);

// A screenshot's base64 PNG written to `path` (its directory made); whether it was.
bool write_base64(const std::filesystem::path& path, std::string_view base64);
// The bytes of a base64 text; empty when it is not one.
[[nodiscard]] std::string from_base64(std::string_view base64);

// The CDP timeout (OMUI_CDP_TIMEOUT_S, default 120 s).
[[nodiscard]] std::chrono::milliseconds deadline();

// --- a check's run ---

class Script {
public:
    // detail_when_falsy: a falsy detail is still reported (`detail === undefined ? {} : {detail}`);
    // otherwise only a truthy one is (`detail ? {detail} : {}`).
    explicit Script(bool detail_when_falsy = false);

    json report = {{"checks", json::array()}};
    Browser* page = nullptr;

    // A check recorded in report.checks (nothing while an error is pending); `ok`.
    bool check(bool ok, std::string_view what, std::optional<json> detail = std::nullopt);
    // A row of report.checks as given (nothing while an error is pending).
    void record(json row);
    // An error thrown: what follows is skipped until guard() catches it.
    void raise(std::string message);
    [[nodiscard]] bool thrown() const {
        return thrown_.has_value();
    }
    // The pending error, cleared.
    [[nodiscard]] std::optional<std::string> take();
    // `try { body } catch (error) { check(false, failed, String(error)) }`; whether body completed.
    bool guard(std::string_view failed, const std::function<void()>& body);
    // `try { body } catch (error) { handler(message) }`.
    bool attempt(const std::function<void()>& body,
                 const std::function<void(const std::string&)>& handler);
    // `finally { body }`: run with any pending error held aside; the error raised in it, else the
    // one held, after.
    void finally(const std::function<void()>& body);

    // The page, each failure raised (the call's own message): the answer, or null.
    json call(std::string_view method, json params = json::object());
    // A call whose protocol error is raised too (`if (r.error) throw new Error(r.error.message)`).
    json dispatch(std::string_view method, json params = json::object());
    json value(std::string_view expression);
    void post(std::string_view method, json params = json::object());
    void sleep(int milliseconds);
    // `for (i < count) { if (await value(expression)) return true; await sleep(step); } return
    // false`.
    bool until_count(std::string_view expression, int count, int step_ms);
    // The same with a predicate.
    bool until_count(const std::function<bool()>& predicate, int count, int step_ms);
    // `end = now + timeout; while (now < end) { if (await predicate()) return true; await
    // sleep(step); } return false`.
    bool until_time(const std::function<bool()>& predicate, int timeout_ms, int step_ms);
    bool until_time(std::string_view expression, int timeout_ms, int step_ms);
    // Page.captureScreenshot (params given) written to `path`: its base64, or empty (raised when
    // `required`).
    std::string screenshot(const std::filesystem::path& path, json params = {{"format", "png"}},
                           bool required = true);
    // Whether every recorded check holds (and, when asked, there is at least one).
    [[nodiscard]] bool all_ok(bool at_least_one = false) const;
private:
    std::optional<std::string> thrown_;
    bool detail_when_falsy_;
};

// A JavaScript error's text as String(error.stack || error) begins: "Error: message".
[[nodiscard]] std::string error_text(std::string_view message);

}  // namespace omui::devtools::cdp
