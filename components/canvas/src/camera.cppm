// openmui.ui.canvas:camera — where the canvas looks (canvas-markdown §1.5).
//
// The camera is the world point at the viewport's centre, in double
// precision, and a zoom (screen pixels per world unit). Drawing is camera-
// relative: a node's screen rectangle is computed in double and only then
// rounded to float, so a wall far from the origin draws as crisply as one at
// it. Its moves are pure functions of time (D-60): a spring between two
// views (logarithmic in zoom, so 1% → 400% is as smooth as 100% → 200%), and
// inertia after a fling — seekable, the same on every run.
export module openmui.ui.canvas:camera;

import std;
import openmui.core;
import hanim;
import :scene;

export namespace omui::world {

struct Camera {
    double x = 0, y = 0;  // the world point at the viewport's centre
    double zoom = 1;      // screen pixels per world unit

    [[nodiscard]] Point to_screen(double wx, double wy, Size viewport) const noexcept;
    [[nodiscard]] Rect to_screen(const WorldRect& r, Size viewport) const noexcept;
    [[nodiscard]] std::array<double, 2> to_world(Point screen, Size viewport) const noexcept;
    // What the viewport shows.
    [[nodiscard]] WorldRect visible(Size viewport) const noexcept;

    // Zooms by `factor` keeping the world point under `screen` where it is
    // (the cursor's), within [min, max].
    void zoom_at(Point screen, double factor, Size viewport, double min, double max) noexcept;
    // Moves the world with the pointer: `delta` screen pixels.
    void pan(Point delta) noexcept;
    // The view that shows `r` whole, `margin` screen pixels inside the edges.
    [[nodiscard]] static Camera fit(const WorldRect& r, Size viewport, float margin, double min, double max) noexcept;

    friend constexpr bool operator==(const Camera& a, const Camera& b) noexcept { return a.x == b.x && a.y == b.y && a.zoom == b.zoom; }
};

// A camera moving to another view on a spring (x, y and the logarithm of
// zoom each a lane), from `start`.
class CameraMotion {
public:
    CameraMotion() = default;
    CameraMotion(Camera from, Camera to, hanim::seconds start, hanim::SpringParams params = hanim::springs::standard,
                 std::array<double, 3> velocity = {});
    [[nodiscard]] Camera at(hanim::seconds t) const;
    // Its velocity at t (world units and log-zoom per second): a motion
    // retargeted mid-flight keeps it.
    [[nodiscard]] std::array<double, 3> velocity(hanim::seconds t) const;
    [[nodiscard]] bool settled(hanim::seconds t) const { return t >= end_; }
    [[nodiscard]] const Camera& target() const noexcept { return to_; }
    [[nodiscard]] hanim::seconds start() const noexcept { return start_; }

private:
    Camera                         to_;
    hanim::seconds                  start_{0}, end_{0};
    std::array<hanim::Oscillator, 3> lanes_{};
};

// A fling: the camera keeps the pointer's last velocity (screen px/s),
// slowing exponentially, until it is slower than `rest` px/s.
class Inertia {
public:
    Inertia() = default;
    Inertia(Camera from, Point velocity, hanim::seconds start, double decay = 6.0, double rest = 8.0);
    [[nodiscard]] Camera at(hanim::seconds t) const;
    [[nodiscard]] bool settled(hanim::seconds t) const { return t >= end_; }
    [[nodiscard]] hanim::seconds start() const noexcept { return start_; }

private:
    Camera        from_;
    Point         velocity_{};
    hanim::seconds start_{0}, end_{0};
    double        decay_ = 6;
};

}  // namespace omui::world
