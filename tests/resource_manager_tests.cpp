// v0.2 阶段7D tests (plan §5 资源): 并发加载、取消、失败、淘汰、代际
// 句柄、设备重建重新上传和析构清理。PNG 解码用 stb_image_write 现场生成
// 输入，原始 .lumenrgba 格式直接手写。

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "lumen/render/cpu_renderer.h"
#include "lumen/render/render_commands.h"
#include "lumen/render/resource_manager.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

using lumen::render::CpuRenderer;
using lumen::render::CommandType;
using lumen::render::FrameInfo;
using lumen::render::PixelBuffer;
using lumen::render::RenderCommandList;
using lumen::render::ResourceManager;
using lumen::render::ResourceError;
using lumen::render::ResourceHandle;
using lumen::render::ResourceState;

namespace {

PixelBuffer solidImage(int width, int height, std::uint8_t red) {
    PixelBuffer image;
    image.width = width;
    image.height = height;
    image.rgba.resize(static_cast<std::size_t>(width) * height * 4);
    for (std::size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
        image.rgba[i] = red;
        image.rgba[i + 1] = 64;
        image.rgba[i + 2] = 128;
        image.rgba[i + 3] = 255;
    }
    return image;
}

// 临时 PNG 文件：测试结束删除。
class TempFile {
  public:
    explicit TempFile(std::string name) : path_(std::move(name)) {}
    ~TempFile() { std::remove(path_.c_str()); }
    [[nodiscard]] const std::string& path() const { return path_; }

  private:
    std::string path_;
};

bool writePng(const std::string& path, const PixelBuffer& image) {
    const int stride = image.width * 4;
    return stbi_write_png(path.c_str(), image.width, image.height, 4,
                          image.rgba.data(), stride) != 0;
}

std::string writeRawFile(const std::string& path, const PixelBuffer& image) {
    std::ofstream out(path, std::ios::binary);
    out << "LUMENRGBA\n" << image.width << " " << image.height << "\n";
    out.write(reinterpret_cast<const char*>(image.rgba.data()),
              static_cast<std::streamsize>(image.rgba.size()));
    return path;
}

// pumpCompletions 轮询直到谓词满足或超时（worker 异步完成）。
template <typename Predicate>
bool waitUntil(ResourceManager& manager, Predicate&& predicate,
               int timeoutMs = 3000) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        (void)manager.pumpCompletions();
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

}  // namespace

TEST_CASE("resource_manager_loads_png_asynchronously", "[resource]") {
    TempFile png("lumen-test-asset.png");
    REQUIRE(writePng(png.path(), solidImage(4, 4, 200)));

    ResourceManager manager;
    const ResourceHandle handle = manager.requestImage(png.path());
    CHECK(handle.valid());
    CHECK(manager.state(handle) == ResourceState::Loading);
    CHECK(manager.pixels(handle) == nullptr);

    REQUIRE(waitUntil(manager, [&] { return manager.ready(handle); }));
    const PixelBuffer* pixels = manager.pixels(handle);
    REQUIRE(pixels != nullptr);
    CHECK(pixels->width == 4);
    CHECK(pixels->height == 4);
    CHECK(pixels->rgba.size() == 64);
    CHECK(pixels->rgba[0] == 200);
    CHECK(pixels->alphaMode == lumen::render::AlphaMode::Straight);
    CHECK(manager.diagnostics().loads == 1);
}

TEST_CASE("resource_manager_loads_raw_format", "[resource]") {
    TempFile raw("lumen-test-asset.lumenrgba");
    writeRawFile(raw.path(), solidImage(3, 2, 90));

    ResourceManager manager;
    const ResourceHandle handle = manager.requestImage(raw.path());
    REQUIRE(waitUntil(manager, [&] { return manager.ready(handle); }));
    const PixelBuffer* pixels = manager.pixels(handle);
    REQUIRE(pixels != nullptr);
    CHECK(pixels->width == 3);
    CHECK(pixels->height == 2);
    CHECK(pixels->rgba.size() == 24);
    CHECK(pixels->rgba[0] == 90);
    CHECK(pixels->alphaMode == lumen::render::AlphaMode::Straight);
}

TEST_CASE("resource_manager_reports_missing_file_failure", "[resource]") {
    ResourceManager manager;
    const ResourceHandle handle = manager.requestImage("does-not-exist.png");
    REQUIRE(waitUntil(manager, [&] {
        return manager.state(handle) == ResourceState::Failed;
    }));
    CHECK(manager.pixels(handle) == nullptr);
    CHECK(manager.diagnostics().failures == 1);
    // 失败不阻塞：状态可查询、循环继续。
    CHECK(manager.liveCount() == 1);
}

TEST_CASE("resource_manager_reports_corrupt_data_failure", "[resource]") {
    TempFile corrupt("lumen-test-corrupt.png");
    {
        std::ofstream out(corrupt.path(), std::ios::binary);
        out << "not a png at all";
    }
    ResourceManager manager;
    const ResourceHandle handle = manager.requestImage(corrupt.path());
    REQUIRE(waitUntil(manager, [&] {
        return manager.state(handle) == ResourceState::Failed;
    }));
}

TEST_CASE("resource_manager_cancel_drops_inflight_result", "[resource]") {
    // 大文件让 worker 有可乘之机的窗口；即便文件很小，取消语义仍必须
    // 成立：release 后句柄失效，完成事件按旧代丢弃。
    TempFile png("lumen-test-big.png");
    REQUIRE(writePng(png.path(), solidImage(64, 64, 10)));

    ResourceManager manager;
    const ResourceHandle handle = manager.requestImage(png.path());
    manager.release(handle);
    // 旧代句柄：查询落到"未找到"（Evicted 语义），像素不可用。
    CHECK(manager.state(handle) == ResourceState::Evicted);
    CHECK(manager.pixels(handle) == nullptr);
    CHECK(manager.diagnostics().cancels == 1);

    REQUIRE(waitUntil(manager, [&] {
        return manager.diagnostics().staleCompletions >= 1;
    }));
    // 旧句柄不能复活。
    CHECK(manager.state(handle) != ResourceState::Ready);
    CHECK(manager.pixels(handle) == nullptr);
}

