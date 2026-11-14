module mlibs.agent;

import std;
import openas;
import mlibs.json;

namespace mlibs::agent {

std::string json_text(std::string_view text) {
    std::string out = "\"";
    for (const char raw : text) {
        const auto ch = static_cast<unsigned char>(raw);
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (ch < 0x20) out += std::format("\\u{:04x}", static_cast<unsigned>(ch));
                else out += raw;
        }
    }
    out += '"';
    return out;
}

std::expected<Request, std::string> parse_request(std::string_view line) {
    auto parsed = mlibs::json::Document::parse(line, "request");
    if (!parsed) return std::unexpected(std::format("a request is one JSON object a line: {}", parsed.error().text()));
    auto document = std::make_shared<const mlibs::json::Document>(std::move(*parsed));
    const auto root = document->root();
    if (!root.is_object()) return std::unexpected(std::string("a request is one JSON object a line"));
    Request request;
    request.op = root["op"].text().value_or("");
    if (request.op.empty()) return std::unexpected(std::string("a request names its operation: \"op\""));
    if (const auto re = root["re"]) request.re = re.dump();
    request.answer = root["answer"].text().value_or("");
    request.document = std::move(document);
    return request;
}

Answer::Answer(Reply reply, std::string re) : reply_(std::move(reply)), re_(std::move(re)) {}

void Answer::record(std::string_view kind, std::string_view members) {
    if (finished_ || !reply_) return;
    std::string line = std::format("{{\"kind\":{}", json_text(kind));
    if (!re_.empty()) line += std::format(",\"re\":{}", re_);
    if (!members.empty()) {
        line += ',';
        line += members;
    }
    if (kind == "result" && fact_ != 0 && !members.contains("\"fact\":")) line += std::format(",\"fact\":{}", fact_);
    if (kind == "error") {
        // Kept for the record of what was asked: a subject that writes its own records is read here.
        if (auto parsed = mlibs::json::Document::parse(std::format("{{{}}}", members), "record")) {
            code_ = parsed->root()["code"].text().value_or("");
            text_ = parsed->root()["text"].text().value_or("");
        }
    }
    // A success says what follows it — whoever writes the result (a subject that speaks records writes its own).
    if (kind == "result" && !then_.empty() && (members.contains("\"exitCode\":0,") || members.ends_with("\"exitCode\":0")) && !members.contains("\"then\":"))
        line += std::format(",\"then\":{}", then_);
    line += '}';
    reply_(line);
    if (kind == "result") {
        finished_ = true;
        if (ended_) ended_(code_, text_);
    }
}

void Answer::data(std::string_view json) { record("data", std::format("\"data\":{}", json)); }
void Answer::notice(std::string_view text) { record("notice", std::format("\"text\":{}", json_text(text))); }
void Answer::error(std::string_view code, std::string_view text, std::string_view next) {
    if (next.empty()) next = otherwise_;
    std::string members = std::format("\"code\":{},\"text\":{}", json_text(code), json_text(text));
    if (!next.empty()) members += std::format(",\"next\":{}", next);
    record("error", members);
}
void Answer::result(int exit_code) {
    record("result", std::format("\"exitCode\":{}", exit_code));
    finished_ = true;
}
void Answer::fail(std::string_view code, std::string_view text, int exit_code, std::string_view next) {
    error(code, text, next);
    result(exit_code);
}

std::string way_out(std::string_view why, std::string_view request) {
    return std::format("{{\"why\":{},\"request\":{}}}", json_text(why), request);
}

}  // namespace mlibs::agent
