#pragma once

#include <algorithm>
#include <cstdint>

namespace lumen::core {

// Logical-coordinate geometry. All layout uses float logical pixels;
// platform/render boundary converts to framebuffer pixels.
struct Size {
    float width{0.0F};
    float height{0.0F};

    bool operator==(const Size& other) const = default;
};

struct Offset {
    float x{0.0F};
    float y{0.0F};

    bool operator==(const Offset& other) const = default;
};

struct Rect {
    Offset origin{};
    Size size{};

    static Rect fromXYWH(float x, float y, float width, float height) {
        return Rect{Offset{x, y}, Size{width, height}};
    }

    float left() const { return origin.x; }
    float top() const { return origin.y; }
    float right() const { return origin.x + size.width; }
    float bottom() const { return origin.y + size.height; }

    // Half-open containment: [left, right) x [top, bottom).
    // Used by hit testing in Stage 3; edge pixels belong to one node only.
    bool contains(Offset point) const {
        return point.x >= left() && point.x < right() && point.y >= top() &&
               point.y < bottom();
    }

    bool operator==(const Rect& other) const = default;
};

struct EdgeInsets {
    float left{0.0F};
    float top{0.0F};
    float right{0.0F};
    float bottom{0.0F};

    static EdgeInsets all(float value) {
        return EdgeInsets{value, value, value, value};
    }
    static EdgeInsets symmetric(float horizontal, float vertical) {
        return EdgeInsets{horizontal, vertical, horizontal, vertical};
    }
    static EdgeInsets only(float left = 0.0F, float top = 0.0F,
                           float right = 0.0F, float bottom = 0.0F) {
        return EdgeInsets{left, top, right, bottom};
    }

    float horizontal() const { return left + right; }
    float vertical() const { return top + bottom; }

    bool operator==(const EdgeInsets& other) const = default;
};

struct Constraints {
    float minWidth{0.0F};
    float maxWidth{0.0F};
    float minHeight{0.0F};
    float maxHeight{0.0F};

    static Constraints tight(Size size) {
        return Constraints{size.width, size.width, size.height, size.height};
    }
    static Constraints loose(Size size) {
        return Constraints{0.0F, size.width, 0.0F, size.height};
    }
    // Unbounded main axis uses a large finite value; isBounded*() treats
    // anything below 1e8 as bounded so flex degrades gracefully.
    static Constraints unbounded() {
        constexpr float kInfinity = 1e9F;
        return Constraints{0.0F, kInfinity, 0.0F, kInfinity};
    }

    bool isTight() const {
        return minWidth == maxWidth && minHeight == maxHeight;
    }
    bool isBoundedWidth() const { return maxWidth < 1e8F; }
    bool isBoundedHeight() const { return maxHeight < 1e8F; }

    Size biggest() const { return Size{maxWidth, maxHeight}; }
    Size smallest() const { return Size{minWidth, minHeight}; }

    Size constrain(Size size) const {
        return Size{
            std::clamp(size.width, minWidth, maxWidth),
            std::clamp(size.height, minHeight, maxHeight),
        };
    }

    Constraints deflate(EdgeInsets insets) const {
        const float deflatedMaxWidth =
            std::max(0.0F, maxWidth - insets.horizontal());
        const float deflatedMaxHeight =
            std::max(0.0F, maxHeight - insets.vertical());
        const float deflatedMinWidth =
            std::clamp(minWidth - insets.horizontal(), 0.0F, deflatedMaxWidth);
        const float deflatedMinHeight =
            std::clamp(minHeight - insets.vertical(), 0.0F, deflatedMaxHeight);
        return Constraints{
            deflatedMinWidth, deflatedMaxWidth, deflatedMinHeight,
            deflatedMaxHeight};
    }

    bool operator==(const Constraints& other) const = default;
};

struct Color {
    std::uint8_t r{0};
    std::uint8_t g{0};
    std::uint8_t b{0};
    std::uint8_t a{255};

    static Color fromRGBA(std::uint8_t red, std::uint8_t green,
                          std::uint8_t blue, std::uint8_t alpha = 255) {
        return Color{red, green, blue, alpha};
    }
    static Color transparent() { return Color{0, 0, 0, 0}; }

    bool operator==(const Color& other) const = default;
};

struct CornerRadius {
    float topLeft{0.0F};
    float topRight{0.0F};
    float bottomLeft{0.0F};
    float bottomRight{0.0F};

    static CornerRadius all(float radius) {
        return CornerRadius{radius, radius, radius, radius};
    }
    static CornerRadius zero() { return CornerRadius{}; }

    bool operator==(const CornerRadius& other) const = default;
};

struct TextStyle {
    float fontSize{14.0F};
    Color color{Color::fromRGBA(0, 0, 0)};
    bool bold{false};

    bool operator==(const TextStyle& other) const = default;
};

}  // namespace lumen::core
