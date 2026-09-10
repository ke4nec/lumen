#pragma once

#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "lumen/render/render_commands.h"
#include "lumen/render/renderer.h"

namespace lumen::render {

// v0.2 阶段7D (plan §3.3): 资源管理。
//
// 后台 worker 只执行受限文件读取、图片解码和校验，产出不可变 CPU 数据；
// UI 线程通过 pumpCompletions() 迁移状态、通过 appendUploads() 生成
// upload/unload 命令——GPU 对象只在 Renderer 所属线程创建和销毁。
//
// 句柄带代（generation）：release/取消立刻推进代数，异步完成如果携带
// 旧代就丢弃，不能复活已释放资源（plan §2.3 不变量）。
//
// 资源缓存有字节上限（LRU 淘汰到 Evicted）；失败进入诊断计数，绝不
// 阻塞事件循环。设备重建后所有 Ready 资源重新排队上传，ImageId 不变。

enum class ResourceState : std::uint8_t {
    Loading,
    Ready,
    Failed,
    Cancelled,
    Evicted,
};

enum class ResourceError : std::uint8_t {
    None,
    FileOpenFailed,
    DecodeFailed,
    InvalidHandle,
};

struct ResourceHandle {
    std::uint32_t index{0};      // 0 = invalid
    std::uint32_t generation{0};

    [[nodiscard]] bool valid() const { return index != 0; }
    [[nodiscard]] bool operator==(const ResourceHandle&) const = default;
};

class ResourceManager {
  public:
    struct Config {
        // CPU 缓存字节上限；超出按 LRU 淘汰到 Evicted。
        std::size_t maxCacheBytes{64U << 20U};
        // 后台 worker 数；0 视为 1。
        std::uint32_t workerCount{1};
    };

    struct Completion {
        ResourceHandle handle;
        ResourceState result{ResourceState::Failed};
        ResourceError error{ResourceError::None};
    };

    struct Diagnostics {
        std::uint64_t requested{0};
        std::uint64_t loads{0};
        std::uint64_t failures{0};
        std::uint64_t cancels{0};
        std::uint64_t evictions{0};
        std::uint64_t uploads{0};
        std::uint64_t reuploads{0};
        std::uint64_t staleCompletions{0};
    };

    explicit ResourceManager(Config config = {});
    ~ResourceManager();

    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    // 异步加载图片（PNG/JPEG 由 stb_image 解码；.lumenrgba 原始格式直读）。
    ResourceHandle requestImage(const std::string& path);
    // 同步注册已解码像素（不经 worker；状态立即 Ready）。
    ResourceHandle registerImage(PixelBuffer pixels);

    // UI 线程 pump：应用 worker 完成事件（代数校验、缓存预算、上传排
    // 队）并返回本批完成项。
    std::vector<Completion> pumpCompletions();

    [[nodiscard]] ResourceState state(ResourceHandle handle) const;
    [[nodiscard]] bool ready(ResourceHandle handle) const;
    // Ready 资源的像素；其他状态返回 nullptr（应用绘制固定占位内容）。
    // 非 const：访问会刷新 LRU 时间戳。
    [[nodiscard]] const PixelBuffer* pixels(ResourceHandle handle);
    // 命令引用的稳定 ImageId（UploadImage/DrawImage）。
    [[nodiscard]] ImageId imageId(ResourceHandle handle) const;

    // 释放：Loading → Cancelled（worker 完成时丢弃）；Ready → Evicted。
    // 立刻推进代数，旧句柄随之失效。
    void release(ResourceHandle handle);

    // 把 pending 的 upload/unload 增量追加进命令列表（每帧调用一次）。
    void appendUploads(RenderCommandList& list);

    // GPU 设备重建：全部 Ready 资源重新排队上传（ImageId 不变）。
    void handleDeviceRebuilt();

    [[nodiscard]] std::uint64_t cacheBytes() const { return cacheBytes_; }
    [[nodiscard]] std::size_t liveCount() const;
    [[nodiscard]] const Diagnostics& diagnostics() const { return diagnostics_; }

  private:
    struct Slot {
        ResourceState state{ResourceState::Loading};
        std::uint32_t generation{1};
        std::string path{};
        PixelBuffer pixels{};
        std::uint64_t bytes{0};
        std::uint64_t lastUse{0};
        // 待写入下一帧命令的上传；uploaded 记录已上传（需要 unload）。
        bool needsUpload{false};
        bool uploaded{false};
    };

    struct Job {
        std::uint32_t index{0};
        std::uint32_t generation{0};
        std::string path{};
    };

    struct Result {
        std::uint32_t index{0};
        std::uint32_t generation{0};
        PixelBuffer pixels{};
        ResourceError error{ResourceError::None};
    };

    [[nodiscard]] ResourceHandle makeHandle(const Slot& slot,
                                            std::size_t index) const;
    // 复用已终结槽位（代数 +1）或追加新槽位。
    [[nodiscard]] std::size_t acquireSlot();
    void enqueueJob(Job job);
    void workerLoop();
    [[nodiscard]] Completion applyResult(const Result& result);
    void evictForBudget();
    [[nodiscard]] const Slot* findSlot(ResourceHandle handle) const;

    Config config_{};
    std::vector<Slot> slots_{};
    std::size_t freeCursor_{0};
    // 已上传资源失效时记录的 unload 命令（引用上传时的 ImageId）。
    std::vector<ImageId> unloadQueue_{};

    std::vector<Job> queue_{};
    std::vector<Result> finished_{};
    std::mutex mutex_{};
    std::condition_variable wake_{};
    std::vector<std::thread> workers_{};
    bool shuttingDown_{false};

    std::uint64_t cacheBytes_{0};
    mutable std::uint64_t useCounter_{0};
    Diagnostics diagnostics_{};
};

}  // namespace lumen::render