TEST_CASE("resource_manager_generational_handles_invalidate_on_release",
          "[resource]") {
    ResourceManager manager;
    const ResourceHandle first = manager.registerImage(solidImage(2, 2, 5));
    REQUIRE(manager.ready(first));
    manager.release(first);
    CHECK(manager.state(first) != ResourceState::Ready);

    // 槽位复用后旧句柄仍失效（代数不同）。
    const ResourceHandle second = manager.registerImage(solidImage(2, 2, 6));
    CHECK(second.index == first.index);
    CHECK(second.generation != first.generation);
    CHECK_FALSE(manager.ready(first));
    CHECK(manager.ready(second));
}

TEST_CASE("resource_manager_evicts_over_budget", "[resource]") {
    const std::size_t one = solidImage(4, 4, 1).rgba.size();
    ResourceManager::Config config;
    config.maxCacheBytes = one * 2;  // 容得下两张
    ResourceManager manager{config};

    const ResourceHandle old = manager.registerImage(solidImage(4, 4, 1));
    const ResourceHandle keep = manager.registerImage(solidImage(4, 4, 2));
    CHECK(manager.ready(old));
    CHECK(manager.ready(keep));
    (void)manager.pixels(keep);  // keep 刷新为最近使用

    const ResourceHandle fresh = manager.registerImage(solidImage(4, 4, 3));
    // 超预算：最久未用的 old 被淘汰，keep/fresh 留存。
    CHECK(manager.state(old) == ResourceState::Evicted);
    CHECK(manager.pixels(old) == nullptr);
    CHECK(manager.ready(keep));
    CHECK(manager.ready(fresh));
    CHECK(manager.diagnostics().evictions == 1);
    CHECK(manager.cacheBytes() <= config.maxCacheBytes);
}

TEST_CASE("resource_manager_uploads_and_reuploads_survive_device_rebuild",
          "[resource]") {
    ResourceManager manager;
    const ResourceHandle handle = manager.registerImage(solidImage(3, 3, 77));

    RenderCommandList frame1;
    manager.appendUploads(frame1);
    REQUIRE(frame1.size() == 1);
    CHECK(frame1.commands()[0].type == CommandType::UploadImage);
    CHECK(frame1.commands()[0].pixels.rgba[0] == 77);

    // 已上传：下一帧没有重复 upload。
    RenderCommandList frame2;
    manager.appendUploads(frame2);
    CHECK(frame2.empty());

    // 设备重建：同 ImageId 重新排队上传。
    const lumen::render::ImageId id = manager.imageId(handle);
    manager.handleDeviceRebuilt();
    RenderCommandList frame3;
    manager.appendUploads(frame3);
    REQUIRE(frame3.size() == 1);
    CHECK(frame3.commands()[0].type == CommandType::UploadImage);
    CHECK(frame3.commands()[0].image == id);
    CHECK(manager.diagnostics().reuploads == 1);

    // release 已上传资源 → unload 命令引用上传时的 id。
    manager.release(handle);
    RenderCommandList frame4;
    manager.appendUploads(frame4);
    REQUIRE(frame4.size() == 1);
    CHECK(frame4.commands()[0].type == CommandType::UnloadImage);
    CHECK(frame4.commands()[0].image == id);
}

TEST_CASE("resource_manager_upload_commands_replay_into_renderer", "[resource]") {
    ResourceManager manager;
    const ResourceHandle handle = manager.registerImage(solidImage(2, 2, 150));
    const lumen::render::ImageId id = manager.imageId(handle);

    RenderCommandList list;
    manager.appendUploads(list);
    list.drawImage(id, lumen::core::Rect::fromXYWH(0.0F, 0.0F, 40.0F, 40.0F));

    CpuRenderer renderer;
    FrameInfo info;
    info.viewport = lumen::core::Size{40.0F, 40.0F};
    renderer.submit(list, info);
    CHECK(renderer.stats().uploads == 1);
}

TEST_CASE("resource_manager_destructor_joins_workers_cleanly", "[resource]") {
    TempFile png("lumen-test-shutdown.png");
    REQUIRE(writePng(png.path(), solidImage(8, 8, 30)));
    {
        ResourceManager::Config config;
        config.workerCount = 3;
        ResourceManager manager{config};
        for (int i = 0; i < 16; ++i) {
            (void)manager.requestImage(png.path());
        }
        // 不 pump、不等待：带着在途任务直接析构（析构时队列清理）。
    }
    SUCCEED("destructor joined all workers without deadlock");
}

TEST_CASE("resource_manager_concurrent_requests_all_complete", "[resource]") {
    TempFile png("lumen-test-many.png");
    REQUIRE(writePng(png.path(), solidImage(4, 4, 40)));

    ResourceManager::Config config;
    config.workerCount = 2;
    ResourceManager manager{config};
    std::vector<ResourceHandle> handles;
    for (int i = 0; i < 12; ++i) {
        handles.push_back(manager.requestImage(png.path()));
    }
    REQUIRE(waitUntil(manager, [&] {
        for (const auto& handle : handles) {
            if (!manager.ready(handle)) {
                return false;
            }
        }
        return true;
    }));
    CHECK(manager.diagnostics().loads == 12);
    CHECK(manager.liveCount() == 12);
}
