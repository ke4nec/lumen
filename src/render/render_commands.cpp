#include "lumen/render/render_commands.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

#include "lumen/core/geometry.h"

namespace lumen::render {
namespace {

// 小端序列化：固定 magic + 版本 + 数量，然后逐命令逐字段。反序列化对
// 任何长度/校验不匹配返回 false，调用方按损坏帧处理（全帧重绘）。
// v2：TextStyle 全字段（视觉系统后 resolved 样式带 weight/family 等区分
// 字段，v1 只存 fontSize/color 会丢失语义）。
// v3：TextRun 增加 baselinePx 与 shapedRuns（M1 布局/绘制共享 shaping）。
constexpr char kMagic[] = "LUMENCMD";
constexpr std::uint32_t kVersion = 3;

void putU8(std::string& out, std::uint8_t value) {
    out.push_back(static_cast<char>(value));
}

void putU32(std::string& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        putU8(out, static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFU));
    }
}

void putU64(std::string& out, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        putU8(out, static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFU));
    }
}

void putF32(std::string& out, float value) {
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "float must be 32-bit");
    std::memcpy(&bits, &value, sizeof(bits));
    putU32(out, bits);
}

void putBytes(std::string& out, const void* data, std::size_t size) {
    out.append(static_cast<const char*>(data), size);
}

void putString(std::string& out, const std::string& value) {
    putU32(out, static_cast<std::uint32_t>(value.size()));
    putBytes(out, value.data(), value.size());
}

struct Reader {
    const char* data{nullptr};
    std::size_t size{0};
    std::size_t offset{0};
    bool failed{false};

    std::uint8_t getU8() {
        if (failed || offset + 1 > size) {
            failed = true;
            return 0;
        }
        return static_cast<std::uint8_t>(data[offset++]);
    }

    std::uint32_t getU32() {
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            value |= static_cast<std::uint32_t>(getU8()) << (8 * i);
        }
        return value;
    }

    std::uint64_t getU64() {
        std::uint64_t value = 0;
        for (int i = 0; i < 8; ++i) {
            value |= static_cast<std::uint64_t>(getU8()) << (8 * i);
        }
        return value;
    }

    float getF32() {
        const std::uint32_t bits = getU32();
        float value = 0.0F;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    void getBytes(void* destination, std::size_t count) {
        if (failed || offset + count > size) {
            failed = true;
            return;
        }
        std::memcpy(destination, data + offset, count);
        offset += count;
    }

    std::string getString() {
        const std::uint32_t length = getU32();
        if (failed || length > size - offset) {
            failed = true;
            return {};
        }
        std::string value(static_cast<std::size_t>(length), '\0');
        getBytes(value.data(), length);
        return value;
    }
};

void serializeCommand(const RenderCommand& command, std::string& out) {
    putU8(out, static_cast<std::uint8_t>(command.type));
    putF32(out, command.alpha);
    putF32(out, command.transform.a);
    putF32(out, command.transform.b);
    putF32(out, command.transform.c);
    putF32(out, command.transform.d);
    putF32(out, command.transform.tx);
    putF32(out, command.transform.ty);

    const auto putRect = [&out](const core::Rect& rect) {
        putF32(out, rect.origin.x);
        putF32(out, rect.origin.y);
        putF32(out, rect.size.width);
        putF32(out, rect.size.height);
    };
    putRect(command.rect);
    putRect(command.bounds);
    putU8(out, command.hasBounds ? 1 : 0);
    putU8(out, command.color.r);
    putU8(out, command.color.g);
    putU8(out, command.color.b);
    putU8(out, command.color.a);
    putF32(out, command.radius.topLeft);
    putF32(out, command.radius.topRight);
    putF32(out, command.radius.bottomLeft);
    putF32(out, command.radius.bottomRight);
    putString(out, command.textRun.text);
    putF32(out, command.textRun.origin.x);
    putF32(out, command.textRun.origin.y);
    // v3：shaped 文本绘制数据（baseline + runs + glyphs）。
    putF32(out, command.textRun.baselinePx);
    putU32(out, static_cast<std::uint32_t>(
                     command.textRun.shapedRuns.size()));
    for (const TextGlyphRun& run : command.textRun.shapedRuns) {
        putString(out, run.family);
        putU8(out, run.placeholder ? 1 : 0);
        putU32(out, static_cast<std::uint32_t>(run.glyphs.size()));
        for (const text::ShapedGlyph& glyph : run.glyphs) {
            putU32(out, glyph.glyphId);
            putF32(out, glyph.advancePx);
            putF32(out, glyph.xOffsetPx);
            putU32(out, glyph.cluster);
        }
    }
    putF32(out, command.textStyle.fontSize);
    putU8(out, command.textStyle.color.r);
    putU8(out, command.textStyle.color.g);
    putU8(out, command.textStyle.color.b);
    putU8(out, command.textStyle.color.a);
    putU8(out, command.textStyle.bold ? 1 : 0);
    putString(out, command.textStyle.family);
    putU32(out, static_cast<std::uint32_t>(command.textStyle.weight));
    putU8(out, command.textStyle.italic ? 1 : 0);
    putF32(out, command.textStyle.letterSpacing);
    putF32(out, command.textStyle.lineHeight);
    putU8(out, static_cast<std::uint8_t>(command.textStyle.direction));
    putU32(out, static_cast<std::uint32_t>(command.textStyle.maxLines));
    putU8(out, static_cast<std::uint8_t>(command.textStyle.overflow));
    putU64(out, command.image);
    putU32(out, static_cast<std::uint32_t>(command.pixels.width));
    putU32(out, static_cast<std::uint32_t>(command.pixels.height));
    putU32(out, static_cast<std::uint32_t>(command.pixels.rgba.size()));
    putBytes(out, command.pixels.rgba.data(), command.pixels.rgba.size());
}

