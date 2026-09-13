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
// 过）。应用侧把错误渲染为 errorText（StyleOverrides 前景色覆盖）行，
// 并给字段声明 invalid；错误状态由应用决定何时清除（提交时重算）。
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
    // M6：组合器（校验按序执行，首个非空错误返回）；邮箱/范围/格式等
    // 业务规则由应用经 compose 组合提供，不写死在框架。
    [[nodiscard]] static Validator compose(std::vector<Validator> chain);

    std::map<std::string, Validator> validators_{};
    std::map<std::string, std::string> errors_{};
};

}  // namespace lumen::widgets
