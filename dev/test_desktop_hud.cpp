// Offline checks for the desktop window's HUD "as in the headset" (utility/WuWaDesktopHud.hpp) and the steady
// desktop view's eased margin (utility/WuWaSteadyView.hpp). No game, device or GPU.
//   python dev/qa.py unit --cpp --only test_desktop_hud
#include "../mod/uevr/src/utility/WuWaDesktopHud.hpp"
#include "../mod/uevr/src/utility/WuWaSteadyView.hpp"
#include <cstdio>
#include <cstdlib>

static int failures = 0;
static void check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
}
static bool near_value(float a, float b, float tolerance = 1e-4f) { return std::abs(a - b) <= tolerance; }

using namespace wuwa_desktop_hud;

static Panel ahead(float distance, float width, float height) {
    Panel p{};
    p.tracking = glm::mat4{1.0f};
    p.tracking[3] = glm::vec4{0.0f, 0.0f, -distance, 1.0f};
    p.width = width;
    p.height = height;
    return p;
}

int main() {
    EyeImage symmetric{{-1.0f, 1.0f, 1.0f, -1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}};
    // A 1.6 x 0.9 m panel 2 m ahead covers tangents +-0.4 across and +-0.225 up/down.
    {
        const auto uv = project(ahead(2.0f, 1.6f, 0.9f), glm::mat4{1.0f}, symmetric);
        check(uv.has_value(), "panel ahead projects");
        if (uv) {
            check(near_value((*uv)[0].x, 0.3f) && near_value((*uv)[1].x, 0.7f), "left/right edges at 0.3 / 0.7");
            check(near_value((*uv)[0].y, 0.3875f) && near_value((*uv)[3].y, 0.6125f), "top/bottom edges, v grows down");
        }
    }
    // An off-centre eye view (more room outward): the panel centre is not the texture centre.
    {
        EyeImage outward{{-1.2f, 0.8f, 1.0f, -1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}};
        const auto uv = project(ahead(2.0f, 0.02f, 0.02f), glm::mat4{1.0f}, outward);
        check(uv && near_value(0.5f * ((*uv)[0].x + (*uv)[1].x), 0.6f, 1e-3f), "asymmetric view: centre at u 0.6");
    }
    // A projection override renders a wider texture; the raw view sits inside it (view_bounds).
    {
        EyeImage wider{{-1.0f, 1.0f, 1.0f, -1.0f}, {0.1f, 0.9f, 0.0f, 1.0f}};
        const auto uv = project(ahead(2.0f, 1.6f, 0.9f), glm::mat4{1.0f}, wider);
        check(uv && near_value((*uv)[0].x, 0.1f + 0.3f * 0.8f) && near_value((*uv)[1].x, 0.1f + 0.7f * 0.8f), "view bounds scale u");
    }
    // The left eye sits 32 mm left of the head: the panel appears right of centre by 0.032 / 2 in tangent.
    {
        glm::mat4 left_eye{1.0f};
        left_eye[3] = glm::vec4{-0.032f, 0.0f, 0.0f, 1.0f};
        const auto uv = project(ahead(2.0f, 0.02f, 0.02f), left_eye, symmetric);
        check(uv && near_value(0.5f * ((*uv)[0].x + (*uv)[1].x), 0.5f + 0.016f / 2.0f, 1e-4f), "eye offset shifts the panel");
    }
    // Behind the eye, or degenerate tangents: nothing to draw (the window falls back to the flat HUD).
    {
        check(!project(ahead(-2.0f, 1.6f, 0.9f), glm::mat4{1.0f}, symmetric), "panel behind the eye is not drawn");
        EyeImage broken{{0.5f, 0.5f, 1.0f, -1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}};
        check(!project(ahead(2.0f, 1.6f, 0.9f), glm::mat4{1.0f}, broken), "degenerate projection is refused");
    }
    // Head roll: a world-fixed panel appears turned the other way in the eye image.
    {
        glm::mat4 rolled = glm::mat4_cast(glm::angleAxis(0.2f, glm::vec3{0.0f, 0.0f, 1.0f}));
        const auto uv = project(ahead(2.0f, 1.6f, 0.9f), rolled, symmetric);
        const auto at = uv ? place(*uv, [](glm::vec2 p) { return p * 1000.0f; }) : std::nullopt;
        check(at && near_value(at->rotation, 0.2f, 0.01f), "head roll turns the HUD (clockwise in y-down pixels)");
    }
    // Placement: centre, size and rotation through a window mapping, including a turned crop.
    {
        const std::array<glm::vec2, 4> square{glm::vec2{0.25f, 0.25f}, {0.75f, 0.25f}, {0.75f, 0.75f}, {0.25f, 0.75f}};
        const auto flat = place(square, [](glm::vec2 p) { return p * glm::vec2{2560.0f, 1440.0f}; });
        check(flat && near_value(flat->centre.x, 1280.0f, 0.01f) && near_value(flat->centre.y, 720.0f, 0.01f), "placement centre");
        check(flat && near_value(flat->size.x, 1280.0f, 0.01f) && near_value(flat->size.y, 720.0f, 0.01f) &&
              near_value(flat->rotation, 0.0f), "placement size, no rotation");
        const float turn = 0.1f;
        const auto turned = place(square, [turn](glm::vec2 p) {
            const glm::vec2 q = (p - 0.5f) * 1000.0f;
            return glm::vec2{500.0f, 500.0f} + glm::vec2{q.x * std::cos(turn) - q.y * std::sin(turn), q.x * std::sin(turn) + q.y * std::cos(turn)};
        });
        check(turned && near_value(turned->rotation, turn, 1e-4f) && near_value(turned->size.x, 500.0f, 0.01f), "turned crop turns the HUD");
        const std::array<glm::vec2, 4> dot{glm::vec2{0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f}};
        check(!place(dot, [](glm::vec2 p) { return p * 1000.0f; }), "a collapsed panel is not drawn");
    }
    // Steady view: during a steady head turn the lag eases towards the margin and never reaches it, and it never
    // jumps from one frame to the next (the old hard stop pinned the view to the head mid-turn).
    {
        wuwa_steady_view::Smoother smoother;
        const float max_angle = 0.12f, fx = 1000.0f;
        float last_dx = 0.0f, last_step = 0.0f, worst_change = 0.0f, worst_offset = 0.0f;
        for (int i = 0; i < 400; ++i) {
            const double now = i / 90.0;
            const float yaw = 1.2f * static_cast<float>(now); // 1.2 rad/s: a quick turn
            const auto crop = smoother.update(glm::angleAxis(yaw, glm::vec3{0.0f, 1.0f, 0.0f}), now, 0.35f, max_angle, fx, fx);
            const float step = crop.dx - last_dx;
            if (i > 2) worst_change = (std::max)(worst_change, std::abs(step - last_step));
            last_step = step;
            worst_offset = (std::max)(worst_offset, std::abs(crop.dx));
            last_dx = crop.dx;
        }
        check(worst_offset < fx * std::tan(max_angle), "lag stays inside the margin");
        check(worst_offset > fx * std::tan(0.6f * max_angle), "a quick turn uses the eased part of the margin");
        // The head moves about 13 px a frame here. A hard stop at the margin dropped the crop's motion from about
        // 9 px a frame to 0 in one frame; the eased margin takes about ten frames (at most ~2.6 px change a frame).
        check(worst_change < 5.0f, "the crop's motion changes gradually (no snap at the margin)");
    }
    if (failures) return 1;
    std::printf("PASS: desktop HUD projection, placement and the steady view's eased margin\n");
    return 0;
}
