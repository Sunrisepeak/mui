module mlibs.http.api;

import std;

namespace mlibs::http {

namespace {

std::string lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string_view trimmed(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}

}  // namespace

std::string_view Request::header(std::string_view name) const {
    for (const auto& [key, value] : headers)
        if (key == name) return value;
    return {};
}

std::expected<std::optional<Request>, std::string> parse(std::string_view arrived, std::size_t most) {
    const std::size_t end = arrived.find("\r\n\r\n");
    if (end == std::string_view::npos) {
        if (arrived.size() > most) return std::unexpected(std::format("the request's head is larger than {} bytes", most));
        return std::optional<Request>{};
    }
    if (end + 4 > most) return std::unexpected(std::format("the request's head is larger than {} bytes", most));
    Request request;
    request.head_size = end + 4;
    std::string_view head = arrived.substr(0, end);
    const std::size_t first_end = head.find("\r\n");
    const std::string_view first = head.substr(0, first_end);
    const std::size_t space = first.find(' '), second = first.find(' ', space + 1);
    if (space == std::string_view::npos || second == std::string_view::npos || !first.substr(second + 1).starts_with("HTTP/1."))
        return std::unexpected(std::format("not an HTTP/1.x request line: {}", first.substr(0, 80)));
    request.method = std::string(first.substr(0, space));
    const std::string_view target = first.substr(space + 1, second - space - 1);
    const std::size_t question = target.find('?');
    request.path = std::string(target.substr(0, question));
    if (question != std::string_view::npos) request.query = std::string(target.substr(question + 1));
    for (std::string_view rest = first_end == std::string_view::npos ? std::string_view{} : head.substr(first_end + 2); !rest.empty();) {
        const std::size_t line_end = rest.find("\r\n");
        const std::string_view line = rest.substr(0, line_end);
        rest = line_end == std::string_view::npos ? std::string_view{} : rest.substr(line_end + 2);
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos) continue;
        request.headers.emplace_back(lower(trimmed(line.substr(0, colon))), std::string(trimmed(line.substr(colon + 1))));
    }
    if (const auto length = request.header("content-length"); !length.empty()) {
        const auto [at, failed] = std::from_chars(length.data(), length.data() + length.size(), request.body_length);
        if (failed != std::errc{} || at != length.data() + length.size()) return std::unexpected(std::format("Content-Length is not a count: {}", length));
    }
    return std::optional<Request>{std::move(request)};
}

std::string query_value(std::string_view query, std::string_view key) {
    for (std::size_t at = 0; at < query.size();) {
        const std::size_t end = std::min(query.find('&', at), query.size());
        const std::string_view pair = query.substr(at, end - at);
        if (pair.starts_with(key) && pair.size() > key.size() && pair[key.size()] == '=') return std::string(pair.substr(key.size() + 1));
        at = end + 1;
    }
    return {};
}

std::string decoded(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    const auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t at = 0; at < text.size(); ++at) {
        if (text[at] == '%' && at + 2 < text.size() && digit(text[at + 1]) >= 0 && digit(text[at + 2]) >= 0) {
            out += static_cast<char>(digit(text[at + 1]) * 16 + digit(text[at + 2]));
            at += 2;
        } else {
            out += text[at];
        }
    }
    return out;
}

std::string_view reason(int status) {
    switch (status) {
        case 200: return "OK";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Content Too Large";
        case 500: return "Internal Server Error";
        case 504: return "Gateway Timeout";
        default: return "Error";
    }
}

std::string response(int status, std::string_view body, const Options& options) {
    std::string out = std::format("HTTP/1.1 {} {}\r\nContent-Type: {}\r\nContent-Length: {}\r\nConnection: close\r\n", status, reason(status), options.type,
                                  body.size());
    for (const auto& [name, value] : options.headers) out += std::format("{}: {}\r\n", name, value);
    out += "\r\n";
    out += body;
    return out;
}

std::string_view media_type(std::string_view extension) {
    constexpr std::array<std::pair<std::string_view, std::string_view>, 14> types{{{".html", "text/html; charset=utf-8"},
                                                                                    {".js", "text/javascript"},
                                                                                    {".mjs", "text/javascript"},
                                                                                    {".wasm", "application/wasm"},
                                                                                    {".json", "application/json"},
                                                                                    {".css", "text/css"},
                                                                                    {".md", "text/markdown"},
                                                                                    {".svg", "image/svg+xml"},
                                                                                    {".png", "image/png"},
                                                                                    {".webp", "image/webp"},
                                                                                    {".ttf", "font/ttf"},
                                                                                    {".woff2", "font/woff2"},
                                                                                    {".txt", "text/plain; charset=utf-8"},
                                                                                    {".data", "application/octet-stream"}}};
    for (const auto& [known, type] : types)
        if (known == extension) return type;
    return "application/octet-stream";
}

}  // namespace mlibs::http
