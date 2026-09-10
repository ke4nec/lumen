#include "lumen/render/resource_manager.h"

#include <algorithm>
#include <utility>

#include "image_decode.h"

namespace lumen::render {

namespace {
// ImageId 顶部标记位：与 CpuRenderer::registerImage 的自增 id 空间隔离。
constexpr std::uint64_t kImageIdBase = 0x4000000000000000ULL;
}

ResourceManager::ResourceManager(Config config) : config_(config) {
    if (config_.workerCount == 0) {
        config_.workerCount = 1;
    }
    if (config_.maxCacheBytes == 0) {
        config_.maxCacheBytes = 1;
    }
    workers_.reserve(config_.workerCount);
    for (std::uint32_t i = 0; i < config_.workerCount; ++i) {
        workers_.emplace_back([this] { workerLoop(); });
    }
}

ResourceManager::~ResourceManager() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shuttingDown_ = true;
        queue_.clear();
        finished_.clear();
    }
    wake_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

ResourceHandle ResourceManager::makeHandle(const Slot& slot,
                                           std::size_t index) const {
    ResourceHandle handle;
    handle.index = static_cast<std::uint32_t>(index + 1);
    handle.generation = slot.generation;
    return handle;
}

// 找一个可复用槽位（非 Loading/Ready），复用时代数严格 +1；没有则追加。
std::size_t ResourceManager::acquireSlot() {
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        const std::size_t probe = (freeCursor_ + i) % slots_.size();
        const ResourceState state = slots_[probe].state;
        if (state != ResourceState::Loading && state != ResourceState::Ready) {
            const std::uint32_t nextGeneration = slots_[probe].generation + 1;
            slots_[probe] = Slot{};
            slots_[probe].generation = nextGeneration;
            freeCursor_ = (probe + 1) % slots_.size();
            return probe;
        }
    }
    slots_.emplace_back();
    return slots_.size() - 1;
}

ResourceHandle ResourceManager::requestImage(const std::string& path,
                                             core::WindowId window) {
    const std::size_t index = acquireSlot();
    Slot& slot = slots_[index];
    slot.state = ResourceState::Loading;
    slot.path = path;
    slot.window = window;
    diagnostics_.requested += 1;

    Job job;
    job.index = static_cast<std::uint32_t>(index);
    job.generation = slot.generation;
    job.path = path;
    enqueueJob(std::move(job));
    return makeHandle(slot, index);
}

ResourceHandle ResourceManager::registerImage(PixelBuffer image) {
    const std::size_t index = acquireSlot();
    Slot& slot = slots_[index];
    slot.state = ResourceState::Ready;
    slot.pixels = std::move(image);
    slot.bytes = slot.pixels.rgba.size();
    slot.lastUse = ++useCounter_;
    slot.needsUpload = true;
    cacheBytes_ += slot.bytes;
    diagnostics_.requested += 1;
    diagnostics_.loads += 1;
    evictForBudget();
    return makeHandle(slot, index);
}

void ResourceManager::enqueueJob(Job job) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(std::move(job));
    }
    wake_.notify_one();
}

void ResourceManager::workerLoop() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return shuttingDown_ || !queue_.empty(); });
            if (shuttingDown_) {
                return;
            }
            job = std::move(queue_.front());
            queue_.erase(queue_.begin());
        }
        // 受限工作：只读取文件并解码为不可变 CPU 数据（plan §3.3）。
        Result result;
        result.index = job.index;
        result.generation = job.generation;
        result.error = detail::decodeImageFile(job.path, result.pixels);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!shuttingDown_) {
                finished_.push_back(std::move(result));
            }
        }
    }
}

std::vector<ResourceManager::Completion> ResourceManager::pumpCompletions() {
    std::vector<Result> results;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        results.swap(finished_);
    }
    std::vector<Completion> completions;
    completions.reserve(results.size());
    for (const auto& result : results) {
        completions.push_back(applyResult(result));
    }
    return completions;
}

ResourceManager::Completion ResourceManager::applyResult(const Result& result) {
    Completion completion;
    completion.handle.index = result.index + 1;
    completion.handle.generation = result.generation;
    completion.error = result.error;

    if (result.index >= slots_.size()) {
        completion.result = ResourceState::Cancelled;
        completion.error = ResourceError::InvalidHandle;
        return completion;
    }
    Slot& slot = slots_[result.index];
    completion.window = slot.window;
    if (slot.generation != result.generation ||
        slot.state != ResourceState::Loading) {
        // 旧代完成事件：资源已被释放/取消/复用，不能复活（plan §2.3）。
        diagnostics_.staleCompletions += 1;
        completion.result = ResourceState::Cancelled;
        completion.error = ResourceError::InvalidHandle;
        return completion;
    }
    if (result.error == ResourceError::None) {
        slot.state = ResourceState::Ready;
        slot.pixels = std::move(result.pixels);
        slot.bytes = slot.pixels.rgba.size();
        slot.lastUse = ++useCounter_;
        slot.needsUpload = true;
        cacheBytes_ += slot.bytes;
        diagnostics_.loads += 1;
        completion.result = ResourceState::Ready;
        evictForBudget();
    } else {
        slot.state = ResourceState::Failed;
        diagnostics_.failures += 1;
        completion.result = ResourceState::Failed;
    }
    return completion;
}