bool deserializeCommand(Reader& reader, RenderCommand& command) {
    command = {};
    command.type = static_cast<CommandType>(reader.getU8());
    switch (command.type) {
        case CommandType::Save:
        case CommandType::Restore:
        case CommandType::ClipRect:
        case CommandType::DrawRect:
        case CommandType::DrawText:
        case CommandType::DrawImage:
        case CommandType::UploadImage:
        case CommandType::UnloadImage:
            break;
        default:
            reader.failed = true;
            return false;
    }
    command.alpha = reader.getF32();
    command.transform.a = reader.getF32();
    command.transform.b = reader.getF32();
    command.transform.c = reader.getF32();
    command.transform.d = reader.getF32();
    command.transform.tx = reader.getF32();
    command.transform.ty = reader.getF32();

    const auto getRect = [&reader]() {
        core::Rect rect;
        rect.origin.x = reader.getF32();
        rect.origin.y = reader.getF32();
        rect.size.width = reader.getF32();
        rect.size.height = reader.getF32();
        return rect;
    };
    command.rect = getRect();
    command.bounds = getRect();
    command.hasBounds = reader.getU8() != 0;
    command.color.r = reader.getU8();
    command.color.g = reader.getU8();
    command.color.b = reader.getU8();
    command.color.a = reader.getU8();
    command.radius.topLeft = reader.getF32();
    command.radius.topRight = reader.getF32();
    command.radius.bottomLeft = reader.getF32();
    command.radius.bottomRight = reader.getF32();
    command.textRun.text = reader.getString();
    command.textRun.origin.x = reader.getF32();
    command.textRun.origin.y = reader.getF32();
    command.textRun.baselinePx = reader.getF32();
    const std::uint32_t runCount = reader.getU32();
    if (reader.failed || runCount > reader.size - reader.offset) {
        reader.failed = true;
        return false;
    }
    command.textRun.shapedRuns.resize(runCount);
    for (TextGlyphRun& run : command.textRun.shapedRuns) {
        run.family = reader.getString();
        run.placeholder = reader.getU8() != 0;
        const std::uint32_t glyphCount = reader.getU32();
        if (reader.failed || glyphCount > reader.size - reader.offset) {
            reader.failed = true;
            return false;
        }
        run.glyphs.resize(glyphCount);
        for (text::ShapedGlyph& glyph : run.glyphs) {
            glyph.glyphId = reader.getU32();
            glyph.advancePx = reader.getF32();
            glyph.xOffsetPx = reader.getF32();
            glyph.cluster = reader.getU32();
        }
    }
    command.textStyle.fontSize = reader.getF32();
    command.textStyle.color.r = reader.getU8();
    command.textStyle.color.g = reader.getU8();
    command.textStyle.color.b = reader.getU8();
    command.textStyle.color.a = reader.getU8();
    command.textStyle.bold = reader.getU8() != 0;
    command.textStyle.family = reader.getString();
    command.textStyle.weight = static_cast<int>(reader.getU32());
    command.textStyle.italic = reader.getU8() != 0;
    command.textStyle.letterSpacing = reader.getF32();
    command.textStyle.lineHeight = reader.getF32();
    command.textStyle.direction =
        static_cast<core::TextDirection>(reader.getU8());
    command.textStyle.maxLines =
        static_cast<std::size_t>(reader.getU32());
    command.textStyle.overflow =
        static_cast<core::TextOverflow>(reader.getU8());
    command.image = reader.getU64();
    command.pixels.width = static_cast<int>(reader.getU32());
    command.pixels.height = static_cast<int>(reader.getU32());
    const std::uint32_t byteCount = reader.getU32();
    if (reader.failed || byteCount > reader.size - reader.offset) {
        reader.failed = true;
        return false;
    }
    command.pixels.rgba.resize(byteCount);
    reader.getBytes(command.pixels.rgba.data(), byteCount);
    return !reader.failed;
}

}  // namespace

