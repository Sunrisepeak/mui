// SVG path data (a `d` attribute) to a path: M L H V C S Q T A Z, absolute
// and relative; arcs become cubics with mlibs.math's deterministic functions.
module mlibs.geom.api;
import std;
import mlibs.math;

namespace mlibs::geom {

namespace {
struct DataReader {
    std::string_view text;
    std::size_t at = 0;
    void space() {
        while (at < text.size() && (text[at] == ' ' || text[at] == '\t' || text[at] == '\n' || text[at] == '\r' || text[at] == ',')) ++at;
    }
    bool number_next() {
        space();
        return at < text.size() && (std::isdigit(static_cast<unsigned char>(text[at])) || text[at] == '-' || text[at] == '+' || text[at] == '.');
    }
    std::optional<double> number() {
        space();
        const std::size_t begin = at;
        if (at < text.size() && (text[at] == '-' || text[at] == '+')) ++at;
        bool digits = false, dot = false;
        while (at < text.size() && (std::isdigit(static_cast<unsigned char>(text[at])) || (text[at] == '.' && !dot))) {
            if (text[at] == '.') dot = true;
            else digits = true;
            ++at;
        }
        if (!digits) {
            at = begin;
            return std::nullopt;
        }
        if (at < text.size() && (text[at] == 'e' || text[at] == 'E')) {
            std::size_t exponent = at + 1;
            if (exponent < text.size() && (text[exponent] == '-' || text[exponent] == '+')) ++exponent;
            if (exponent < text.size() && std::isdigit(static_cast<unsigned char>(text[exponent]))) {
                at = exponent;
                while (at < text.size() && std::isdigit(static_cast<unsigned char>(text[at]))) ++at;
            }
        }
        double value = 0;
        const std::size_t from = begin + (text[begin] == '+' ? 1 : 0);
        const auto parsed = std::from_chars(text.data() + from, text.data() + at, value);
        if (parsed.ec != std::errc{} || !std::isfinite(value)) {
            at = begin;
            return std::nullopt;
        }
        return value;
    }
    // An arc's flag: 0 or 1, maybe with no separator after it.
    std::optional<bool> flag() {
        space();
        if (at < text.size() && (text[at] == '0' || text[at] == '1')) return text[at++] == '1';
        return std::nullopt;
    }
};
}  // namespace

std::expected<PathData, PathDataError> parse_path_data(std::string_view text, PathLimits limits) {
    namespace det = mlibs::math::det;
    PathData out;
    DataReader reader{text};
    const auto fail = [&](std::string message) { return std::unexpected(PathDataError{reader.at, std::move(message)}); };
    double cx = 0, cy = 0, sx = 0, sy = 0;  // the current point, the subpath's start
    double last_cx = 0, last_cy = 0;        // the last control point (smooth curves reflect it)
    char previous = 0, command = 0;
    bool started = false;
    const auto point = [&](double x, double y) { out.points.push_back({static_cast<float>(x), static_cast<float>(y)}); };
    const auto cubic = [&](double x1, double y1, double x2, double y2, double x, double y) {
        out.verbs.push_back(PathVerb::cubic);
        point(x1, y1);
        point(x2, y2);
        point(x, y);
        last_cx = x2;
        last_cy = y2;
        cx = x;
        cy = y;
    };
    const auto line = [&](double x, double y) {
        out.verbs.push_back(PathVerb::line);
        point(x, y);
        cx = last_cx = x;
        cy = last_cy = y;
    };
    while (true) {
        reader.space();
        if (reader.at >= text.size()) break;
        const char c = text[reader.at];
        if (std::isalpha(static_cast<unsigned char>(c))) {
            command = c;
            ++reader.at;
        } else if (!command || command == 'Z' || command == 'z') {
            return fail(std::format("`{}` where a command was due (M L H V C S Q T A Z)", c));
        }
        if (!started && std::toupper(static_cast<unsigned char>(command)) != 'M') return fail("path data begins with a moveto (M or m)");
        char upper = 0;
        do {
            // Read again each time: a moveto's further pairs are linetos.
            const bool relative = std::islower(static_cast<unsigned char>(command));
            upper = static_cast<char>(std::toupper(static_cast<unsigned char>(command)));
            switch (upper) {
            case 'M': {
                const auto x = reader.number(), y = reader.number();
                if (!x || !y) return fail("a moveto takes x y");
                cx = relative ? cx + *x : *x;
                cy = relative ? cy + *y : *y;
                sx = last_cx = cx;
                sy = last_cy = cy;
                out.verbs.push_back(PathVerb::move);
                point(cx, cy);
                started = true;
                command = relative ? 'l' : 'L';  // pairs after a moveto are linetos
                break;
            }
            case 'L': {
                const auto x = reader.number(), y = reader.number();
                if (!x || !y) return fail("a lineto takes x y");
                line(relative ? cx + *x : *x, relative ? cy + *y : *y);
                break;
            }
            case 'H': case 'V': {
                const auto v = reader.number();
                if (!v) return fail(std::format("`{}` takes a coordinate", command));
                if (upper == 'H') line(relative ? cx + *v : *v, cy);
                else line(cx, relative ? cy + *v : *v);
                break;
            }
            case 'C': {
                std::array<double, 6> v{};
                for (auto& n : v) {
                    const auto got = reader.number();
                    if (!got) return fail("a curveto takes x1 y1 x2 y2 x y");
                    n = *got;
                }
                if (relative)
                    for (std::size_t k = 0; k < v.size(); ++k) v[k] += k % 2 ? cy : cx;
                cubic(v[0], v[1], v[2], v[3], v[4], v[5]);
                break;
            }
            case 'S': {
                std::array<double, 4> v{};
                for (auto& n : v) {
                    const auto got = reader.number();
                    if (!got) return fail("a smooth curveto takes x2 y2 x y");
                    n = *got;
                }
                if (relative)
                    for (std::size_t k = 0; k < v.size(); ++k) v[k] += k % 2 ? cy : cx;
                const bool reflect = previous == 'C' || previous == 'S';
                cubic(reflect ? 2 * cx - last_cx : cx, reflect ? 2 * cy - last_cy : cy, v[0], v[1], v[2], v[3]);
                break;
            }
            case 'Q': case 'T': {
                double qx = 0, qy = 0;
                if (upper == 'Q') {
                    const auto a = reader.number(), b = reader.number();
                    if (!a || !b) return fail("a quadratic curveto takes x1 y1 x y");
                    qx = relative ? cx + *a : *a;
                    qy = relative ? cy + *b : *b;
                } else {
                    const bool reflect = previous == 'Q' || previous == 'T';
                    qx = reflect ? 2 * cx - last_cx : cx;
                    qy = reflect ? 2 * cy - last_cy : cy;
                }
                const auto px = reader.number(), py = reader.number();
                if (!px || !py) return fail(std::format("`{}` takes x y", command));
                out.verbs.push_back(PathVerb::quad);
                point(qx, qy);
                cx = relative ? cx + *px : *px;
                cy = relative ? cy + *py : *py;
                point(cx, cy);
                last_cx = qx;
                last_cy = qy;
                break;
            }
            case 'A': {
                const auto rx = reader.number(), ry = reader.number(), angle = reader.number();
                const auto large = reader.flag(), sweep = reader.flag();
                const auto px = reader.number(), py = reader.number();
                if (!rx || !ry || !angle || !large || !sweep || !px || !py) return fail("an arc takes rx ry angle large-arc sweep x y");
                const double x = relative ? cx + *px : *px, y = relative ? cy + *py : *py;
                double rxa = std::abs(*rx), rya = std::abs(*ry);
                if (rxa == 0 || rya == 0 || (x == cx && y == cy)) {  // a degenerate arc is a line (SVG F.6.6)
                    line(x, y);
                    break;
                }
                // Endpoint to centre (SVG F.6.5), then a cubic a quarter turn at most.
                const double phi = *angle * det::pi / 180, cos_phi = det::cos(phi), sin_phi = det::sin(phi);
                const double dx = (cx - x) / 2, dy = (cy - y) / 2;
                const double x1 = cos_phi * dx + sin_phi * dy, y1 = -sin_phi * dx + cos_phi * dy;
                const double lambda = x1 * x1 / (rxa * rxa) + y1 * y1 / (rya * rya);
                if (lambda > 1) {
                    const double grow = std::sqrt(lambda);
                    rxa *= grow;
                    rya *= grow;
                }
                const double numerator = rxa * rxa * rya * rya - rxa * rxa * y1 * y1 - rya * rya * x1 * x1;
                const double denominator = rxa * rxa * y1 * y1 + rya * rya * x1 * x1;
                double factor = std::sqrt(std::max(0.0, numerator / denominator));
                if (*large == *sweep) factor = -factor;
                const double ccx = factor * rxa * y1 / rya, ccy = -factor * rya * x1 / rxa;
                const double mx = cos_phi * ccx - sin_phi * ccy + (cx + x) / 2, my = sin_phi * ccx + cos_phi * ccy + (cy + y) / 2;
                const double start = det::atan2((y1 - ccy) / rya, (x1 - ccx) / rxa);
                double turn = det::atan2((-y1 - ccy) / rya, (-x1 - ccx) / rxa) - start;
                if (*sweep && turn < 0) turn += 2 * det::pi;
                if (!*sweep && turn > 0) turn -= 2 * det::pi;
                const int pieces = std::max(1, static_cast<int>(std::ceil(std::abs(turn) / (det::pi / 2) - 1e-9)));
                const double step = turn / pieces;
                const double handle = 4.0 / 3.0 * det::sin(step / 4) / det::cos(step / 4);
                // The ellipse's point at an angle, and its derivative there.
                const auto on = [&](double t) {
                    const double ex = rxa * det::cos(t), ey = rya * det::sin(t), tx = -rxa * det::sin(t), ty = rya * det::cos(t);
                    return std::array<double, 4>{mx + cos_phi * ex - sin_phi * ey, my + sin_phi * ex + cos_phi * ey, cos_phi * tx - sin_phi * ty,
                                                 sin_phi * tx + cos_phi * ty};
                };
                for (int piece = 0; piece < pieces; ++piece) {
                    const auto from = on(start + step * piece), to = on(start + step * (piece + 1));
                    const bool last = piece + 1 == pieces;
                    cubic(from[0] + handle * from[2], from[1] + handle * from[3], to[0] - handle * to[2], to[1] - handle * to[3], last ? x : to[0],
                          last ? y : to[1]);
                }
                break;
            }
            case 'Z':
                out.verbs.push_back(PathVerb::close);
                cx = last_cx = sx;
                cy = last_cy = sy;
                break;
            default: return fail(std::format("`{}` is not a path command (M L H V C S Q T A Z)", command));
            }
            previous = upper;
            if (out.verbs.size() > limits.max_verbs || out.points.size() > limits.max_points) return fail("the path is over its budget");
        } while (upper != 'Z' && reader.number_next());
    }
    if (!started) return fail("no path: path data begins with a moveto (M or m)");
    if (const auto checked = inspect_path(out.view(), limits); !checked) return fail(std::string(describe(checked.error().code).name));
    return out;
}

}  // namespace mlibs::geom
