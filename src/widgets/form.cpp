#include "lumen/widgets/form.h"

#include <utility>

namespace lumen::widgets {

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

}  // namespace lumen::widgets