std::string serializeCommands(const RenderCommandList& list) {
    std::string out;
    out.reserve(64 + list.size() * 96);
    putBytes(out, kMagic, std::strlen(kMagic));
    putU32(out, kVersion);
    putU32(out, static_cast<std::uint32_t>(list.size()));
    for (const auto& command : list.commands()) {
        serializeCommand(command, out);
    }
    return out;
}

bool deserializeCommands(const std::string& blob, RenderCommandList& out) {
    Reader reader{blob.data(), blob.size(), 0, false};
    char magic[9] = {};
    if (blob.size() < std::strlen(kMagic) + 8) {
        return false;
    }
    reader.getBytes(magic, std::strlen(kMagic));
    if (std::memcmp(magic, kMagic, std::strlen(kMagic)) != 0) {
        return false;
    }
    if (reader.getU32() != kVersion) {
        return false;
    }
    const std::uint32_t count = reader.getU32();
    if (reader.failed || count > reader.size - reader.offset) {
        return false;
    }
    RenderCommandList parsed;
    parsed.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        RenderCommand command;
        if (!deserializeCommand(reader, command)) {
            return false;
        }
        // 拒绝尺寸与字节数不一致的像素负载，避免回放越界。
        if (!command.pixels.rgba.empty()) {
            const std::uint64_t expected =
                static_cast<std::uint64_t>(command.pixels.width) *
                static_cast<std::uint64_t>(command.pixels.height) * 4ULL;
            if (expected != command.pixels.rgba.size()) {
                return false;
            }
        }
        parsed.append(std::move(command));
    }
    if (reader.failed || reader.offset != reader.size) {
        return false;
    }
    out = std::move(parsed);
    return true;
}

RenderCommandList cullCommandsOutside(const RenderCommandList& list,
                                      core::Rect bounds) {
    RenderCommandList culled;
    culled.reserve(list.size());
    for (const auto& command : list.commands()) {
        switch (command.type) {
            case CommandType::DrawRect:
            case CommandType::DrawText:
            case CommandType::DrawImage:
                if (command.hasBounds &&
                    !command.bounds.intersects(bounds)) {
                    continue;
                }
                break;
            default:
                // 状态与资源命令保持原样：裁剪结构、上传语义都不能因
                // damage 缩水而改变。
                break;
        }
        culled.append(command);
    }
    return culled;
}

std::optional<core::Rect> commandDrawBounds(const RenderCommandList& list) {
    std::optional<core::Rect> bounds{};
    for (const auto& command : list.commands()) {
        if (!command.hasBounds) {
            switch (command.type) {
                case CommandType::DrawRect:
                case CommandType::DrawText:
                case CommandType::DrawImage:
                    return std::nullopt;
                default:
                    break;
            }
            continue;
        }
        switch (command.type) {
            case CommandType::DrawRect:
            case CommandType::DrawText:
            case CommandType::DrawImage:
                if (bounds.has_value()) {
                    *bounds = bounds->uniteWith(command.bounds);
                } else {
                    bounds = command.bounds;
                }
                break;
            default:
                break;
        }
    }
    return bounds;
}

}  // namespace lumen::render
