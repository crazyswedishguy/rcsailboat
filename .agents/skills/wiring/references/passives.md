# Passive Component Placement Rules

Apply these rules during Step 2 of the wiring workflow. Add each recommendation to the **Notes** column of the affected row. Always include polarity where applicable.

## I²C Bus

- **Pull-up resistors**: 4.7kΩ from SDA to Vcc AND 4.7kΩ from SCL to Vcc
- Add **one pair per bus** — not one per device
- Notes text for first SDA row: `Add 4.7kΩ pull-up to [Vcc voltage]; one pair for the whole bus`
- If breakout boards have built-in pull-ups, note: `Verify breakouts don't stack pull-ups — remove extras if bus is sluggish`

## Logic Power Pins (VCC / 3V3 / 5V)

- **Bypass capacitor**: 100nF ceramic, placed as close as possible to the power pin
- Notes text: `Add 100nF ceramic bypass cap close to this pin`

## Motor Leads

- **Ferrite bead**: one bead on each motor wire (both + and −), placed close to the ESC end
- Notes text on motor+ row: `Add ferrite bead in series, close to ESC; no polarity`
- Notes text on motor− row: `Add ferrite bead in series, close to ESC; no polarity`
- **Motor capacitors** (cover in the motor noise section of wiring.md, not as a row note): solder three 100nF ceramic caps (X7R, ≥25V) directly at the motor terminals — one across the two terminals, one from each terminal to the motor case. This is more effective than ferrites alone for suppressing commutator noise.

## Battery Main Lead

- **Inline fuse**: on the positive lead only; size to motor stall current (not running current)
- Notes text on battery+ row: `Add inline fuse on + lead; size to motor stall current`
- Polarity: fuse on + lead, never on −

## 3.3V ↔ 5V Signal Crossings

- **Logic level shifter required** — this is a named component; create its own table
- Notes text on the crossing row: `Voltage mismatch — route through logic level shifter (see its table)`
- Do not use a resistor divider as a substitute for bidirectional signals

## Reverse Polarity Protection

- **Schottky diode** (if chosen) — named component; give it its own table
- If no protection used, add to battery+ Notes: `No reverse polarity protection — verify polarity before every connection`
- Polarity when used: `Anode → source; cathode → load`

## Electrolytic Capacitors (bulk bypass)

- **Polarity critical**
- Notes text: `Electrolytic cap — + lead to [higher rail]; verify polarity before powering`

## SPI Bus

- No special passives needed at speeds below 10 MHz
- Bypass caps on Vcc still apply (see Logic Power Pins above)
- At speeds above 20 MHz: Notes text on SCK/MOSI rows: `Consider 22–47Ω series resistor to reduce ringing`

## UART

- No special passives needed for runs under 30 cm
- For longer runs or noisy environments: Notes text on TX row: `Consider 100–330Ω series resistor on TX for noise immunity`

## High-Current Connectors

- Note connector current rating if it may be marginal
- Add bulk capacitor (100–470µF electrolytic) near high-current loads when supply wiring is long
- Notes text: `Add 100–470µF bulk cap near load if supply wiring exceeds ~30 cm`
