// G-8：单实例助手实现（契约见 single_instance.h 与
// docs/lumen-window-experience-design.md §4）。POSIX unix domain socket：
// bind 占用 = Primary；EADDRINUSE = Secondary（connect 送达激活请求）。
// 监听线程为守护线程（进程退出即回收）；socket 文件随守卫析构清理。

#include "lumen/core/single_instance.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace lumen::core {

namespace {

struct PrimaryState {
    int listenFd{-1};
    std::string socketPath{};
    std::thread acceptThread{};
};

std::atomic<bool> acceptRunning{false};

std::string defaultSocketDirectory() {
    if (const char* runtime = std::getenv("XDG_RUNTIME_DIR");
        runtime != nullptr && *runtime != '\0') {
        return runtime;
    }
    if (const char* tmp = std::getenv("TMPDIR"); tmp != nullptr &&
                                                  *tmp != '\0') {
        return tmp;
    }
    return "/tmp";
}

std::string socketPathFor(const SingleInstanceGuard::Config& config) {
    const std::string dir =
        config.socketDirectory.empty() ? defaultSocketDirectory()
                                       : config.socketDirectory;
    return dir + "/lumen-" + config.appName + ".single-instance";
}

void acceptLoop(int listenFd, const std::function<void()>& onActivate) {
    while (acceptRunning.load(std::memory_order_acquire)) {
        const int conn = ::accept(listenFd, nullptr, nullptr);
        if (conn < 0) {
            if (!acceptRunning.load(std::memory_order_acquire)) {
                return;  // 关闭路径的正常 EINTR/EINVAL
            }
            continue;
        }
        char buffer[16] = {};
        (void)::read(conn, buffer, sizeof(buffer) - 1);
        ::close(conn);
        if (onActivate) {
            onActivate();
        }
    }
}

}  // namespace

SingleInstanceGuard::Status SingleInstanceGuard::acquire(
    const Config& config) {
    if (config.appName.empty()) {
        return Status::Unavailable;
    }
    const std::string path = socketPathFor(config);
    if (path.size() >= sizeof(sockaddr_un::sun_path)) {
        return Status::Unavailable;
    }
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return Status::Unavailable;
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::snprintf(address.sun_path, sizeof(address.sun_path), "%s",
                  path.c_str());
    // 先试连接：已有实例 → 送达激活请求（Secondary 语义）。
    if (::connect(fd, reinterpret_cast<const sockaddr*>(&address),
                  sizeof(address)) == 0) {
        (void)::write(fd, "activate\n", 9);
        ::close(fd);
        return Status::SecondaryActivated;
    }
    ::close(fd);
    // 绑定：socket 文件残留（上次崩溃未清理）时先移除再 bind。
    ::unlink(path.c_str());
    const int listenFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listenFd < 0) {
        return Status::Unavailable;
    }
    if (::bind(listenFd, reinterpret_cast<const sockaddr*>(&address),
               sizeof(address)) != 0 || ::listen(listenFd, 4) != 0) {
        ::close(listenFd);
        return Status::SecondaryNotifyFailed;
    }
    // Primary：起守护 accept 线程。状态置活前先注册清理（静态注册表）。
    static std::vector<PrimaryState>& primaries = *new std::vector<
        PrimaryState>();
    auto* state = &primaries.emplace_back();
    state->listenFd = listenFd;
    state->socketPath = path;
    acceptRunning.store(true, std::memory_order_release);
    state->acceptThread = std::thread([state, onActivate = config.onActivateRequest] {
        acceptLoop(state->listenFd, onActivate);
    });
    state->acceptThread.detach();
    // 进程退出清理（正常退出移除 socket 文件；崩溃残留由下次 unlink 兜底）。
    std::atexit([] {
        acceptRunning.store(false, std::memory_order_release);
        for (const auto& primary : primaries) {
            ::shutdown(primary.listenFd, SHUT_RDWR);
            ::close(primary.listenFd);
            ::unlink(primary.socketPath.c_str());
        }
    });
    return Status::Primary;
}

}  // namespace lumen::core
