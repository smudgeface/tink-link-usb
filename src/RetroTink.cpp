#include "RetroTink.h"
#include "Logger.h"

RetroTink::RetroTink()
    : _lastCommand("")
{
}

RetroTink::~RetroTink() {
}

void RetroTink::begin() {
    LOG_INFO("RetroTink controller initialized (stub mode - USB Host not yet implemented)");
}

void RetroTink::addTrigger(const TriggerMapping& trigger) {
    _triggers.push_back(trigger);

    const char* modeStr = (trigger.mode == TriggerMapping::SVS) ? "SVS" : "Remote";
    LOG_DEBUG("RetroTink: Added trigger - input %d -> profile %d (%s)",
              trigger.extronInput, trigger.profile, modeStr);
}

void RetroTink::clearTriggers() {
    _triggers.clear();
    LOG_DEBUG("RetroTink: All triggers cleared");
}

void RetroTink::onExtronInputChange(int input) {
    const TriggerMapping* trigger = findTrigger(input);

    if (trigger) {
        String command = generateCommand(*trigger);
        sendCommand(command);
        LOG_INFO("RetroTink: Input %d triggered -> %s", input, command.c_str());
    } else {
        LOG_DEBUG("RetroTink: No trigger defined for input %d", input);
    }
}

void RetroTink::sendRawCommand(const String& command) {
    sendCommand(command);
    LOG_DEBUG("RetroTink: Raw command sent: %s", command.c_str());
}

void RetroTink::sendContinuousTest(int count) {
    LOG_DEBUG("RetroTink: Sending %d test signals (stub mode)", count);
    for (int i = 0; i < count; i++) {
        sendCommand("TEST");
        delay(100);
    }
    _lastCommand = "Continuous test pattern";
}

const TriggerMapping* RetroTink::findTrigger(int input) const {
    for (const auto& trigger : _triggers) {
        if (trigger.extronInput == input) {
            return &trigger;
        }
    }
    return nullptr;
}

String RetroTink::generateCommand(const TriggerMapping& trigger) const {
    String cmd;

    if (trigger.mode == TriggerMapping::SVS) {
        // SVS format: "SVS NEW INPUT=N"
        cmd = "SVS NEW INPUT=" + String(trigger.profile);
    } else {
        // Remote format: "remote profN"
        cmd = "remote prof" + String(trigger.profile);
    }

    return cmd;
}

void RetroTink::sendCommand(const String& command) {
    // Phase 1: Just log what would be sent
    // Phase 3: Will send via USB Host FTDI
    LOG_DEBUG("RetroTink TX (stub): [%s]", command.c_str());
    _lastCommand = command;

    // TODO: In Phase 3, replace with actual USB Host serial write:
    // usbSerial->println(command);
}
