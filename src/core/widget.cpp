#include "lumen/core/widget.h"

namespace lumen::core {

bool isLeafWidget(WidgetType type) {
    switch (type) {
        case WidgetType::Text:
        case WidgetType::Button:
        case WidgetType::TextField:
            return true;
        case WidgetType::Container:
        case WidgetType::Row:
        case WidgetType::Column:
        case WidgetType::Stack:
            return false;
    }
    return false;
}

bool isFlexContainer(WidgetType type) {
    return type == WidgetType::Row || type == WidgetType::Column;
}

}  // namespace lumen::core
