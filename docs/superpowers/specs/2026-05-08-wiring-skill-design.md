# Wiring Skill — Design Spec

**Date:** 2026-05-08
**Status:** Approved

---

## Purpose

A general-purpose Claude Code skill that reads project files, generates step-by-step wiring tables for every component, recommends passive components and wire gauges, identifies gaps through targeted questions, and optionally hands off to a parts inventory skill.

---

## Workflow

The skill runs this sequence when invoked:

1. **Discover** — scan for known project files: `docs/hardware.md`, `docs/pinmap.md`, firmware `config.h`, `README.md`, and any file matching `**/bom*.md` or `**/hardware*.md`
2. **Extract** — pull out the component inventory, power rails, protocol buses, and GPIO assignments from whatever files are found
3. **Draft tables** — generate one table per component (see Table Format below); mark unknown connections as `TBD`
4. **Ask about wire gauges** — present the list of required gauges derived from the draft, then ask the user which gauges they have on hand; flag any mismatches in the Wire Gauge column
5. **Flag passives** — annotate each relevant connection in the Notes column with recommended passives; named multi-pin components (logic level shifters, shunts, UBECs) get their own tables
6. **Detect gaps** — identify:
   - Components in the BOM missing from the pin map
   - Incomplete protocol buses (e.g., I²C with SDA but no SCL)
   - Power rails that are unaccounted for
   - Voltage level mismatches (3.3V ↔ 5V crossings)
   - Wire gauge requirements not covered by what the user has on hand
7. **Ask targeted questions** — one per gap; only about things that cannot be resolved from the files
8. **Suggest missing components** — flag components likely needed but absent from the BOM (e.g., pull-up resistors for I²C, ferrite beads on motor leads, inline fuse on battery lead)
9. **Write output** — save `docs/wiring/wiring.md` and `docs/wiring/wiring.csv`
10. **Inventory handoff** — if the `inventory` skill is installed and missing components were identified, offer to invoke it to turn the gap list into a parts checklist

---

## Table Format

### Per-component heading

```
## <Component Name> (<Type> — <Protocol/Address if applicable>)
```

Example: `## PCA9685 (PWM Driver — I²C 0x40)`

### Columns

| # | Column | Description |
|---|--------|-------------|
| 1 | FROM Component | Name of this component |
| 2 | FROM Pin # | Physical pin number and/or label as printed on the board/datasheet (e.g., `3 / SCL`) |
| 3 | FROM Descriptor | Functional description of this pin (e.g., `I²C clock`) |
| 4 | TO Component | Name of the component at the other end |
| 5 | TO Pin # | Physical pin number and/or label at the other end |
| 6 | TO Descriptor | Functional description of the destination pin |
| 7 | Notes | Passive components, polarity warnings, orientation notes, voltage level |
| 8 | Wire Gauge | Recommended AWG for this connection; flagged if not in user's available gauges |

### Example table

```markdown
## PCA9685 (PWM Driver — I²C 0x40)

| FROM Component | FROM Pin # | FROM Descriptor     | TO Component   | TO Pin #   | TO Descriptor          | Notes                                              | Wire Gauge |
|----------------|------------|---------------------|----------------|------------|------------------------|----------------------------------------------------|------------|
| PCA9685        | 1 / VCC    | Logic power supply  | Buck converter | OUT+       | 5V regulated output    | Add 100nF bypass cap close to VCC pin              | 22 AWG     |
| PCA9685        | 2 / GND    | Ground              | Star ground    | —          | Common ground point    |                                                    | 22 AWG     |
| PCA9685        | 3 / SDA    | I²C data            | ESP32-S3       | GPIO47     | I²C data (shared bus)  | One 4.7kΩ pull-up to 3.3V per bus, not per device  | 26 AWG     |
| PCA9685        | 4 / SCL    | I²C clock           | ESP32-S3       | GPIO48     | I²C clock (shared bus) |                                                    | 26 AWG     |
| PCA9685        | 5 / OE     | Output enable       | GND            | —          | Ground                 | Tie low to enable outputs permanently              | 26 AWG     |
| PCA9685        | V+         | Servo motor power   | UBEC           | OUT+       | 6V regulated output    | Separate from logic VCC                            | 20 AWG     |
| PCA9685        | CH0        | PWM channel 0       | Rudder servo   | Signal     | PWM signal input       |                                                    | 26 AWG     |
| PCA9685        | CH1        | PWM channel 1       | Sail winch     | Signal     | PWM signal input       |                                                    | 26 AWG     |
| PCA9685        | CH2        | PWM channel 2       | Motor ESC      | Signal     | PWM signal input       |                                                    | 26 AWG     |
| PCA9685        | CH3–CH15   | PWM channels 3–15   | —              | —          | Unused                 | Leave unconnected                                  | —          |
```

### Wiring rules

