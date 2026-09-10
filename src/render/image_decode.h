#pragma once

#include <string>

#include "lumen/render/renderer.h"
#include "lumen/render/resource_manager.h"

namespace lumen::render::detail {

// 在调用线程（资源 worker）解码图片文件为 straight RGBA 像素。
[[nodiscard]] ResourceError decodeImageFile(const std::string& path,
                                            PixelBuffer& out);

}  // namespace lumen::render::detail
