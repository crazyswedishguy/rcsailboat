// failsafe.cpp — BOOT / DISARMED / ARMED / FAILSAFE state machine.
//
// Transitions (see docs/failsafe.md for rationale):
//   BOOT      → DISARMED   link acquired (first valid CRSF frame)
//   DISARMED  → ARMED      arm channel ≥ 0.95 AND throttle within ±0.05 of neutral
//   ARMED     → DISARMED   arm channel < 0.05 (operator explicitly disarmed)
//   ARMED     → FAILSAFE   link lost > 500 ms
//   FAILSAFE  → DISARMED   link restored AND arm channel was lowered since failsafe began
//
// failsafe_active() returns true in BOOT, DISARMED, and FAILSAFE states.
// main.cpp applies safe servo positions whenever failsafe_active() is true.

#include "failsafe.h"
#include "elrs.h"
#include "protocol.h"
#include <Arduino.h>
#include <cmath>

enum class FSState : uint8_t { BOOT, DISARMED, ARMED, FAILSAFE };

static FSState s_state       = FSState::BOOT;
static bool    s_arm_cycled  = false;  // arm went low at least once since FAILSAFE entered
static bool    s_arm_warned  = false;  // suppress repeated "throttle not neutral" logs

static const char *state_name(FSState s)
{
    switch (s) {
        case FSState::BOOT:     return "BOOT";
        case FSState::DISARMED: return "DISARMED";
        case FSState::ARMED:    return "ARMED";
        case FSState::FAILSAFE: return "FAILSAFE";
    }
    return "?";
}

void failsafe_init()
{
    s_state      = FSState::BOOT;
    s_arm_cycled = false;
    s_arm_warned = false;
}

void failsafe_update()
{
    bool  link  = elrs_link_ok();
    float arm   = elrs_get_channel(CH_ARM);
    float throt = elrs_get_channel(CH_THROTTLE);

    FSState prev = s_state;

    switch (s_state) {

        case FSState::BOOT:
            if (link) {
                s_state = FSState::DISARMED;
            }
            break;

        case FSState::DISARMED:
            if (!link) break;  // stay DISARMED; safe positions already applied by main.cpp

            if (arm > 0.95f) {
                if (fabsf(throt) <= 0.05f) {
                    s_state      = FSState::ARMED;
                    s_arm_warned = false;
                } else if (!s_arm_warned) {
                    Serial.println("failsafe: arm rejected — return throttle to neutral first");
                    s_arm_warned = true;
                }
            } else {
                s_arm_warned = false;  // reset so the warning fires again next arm attempt
            }
            break;

        case FSState::ARMED:
            if (!link) {
                s_state      = FSState::FAILSAFE;
                s_arm_cycled = false;
            } else if (arm < 0.05f) {
                s_state = FSState::DISARMED;
            }
            break;

        case FSState::FAILSAFE:
            // Track arm going low — operator must acknowledge before re-arm is possible.
            if (arm < 0.05f) s_arm_cycled = true;

            // Only exit FAILSAFE once both: link is healthy again AND arm was lowered.
            // This prevents an automatic return-to-control if the link simply comes back.
            if (link && s_arm_cycled) {
                s_state      = FSState::DISARMED;
                s_arm_cycled = false;
            }
            break;
    }

    if (s_state != prev) {
        Serial.printf("failsafe: %s → %s\n", state_name(prev), state_name(s_state));
    }
}

bool failsafe_active() { return s_state != FSState::ARMED; }
bool failsafe_armed()  { return s_state == FSState::ARMED; }
