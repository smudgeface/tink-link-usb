#ifndef RETROTINK_H
#define RETROTINK_H

#include <Arduino.h>
#include <vector>

// RetroTINK 4K controller
// Phase 1: Stub implementation (logs commands without sending)
// Phase 2: Will use USB Host FTDI communication

struct TriggerMapping {
    int extronInput;
    enum Mode { SVS, REMOTE } mode;
    int profile;
    String name;
};

class RetroTink {
public:
    RetroTink();
    ~RetroTink();

    void begin();

    // Add trigger mapping
    void addTrigger(const TriggerMapping& trigger);

    // Handle Extron input change
    void onExtronInputChange(int input);

    // Send raw command (for debug interface)
    void sendRawCommand(const String& command);

    // Send continuous test signals (for scope testing)
    void sendContinuousTest(int count = 10);

    // Get last command sent
    String getLastCommand() const { return _lastCommand; }

private:
    std::vector<TriggerMapping> _triggers;
    String _lastCommand;

    // Find trigger for given input
    const TriggerMapping* findTrigger(int input) const;

    // Generate command string for trigger
    String generateCommand(const TriggerMapping& trigger) const;

    // Send command to RetroTINK (Phase 1: just log, Phase 2: send via USB)
    void sendCommand(const String& command);
};

#endif // RETROTINK_H
