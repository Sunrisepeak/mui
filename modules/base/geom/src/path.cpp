module mlibs.geom.api;
import std;

namespace mlibs::geom {

// ── flattening ────────────────────────────────────────────────────────────

namespace {

float length(Point v) { return std::sqrt(v.x * v.x + v.y * v.y); }

int segments_for(float deviation, float tolerance) {
    if (!(deviation > tolerance)) return 1;
    float n = std::ceil(std::sqrt(deviation / tolerance));
    return n > 256 ? 256 : static_cast<int>(n);
}

}  // namespace

void flatten_path(PathView path, const Affine& m, PathPolylines& out, float tolerance) {
    std::size_t pi = 0;
    bool open = false;
    Point current{};
    auto start = [&](Point p) {
        out.begin();
        out.points.push_back(p);
        open = true;
        current = p;
    };
    for (PathVerb v : path.verbs) {
        switch (v) {
            case PathVerb::move: {
                start(m.apply(path.points[pi++]));
                break;
            }
            case PathVerb::line: {
                Point p = m.apply(path.points[pi++]);
                if (!open) start(current);
                out.points.push_back(p);
                current = p;
                break;
            }
            case PathVerb::quad: {
                Point c = m.apply(path.points[pi]), p = m.apply(path.points[pi + 1]);
                pi += 2;
                if (!open) start(current);
                Point p0 = current;
                Point dd{p0.x - 2 * c.x + p.x, p0.y - 2 * c.y + p.y};
                int n = segments_for(length(dd) / 4, tolerance);
                for (int i = 1; i <= n; ++i) {
                    float t = static_cast<float>(i) / static_cast<float>(n), u = 1 - t;
                    out.points.push_back({u * u * p0.x + 2 * u * t * c.x + t * t * p.x, u * u * p0.y + 2 * u * t * c.y + t * t * p.y});
                }
                current = p;
                break;
            }
            case PathVerb::cubic: {
                Point c1 = m.apply(path.points[pi]), c2 = m.apply(path.points[pi + 1]), p = m.apply(path.points[pi + 2]);
                pi += 3;
                if (!open) start(current);
                Point p0 = current;
                Point d1{p0.x - 2 * c1.x + c2.x, p0.y - 2 * c1.y + c2.y};
                Point d2{c1.x - 2 * c2.x + p.x, c1.y - 2 * c2.y + p.y};
                int n = segments_for(0.75f * std::max(length(d1), length(d2)), tolerance);
                for (int i = 1; i <= n; ++i) {
                    float t = static_cast<float>(i) / static_cast<float>(n), u = 1 - t;
                    float a = u * u * u, b = 3 * u * u * t, cc = 3 * u * t * t, d = t * t * t;
                    out.points.push_back({a * p0.x + b * c1.x + cc * c2.x + d * p.x, a * p0.y + b * c1.y + cc * c2.y + d * p.y});
                }
                current = p;
                break;
            }
            case PathVerb::close: {
                if (open) {
                    current = out.points[out.starts.back()];
                    out.closed.back() = true;
                }
                open = false;
                break;
            }
        }
    }
}

} // namespace mlibs::geom
