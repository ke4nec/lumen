// G-7：轻量持久化助手实现（契约见 preferences.h 与
// docs/lumen-preferences-design.md）。行格式：
//   # lumen-prefs v1
//   version=<int>
//   <escaped-key>=<escaped-value>
// 原子写 = 写 <path>.tmp 后 rename（POSIX 原子替换；写一半崩溃主文件
// 恒完整）。解析严格：格式头缺失/行非法 → 损坏降级（空表）。

#include "lumen/core/preferences.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <utility>

namespace lumen::core {

namespace {
constexpr const char* kMagic = "# lumen-prefs v1";
}  // namespace

std::string Preferences::escape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '=':
                out += "\\=";
                break;
            default:
                out += c;
                break;
        }
    }
    return out;
}

std::string Preferences::unescape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
            switch (const char next = value[++i]) {
                case 'n':
                    out += '\n';
                    break;
                case '\\':
                    out += '\\';
                    break;
                case '=':
                    out += '=';
                    break;
                default:
                    out += '\\';
                    out += next;
                    break;
            }
        } else {
            out += value[i];
        }
    }
    return out;
}

bool Preferences::load(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        values_.clear();  // 首次运行：空表 + ok
        return true;
    }
    std::string content;
    char buffer[4096];
    std::size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        content.append(buffer, got);
    }
    std::fclose(file);

    // 严格解析：头缺失即损坏（写一半的 .tmp 不会成为主文件，但外力
    // 截断可能）。
    if (content.rfind(kMagic, 0) != 0) {
        values_.clear();
        return false;
    }
    std::map<std::string, std::string> parsed;
    std::size_t pos = content.find('\n');
    if (pos == std::string::npos) {
        pos = content.size();
    }
    while (pos + 1 < content.size()) {
        const std::size_t end = content.find('\n', pos + 1);
        const std::string line = content.substr(
            pos + 1, end == std::string::npos ? std::string::npos
                                              : end - pos - 1);
        pos = end == std::string::npos ? content.size() : end;
        if (line.empty() || line.rfind("# ", 0) == 0) {
            continue;
        }
        // 第一个未转义的 '=' 分隔。
        std::size_t split = std::string::npos;
        for (std::size_t i = 0; i < line.size(); ++i) {
            if (line[i] == '\\' && i + 1 < line.size()) {
                ++i;
                continue;
            }
            if (line[i] == '=') {
                split = i;
                break;
            }
        }
        if (split == std::string::npos) {
            values_.clear();
            return false;  // 非法行 = 损坏降级
        }
        parsed[unescape(line.substr(0, split))] =
            unescape(line.substr(split + 1));
    }
    const auto version = parsed.find("version");
    if (version != parsed.end()) {
        version_ = std::atoi(version->second.c_str());
        parsed.erase(version);
    }
    values_ = std::move(parsed);
    return true;
}

bool Preferences::save(const std::string& path) const {
    namespace fs = std::filesystem;
    const fs::path tmp = fs::path(path).string() + ".tmp";
    std::FILE* file = std::fopen(tmp.string().c_str(), "wb");
    if (file == nullptr) {
        return false;
    }
    std::string out = std::string(kMagic) + "\nversion=" +
                      std::to_string(version_) + "\n";
    for (const auto& [key, value] : values_) {
        out += escape(key) + "=" + escape(value) + "\n";
    }
    const bool written =
        std::fwrite(out.data(), 1, out.size(), file) == out.size();
    if (std::fclose(file) != 0 || !written) {
        std::remove(tmp.string().c_str());
        return false;
    }
    std::error_code ec;
    fs::rename(tmp, fs::path(path), ec);
    if (ec) {
        std::remove(tmp.string().c_str());
        return false;
    }
    return true;
}

void Preferences::setString(const std::string& key, const std::string& value) {
    const auto it = values_.find(key);
    if (it != values_.end() && it->second == value) {
        return;  // 幂等：不通知
    }
    values_[key] = value;
    for (const auto& [id, observer] : observers_) {
        (void)id;
        observer(key);
    }
}

std::string Preferences::getString(const std::string& key,
                                   const std::string& fallback) const {
    const auto it = values_.find(key);
    return it != values_.end() ? it->second : fallback;
}

void Preferences::setInt(const std::string& key, long long value) {
    setString(key, std::to_string(value));
}

long long Preferences::getInt(const std::string& key,
                              long long fallback) const {
    const auto it = values_.find(key);
    if (it == values_.end()) {
        return fallback;
    }
    char* end = nullptr;
    const long long parsed = std::strtoll(it->second.c_str(), &end, 10);
    return end != it->second.c_str() && *end == '\0' ? parsed : fallback;
}

void Preferences::setBool(const std::string& key, bool value) {
    setString(key, value ? "true" : "false");
}

bool Preferences::getBool(const std::string& key, bool fallback) const {
    const std::string value = getString(key);
    if (value == "true" || value == "1") {
        return true;
    }
    if (value == "false" || value == "0") {
        return false;
    }
    return fallback;
}

void Preferences::setDouble(const std::string& key, double value) {
    setString(key, std::to_string(value));
}

double Preferences::getDouble(const std::string& key, double fallback) const {
    const auto it = values_.find(key);
    if (it == values_.end()) {
        return fallback;
    }
    char* end = nullptr;
    const double parsed = std::strtod(it->second.c_str(), &end);
    return end != it->second.c_str() && *end == '\0' ? parsed : fallback;
}

bool Preferences::contains(const std::string& key) const {
    return values_.count(key) != 0;
}

bool Preferences::remove(const std::string& key) {
    const auto it = values_.find(key);
    if (it == values_.end()) {
        return false;
    }
    values_.erase(it);
    for (const auto& [id, observer] : observers_) {
        (void)id;
        observer(key);
    }
    return true;
}

void Preferences::clear() {
    values_.clear();
    for (const auto& [id, observer] : observers_) {
        (void)id;
        observer({});
    }
}

Preferences::ObserverId Preferences::subscribe(Observer observer) {
    const ObserverId id = nextId_++;
    observers_[id] = std::move(observer);
    return id;
}

void Preferences::unsubscribe(ObserverId id) { observers_.erase(id); }

}  // namespace lumen::core
