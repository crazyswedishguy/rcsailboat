# Wire Gauge Recommendations

## Gauge Table

| Use Case | Recommended Gauge | Typical Max Current |
|---|---|---|
| Battery main lead / motor leads | 14–16 AWG | 10–15 A |
| UBEC output (servo power rail) | 18–20 AWG | 5–8 A |
| Buck converter output (logic rail) | 20–22 AWG | 2–3 A |
| Servo power wires (rail to servo connector) | 20–22 AWG | 2–3 A |
| Servo signal wires | 24–26 AWG | Signal only |
| I²C / UART / SPI / PWM signals | 26–28 AWG | Signal only |
| Shunt sense leads (e.g. INA219 to shunt) | Match main lead gauge | — |

## How to Apply

1. After drafting all wiring tables, assign a gauge to every connection row using the table above.
2. Collect the full set of unique gauges required across all tables.
3. Ask the user: `"This project requires the following wire gauges: [list]. Which do you have on hand?"`
4. For any required gauge the user does not have, mark the Wire Gauge cell as `⚠️ [X AWG]` and add to Notes: `"Required gauge not available — nearest on hand is [Y AWG]; verify current rating before substituting."`

## Substitution Guidelines

- **Heavier gauge (lower AWG number)**: always safe for current handling; harder to route in tight spaces
- **Lighter gauge (higher AWG number)**: acceptable only if measured or expected current is well within the lighter gauge's continuous rating; **never substitute on battery or motor leads**
- When in doubt: go heavier