void ResourceManager::evictForBudget() {
    if (cacheBytes_ <= config_.maxCacheBytes) {
        return;
    }
    // LRU：按 lastUse 升序淘汰 Ready 资源。已上传的记一条 unload 命令
    // （用上传时的 ImageId，代数推进前的 id）。
    std::vector<std::size_t> candidates;
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].state == ResourceState::Ready) {
            candidates.push_back(i);
        }
    }
    std::sort(candidates.begin(), candidates.end(),
              [this](std::size_t a, std::size_t b) {
                  return slots_[a].lastUse < slots_[b].lastUse;
              });
    for (const std::size_t index : candidates) {
        if (cacheBytes_ <= config_.maxCacheBytes) {
            break;
        }
        Slot& slot = slots_[index];
        if (slot.uploaded) {
            unloadQueue_.push_back(imageId(makeHandle(slot, index)));
        }
        cacheBytes_ -= slot.bytes;
        slot.bytes = 0;
        slot.pixels = PixelBuffer{};
        slot.state = ResourceState::Evicted;
        slot.needsUpload = false;
        slot.uploaded = false;
        slot.generation += 1;  // 句柄随之失效
        diagnostics_.evictions += 1;
    }
}

void ResourceManager::release(ResourceHandle handle) {
    if (handle.index == 0 || handle.index > slots_.size()) {
        return;
    }
    Slot& slot = slots_[handle.index - 1];
    if (slot.generation != handle.generation) {
        return;
    }
    if (slot.state == ResourceState::Loading) {
        slot.state = ResourceState::Cancelled;
        diagnostics_.cancels += 1;
    } else if (slot.state == ResourceState::Ready) {
        if (slot.uploaded) {
            unloadQueue_.push_back(imageId(handle));
        }
        cacheBytes_ -= slot.bytes;
        slot.bytes = 0;
        slot.pixels = PixelBuffer{};
        slot.state = ResourceState::Evicted;
        slot.needsUpload = false;
        slot.uploaded = false;
        diagnostics_.evictions += 1;
    }
    // 代数推进：在途 worker 完成与旧句柄一起失效。
    slot.generation += 1;
}

void ResourceManager::appendUploads(RenderCommandList& list) {
    for (const ImageId id : unloadQueue_) {
        list.unloadImage(id);
    }
    unloadQueue_.clear();
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        Slot& slot = slots_[i];
        if (slot.state == ResourceState::Ready && slot.needsUpload) {
            list.uploadImage(imageId(makeHandle(slot, i)), slot.pixels);
            slot.needsUpload = false;
            slot.uploaded = true;
            diagnostics_.uploads += 1;
        }
    }
}

void ResourceManager::handleDeviceRebuilt() {
    for (auto& slot : slots_) {
        if (slot.state == ResourceState::Ready) {
            slot.needsUpload = true;
            slot.uploaded = false;
            diagnostics_.reuploads += 1;
        }
    }
}

ResourceState ResourceManager::state(ResourceHandle handle) const {
    const Slot* slot = findSlot(handle);
    return slot != nullptr ? slot->state : ResourceState::Evicted;
}

bool ResourceManager::ready(ResourceHandle handle) const {
    return state(handle) == ResourceState::Ready;
}

const PixelBuffer* ResourceManager::pixels(ResourceHandle handle) {
    if (handle.index == 0 || handle.index > slots_.size()) {
        return nullptr;
    }
    Slot& slot = slots_[handle.index - 1];
    if (slot.generation != handle.generation ||
        slot.state != ResourceState::Ready) {
        return nullptr;
    }
    slot.lastUse = ++useCounter_;
    return &slot.pixels;
}

ImageId ResourceManager::imageId(ResourceHandle handle) const {
    if (!handle.valid()) {
        return 0;
    }
    return kImageIdBase |
           (static_cast<ImageId>(handle.index) << 32U) |
           static_cast<ImageId>(handle.generation);
}

std::size_t ResourceManager::liveCount() const {
    std::size_t count = 0;
    for (const auto& slot : slots_) {
        if (slot.state == ResourceState::Loading ||
            slot.state == ResourceState::Ready ||
            slot.state == ResourceState::Failed) {
            ++count;
        }
    }
    return count;
}

const ResourceManager::Slot* ResourceManager::findSlot(
    ResourceHandle handle) const {
    if (handle.index == 0 || handle.index > slots_.size()) {
        return nullptr;
    }
    const Slot& slot = slots_[handle.index - 1];
    if (slot.generation != handle.generation) {
        return nullptr;
    }
    return &slot;
}

}  // namespace lumen::render
