#include "lumen/widgets/form.h"

#include <algorithm>

#include <utility>

namespace lumen::widgets {

core::Widget makeFormField(std::string label, core::Widget field,
                           std::string support, const style::Theme& theme,
                           std::string key) {
    auto labelStyle = theme.typography.label;
    labelStyle.color = theme.colors.contentSecondary;
    auto supportStyle = theme.typography.caption;
    supportStyle.color = field.invalid ? theme.colors.errorContent : theme.colors.contentSecondary;
    supportStyle.lineHeight = std::max(supportStyle.lineHeight,
                                       18.0F / std::max(1.0F, supportStyle.fontSize));
    auto labelNode = core::withKey(core::makeText(std::move(label), labelStyle), key + "-label");
    auto supportNode = core::withKey(core::makeText(support.empty() ? " " : std::move(support),
                                                   supportStyle), key + "-error");
    return core::withKey(core::makeColumn({std::move(labelNode), std::move(field), std::move(supportNode)},
        core::MainAxisAlignment::Start, core::CrossAxisAlignment::Stretch, 4.0F), key + "-group");
}

void FormController::registerField(std::string bindKey,
                                   Validator validator) {
    validators_[std::move(bindKey)] = std::move(validator);
}

bool FormController::validate(const core::StateStore& store) {
    errors_.clear();
    for (const auto& [key, validator] : validators_) {
        if (!validator) {
            continue;
        }
        const std::string error = validator(store.get(key));
        if (!error.empty()) {
            errors_[key] = error;
        }
    }
    return errors_.empty();
}

FormController::Validator FormController::nonEmpty(std::string message) {
    return [message = std::move(message)](const std::string& value) {
        return value.empty() ? message : std::string{};
    };
}

FormController::Validator FormController::minLength(std::size_t length,
                                                    std::string message) {
    return [length, message = std::move(message)](const std::string& value) {
        return value.size() < length ? message : std::string{};
    };
}

FormController::Validator FormController::compose(
    std::vector<Validator> chain) {
    return [chain = std::move(chain)](
               const std::string& value) -> std::string {
        for (const auto& validator : chain) {
            if (validator == nullptr) {
                continue;
            }
            const std::string error = validator(value);
            if (!error.empty()) {
                return error;
            }
        }
        return {};
    };
}

}  // namespace lumen::widgets