- **Each physical wire appears twice** — once as a row in the FROM-component's table, once in the TO-component's table. This allows each component's table to be fully self-contained.
- **Unused pins** are listed explicitly with `—` in TO columns and `Leave unconnected` or `Tie to GND/Vcc as appropriate` in Notes.
- **Named components** (logic level shifters, UBECs, buck converters, shunts — anything with a specific functional role) get their own table even if they have only 2 terminals.
- **Anonymous passives** (ferrite beads, bypass caps, pull-up resistors, inline fuses) appear in the Notes column of the relevant row, with polarity information included where applicable (e.g., diode anode/cathode, electrolytic cap +/−).

---

## Passive Component Rules

Stored in `references/passives.md`. Keyed by connection type:

| Connection type | Rule |
|---|---|
| I²C bus | 4.7kΩ pull-ups to Vcc — one pair per bus, not per device |
| Logic power pin | 100nF bypass cap close to the Vcc pin |
| Motor leads | Ferrite bead on each wire, close to the ESC |
| Battery main lead | Inline fuse, sized to motor stall current (not running current) |
| 3.3V ↔ 5V signal crossing | Logic level shifter — named component, gets its own table |
| SPI bus | No special passives required at low speed; bypass caps on Vcc still apply |
| Reverse polarity protection | Schottky diode — named component if used, otherwise inline note |
| High-current connectors | Note correct polarity; flag if electrolytic cap is used nearby |

---

## Wire Gauge Rules

Stored in `references/wire-gauge.md`. The skill asks the user which gauges they have on hand, then flags any connection whose recommended gauge is unavailable.

| Use case | Recommended gauge |
|---|---|
| Battery main lead / motor leads | 14–16 AWG |
| UBEC output (servo power rail) | 18–20 AWG |
| Buck converter output (logic rail) | 20–22 AWG |
| Servo signal wires | 24–26 AWG |
| I²C / UART / SPI / PWM signals | 26–28 AWG |

---

## Output Style

The wiring.md output is more than bare tables. Each section should read as a useful reference document — written for an electronics hobbyist wiring the project for the first time.

### Power architecture section (first, before any component tables)

Write a narrative section covering:
- Overall power flow (battery → conversion → consumers), with an ASCII block diagram
- A power rail summary table (rail name, voltage, source, consumers)
- A load/power budget table (each load's typical and peak current draw, totals per rail)
- Wire gauge table (path, recommended gauge, reason)
- Motor noise suppression (capacitor placement at motor terminals, ferrite ring locations with priority, suitable ferrite types)

### Per-component sections

Each component section contains, in order:

1. **`##` heading** — `## <Component Name> (<Type>[— <Protocol/Address>])`
2. **Introduction paragraph** (1–3 sentences) — the component's role in the system, and any key wiring constraints or design decisions (e.g., "The PCA9685 has two separate power inputs: VCC for logic (5V) and V+ for servo power (6V) — these must not be swapped.")
3. **Wiring table** — 8-column format as defined above
4. **Rationale blockquotes** — one or more `>` blockquote notes after the table for important design decisions, dangerous mistakes to avoid, or cross-component dependencies (e.g., why the SBEC red wire must not be connected; why the buck converter feeds from the UBEC rather than the battery)

### Warning callouts

Prefix safety-critical notes with `⚠` anywhere they appear — in table Notes cells, in rationale blockquotes, or in the introduction paragraph. Examples:
- Conflicting power sources (two BECs on the same rail)
- Voltage mismatches (5.5V max on a pin fed with 6V)
- Wrong power source for a component (3.3V too low for ELRS receivers)
- Pins that must never be connected to a given voltage

---

## Output Files

| File | Format | Contents |
|---|---|---|
| `docs/wiring/wiring.md` | Markdown | Power architecture section + one section per component (intro + table + rationale notes) |
| `docs/wiring/wiring.csv` | CSV | All connections, each wire appearing twice (one row per end), suitable for Excel |

CSV column order matches the 8-column table format above. Each physical wire appears as two rows: the first row has the natural FROM→TO direction; the second row swaps FROM and TO so the same wire appears in both components' views.

---

## Inventory Skill Integration

At the end of the workflow, if:
- Missing or recommended components were identified (e.g., pull-up resistors, ferrite beads, a fuse), **and**
- The `inventory` skill is present in `.agents/skills/`

Then the skill offers:
> "I identified N components that aren't in your BOM. Would you like me to invoke the inventory skill to turn these into a parts checklist?"

If the inventory skill is not installed, the skill lists the missing components as plain text and notes that an inventory skill can be added later.

---

## File Structure

```
.agents/skills/wiring/
  SKILL.md
  references/
    passives.md       — passive placement rules by connection type
    wire-gauge.md     — AWG recommendations by use case
```

```
docs/wiring/          — generated output (gitignored or committed per user preference)
  wiring.md
  wiring.csv
```
