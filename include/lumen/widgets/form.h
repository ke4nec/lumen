#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "lumen/core/state.h"
#include "lumen/core/widget.h"

namespace lumen::widgets {

// v0.3 阶段8D (plan §3.4): 表单校验。
//
// 字段按 bind key 注册校验器；validate(store) 评估全部字段并把错误写
// 入应用可见的 errors() 映射（key = bind key，value = 错误文案，空 = 通
// 过）。应用侧把错误渲染为 themedError 行；错误状态由应用决定何时清除
//（提交时重算）。
class FormController {
  public:
    using Validator = std::function<std::string(const std::string&)>;

    // 注册字段校验器（覆盖同 key 旧值；空函数 = 只要求非空？否——完全由
    // 校验器决定）。
    void registerField(std::string bindKey, Validator validator);

    // 评估全部字段；返回是否全部通过（errors() 同步刷新）。
    bool validate(const core::StateStore& store);

    [[nodiscard]] const std::map<std::string, std::string>& errors() const {
        return errors_;
    }
    [[nodiscard]] std::size_t fieldCount() const { return validators_.size(); }
    void clearErrors() { errors_.clear(); }

    // 常用校验器：非空、最小长度、简单邮箱形状。
    [[nodiscard]] static Validator nonEmpty(std::string message);
    [[nodiscard]] static Validator minLength(std::size_t length,
                                             std::string message);

  private:
    std::map<std::string, Validator> validators_{};
    std::map<std::string, std::string> errors_{};
};

}  // namespace lumen::widgets
