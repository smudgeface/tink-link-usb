#ifndef SWITCHER_FACTORY_H
#define SWITCHER_FACTORY_H

#include <Arduino.h>
#include "Switcher.h"

/**
 * Factory for creating video switcher instances by type string.
 *
 * Supported types:
 * - "Extron SW VGA" -> ExtronSwVgaSwitcher
 *
 * Usage:
 *   Switcher* sw = SwitcherFactory::create("Extron SW VGA");
 *   if (sw) {
 *       sw->configure(config);
 *       sw->begin();
 *   }
 */
class SwitcherFactory {
public:
    /**
     * Create a switcher instance by type name.
     * @param type Type name string (e.g., "Extron SW VGA")
     * @return Pointer to new switcher instance, or nullptr if type unknown
     */
    static Switcher* create(const String& type);
};

#endif // SWITCHER_FACTORY_H
