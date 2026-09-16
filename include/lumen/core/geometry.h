#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

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

    friend Offset operator+(const Offset& a, const Offset& b) {
        return Offset{a.x + b.x, a.y + b.y};
    }
    friend Offset operator-(const Offset& a, const Offset& b) {
        return Offset{a.x - b.x, a.y - b.y};
    }

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

        // True when the two half-open areas overlap (v0.2 阶段7B: 命令与
        // damage 的相交测试)。
        [[nodiscard]] bool intersects(const Rect& other) const {
            return left() < other.right() && other.left() < right() &&
                   top() < other.bottom() && other.top() < bottom();
        }

        // Smallest rect covering both areas; empty inputs are ignored so the
        // union with an empty rect stays empty.
        [[nodiscard]] Rect uniteWith(const Rect& other) const {
            if (size.width <= 0.0F || size.height <= 0.0F) {
                return other;
            }
            if (other.size.width <= 0.0F || other.size.height <= 0.0F) {
                return *this;
            }
            const float x0 = std::min(left(), other.left());
            const float y0 = std::min(top(), other.top());
            const float x1 = std::max(right(), other.right());
            const float y1 = std::max(bottom(), other.bottom());
            return Rect{Offset{x0, y0}, Size{x1 - x0, y1 - y0}};
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

// 文本溢出策略（v0.3 阶段8B 冻结属性）。Fade 在布局中度量同 Ellipsis。
enum class TextOverflow : std::uint8_t { Clip, Ellipsis, Fade, Visible };

// 段落方向（v0.3 阶段8B）。RTL 采用逐 cluster 视觉逆序的确定性近似。
enum class TextDirection : std::uint8_t { Ltr, Rtl };

struct TextStyle {
    float fontSize{14.0F};
    Color color{Color::fromRGBA(0, 0, 0)};
    bool bold{false};
    // v0.3 阶段8B 冻结属性（plan §3.2）。
    std::string family{};
    int weight{400};  // 100..900（FontWeight 的整数值）
    bool italic{false};
    float letterSpacing{0.0F};
    // 相邻行基线距离 / fontSize；0 = 默认 1.2。字形绘制框独立扩展，
    // 紧行高允许行框重叠，不强制放大该倍数。
    float lineHeight{0.0F};
    TextDirection direction{TextDirection::Ltr};
    std::size_t maxLines{0};  // 0 = 不限
    TextOverflow overflow{TextOverflow::Clip};

    bool operator==(const TextStyle& other) const = default;
};

}  // namespace lumen::core
