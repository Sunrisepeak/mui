import std;
import openmui.math;
import openmui.testing;

using namespace omui::testing;
namespace color = omui::math::color;

int main(int argc, char** argv) {
    Suite suite("math.color");
    suite.test("sRGB luminance keeps endpoints, gamma breakpoint and channel weights", [] {
        check_near(color::relative_luminance(0, 0, 0), 0.0, 1e-15);
        check_near(color::relative_luminance(1, 1, 1), 1.0, 1e-15);
        check_near(color::relative_luminance(1, 0, 0), 0.2126, 1e-15);
        check_near(color::relative_luminance(0, 1, 0), 0.7152, 1e-15);
        check_near(color::relative_luminance(0, 0, 1), 0.0722, 1e-15);
        check_near(color::relative_luminance(0.5, 0.5, 0.5), 0.21404114048223255, 1e-14);
        check_near(color::relative_luminance(0.04045, 0.04045, 0.04045), 0.04045 / 12.92, 1e-15);
        double previous = -1;
        for (int i = 0; i <= 255; ++i) {
            const double c = static_cast<double>(i) / 255;
            const double luminance = color::relative_luminance(c, c, c);
            check(luminance > previous); previous = luminance;
        }
    });
    suite.test("contrast is symmetric, normalized, and distinguishes the observed low-contrast frame", [] {
        check_near(color::contrast_ratio(0, 1), 21.0, 1e-14);
        check_near(color::contrast_ratio(0.25, 0.25), 1.0, 1e-15);
        check_near(color::contrast_ratio(0.2, 0.7), color::contrast_ratio(0.7, 0.2), 1e-15);
        check_near(color::relative_luminance(-1, 2, 0), 0.7152, 1e-15);
        const auto ink = color::relative_luminance(136.0 / 255, 137.0 / 255, 140.0 / 255);
        const auto paper = color::relative_luminance(136.0 / 255, 139.0 / 255, 174.0 / 255);
        check(color::contrast_ratio(ink, paper) > 1.0 && color::contrast_ratio(ink, paper) < 1.1);
    });
    return suite.run(argc, argv);
}
