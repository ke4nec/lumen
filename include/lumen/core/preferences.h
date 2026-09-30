#pragma once

// G-7（gap-backlog）：轻量持久化助手。
//
// 百行级 preferences：键值 + 类型转换 + 变更通知 + 原子写 + 版本字段 +
// 损坏文件降级（docs/lumen-preferences-design.md）。定位是「应用层边界
// 内的薄助手」：不进网络/同步/加密，不改变主路线图 §1.2 的排除承诺；
// 落盘格式为自写行格式（无外部依赖），原子性由 tmp+rename 保证。
//
// 与 G-2 崩溃兜底的关系：本助手不做脏标记/崩溃检测（那是
// diagnostics::RuntimeDiagnostics 的职责）；写一半崩溃只留 .tmp 残留，
// 主文件因原子 rename 恒完整。

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace lumen::core {

class Preferences {
  public:
    using ObserverId = std::uint64_t;
    using Observer = std::function<void(const std::string& key)>;

    // 加载：文件缺失 = 空表 + ok（首次运行）；解析失败 = 损坏降级
    //（返回 false，内容为空——应用可提示或直接重建）。
    bool load(const std::string& path);
    // 保存：tmp + rename 原子替换；失败返回 false（内存态不变）。
    bool save(const std::string& path) const;

    // --- 类型读写（存储统一为字符串；类型不匹配 = 默认值降级） ---
    void setString(const std::string& key, const std::string& value);
    [[nodiscard]] std::string getString(
        const std::string& key, const std::string& fallback = "") const;
    void setInt(const std::string& key, long long value);
    [[nodiscard]] long long getInt(const std::string& key,
                                   long long fallback = 0) const;
    void setBool(const std::string& key, bool value);
    [[nodiscard]] bool getBool(const std::string& key,
                               bool fallback = false) const;
    void setDouble(const std::string& key, double value);
    [[nodiscard]] double getDouble(const std::string& key,
                                   double fallback = 0.0) const;

    [[nodiscard]] bool contains(const std::string& key) const;
    bool remove(const std::string& key);
    [[nodiscard]] std::size_t size() const { return values_.size(); }
    void clear();

    // --- 应用数据版本（迁移钩子；由应用读写，助手不解释） ---
    [[nodiscard]] int version() const { return version_; }
    void setVersion(int version) { version_ = version; }

    // --- 变更通知（值实际变化才通知；观测期间再次修改安全） ---
    ObserverId subscribe(Observer observer);
    void unsubscribe(ObserverId id);

    // 转义/反转义（导出为 protected static 供测试锁定；value 中 \n、\\
    // 编码为 \\n、\\\\）。
    [[nodiscard]] static std::string escape(const std::string& value);
    [[nodiscard]] static std::string unescape(const std::string& value);

  private:
    int version_{1};
    std::map<std::string, std::string> values_{};
    std::map<ObserverId, Observer> observers_{};
    ObserverId nextId_{1};
};

}  // namespace lumen::core
