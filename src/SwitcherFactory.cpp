#include "SwitcherFactory.h"
#include "ExtronSwVgaSwitcher.h"
#include "Logger.h"

Switcher* SwitcherFactory::create(const String& type) {
    if (type == "Extron SW VGA") {
        return new ExtronSwVgaSwitcher();
    }

    LOG_ERROR("SwitcherFactory: Unknown type: %s", type.c_str());
    return nullptr;
}
