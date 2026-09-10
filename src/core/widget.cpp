#include "lumen/core/widget.h"

namespace lumen::core {

bool isLeafWidget(WidgetType type) {
    switch (type) {
        case WidgetType::Text:
        case WidgetType::Button:
        case WidgetType::TextField:
        case WidgetType::Checkbox:
        case WidgetType::Switch:
            return true;
        case WidgetType::Container:
        case WidgetType::Row:
        case WidgetType::Column:
        case WidgetType::Stack:
        case WidgetType::ScrollView:
        case WidgetType::ListView:
        case WidgetType::FocusScope:
            return false;
    }
    return false;
}

bool isFlexContainer(WidgetType type) {
    return type == WidgetType::Row || type == WidgetType::Column;
}

bool isScrollableWidget(WidgetType type) {
    return type == WidgetType::ScrollView || type == WidgetType::ListView;
}

}  // namespace lumen::core
