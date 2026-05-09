# Wiring Skill Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a Claude Code skill that reads project files and generates per-component wiring tables with passive recommendations, wire gauge guidance, gap detection, and Markdown + CSV output.

**Architecture:** Three Markdown files — a main `SKILL.md` that defines the 7-step workflow, and two reference files (`passives.md`, `wire-gauge.md`) that the skill loads as lookup tables during execution. No code is compiled or run; the skill guides Claude's behaviour at invocation time.

**Tech Stack:** Markdown skill files, Claude Code skill system (`npx skills`), git.

---

### Task 1: Create skill directory and write `references/passives.md`

**Files:**
- Create: `.agents/skills/wiring/references/passives.md`

- [ ] **Step 1: Create the directory structure**

```powershell
New-Item -ItemType Directory -Force ".agents\skills\wiring\references"
```

Expected: directory created with no errors.

- [ ] **Step 2: Write `references/passives.md`**

Create `.agents/skills/wiring/references/passives.md` with this exact content:

```markdown
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
```

- [ ] **Step 3: Verify the file exists and is non-empty**

```powershell
Get-Item ".agents\skills\wiring\references\passives.md" | Select-Object Name, Length
```

Expected: file shown with `Length > 0`.

- [ ] **Step 4: Commit**

```powershell
git add ".agents\skills\wiring\references\passives.md"
git commit -m "feat(wiring-skill): add passives reference"
```

---

### Task 2: Write `references/wire-gauge.md`

**Files:**
- Create: `.agents/skills/wiring/references/wire-gauge.md`

- [ ] **Step 1: Write `references/wire-gauge.md`**

Create `.agents/skills/wiring/references/wire-gauge.md` with this exact content:

```markdown
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
```

- [ ] **Step 2: Verify the file exists and is non-empty**

```powershell
Get-Item ".agents\skills\wiring\references\wire-gauge.md" | Select-Object Name, Length
```

Expected: file shown with `Length > 0`.

- [ ] **Step 3: Commit**

```powershell
git add ".agents\skills\wiring\references\wire-gauge.md"
git commit -m "feat(wiring-skill): add wire-gauge reference"
```

---

### Task 3: Write `SKILL.md`

**Files:**
- Create: `.agents/skills/wiring/SKILL.md`

- [ ] **Step 1: Write `SKILL.md`**

Create `.agents/skills/wiring/SKILL.md` with this exact content:

````markdown
---
name: wiring
description: Generate complete per-component wiring tables for electronics projects. Reads project BOM and pin-map files, produces 8-column FROM/TO connection tables with passive component recommendations, wire gauge guidance, gap detection, and CSV + Markdown output. Invoke when wiring up components, planning connections, or checking a wiring design.
license: MIT
metadata:
  author: local
  version: "1.0.0"
  domain: specialized
  triggers: wiring, wire up, connect components, pin connections, wiring table, wiring instructions, how do I wire, wiring guide
  role: specialist
  scope: planning + documentation
  output-format: markdown + csv
  related-skills: inventory
---

# Wiring Assistant

Electronics wiring specialist. Reads project files to generate complete per-component connection tables, flags passive component requirements, identifies wiring gaps, and writes Markdown + CSV output ready for bench use.

## Workflow

### Step 1 — Discover project files

Scan for and read these files if present:
- `docs/hardware.md` — BOM and power rails
- `docs/pinmap.md` — GPIO assignments
- `boat-firmware/src/config.h`, `src/config.h`, `firmware/src/config.h` — GPIO defines
- `README.md` — project overview
- Any file matching `**/bom*.md`, `**/hardware*.md`, `**/wiring*.md`

Extract from every file found:
- **Component inventory**: names, types, communication protocols, I²C addresses
- **Power rails**: voltage sources (battery, UBEC, buck converter) and their consumers
- **Protocol buses**: I²C, SPI, UART — with every assigned pin
- **GPIO assignments**: MCU GPIO number → peripheral

### Step 2 — Draft wiring tables

Generate one table per component. Format:

```
## <Component Name> (<Type>[— <Protocol/Address>])

| FROM Component | FROM Pin # | FROM Descriptor | TO Component | TO Pin # | TO Descriptor | Notes | Wire Gauge |
|---|---|---|---|---|---|---|---|
```

Rules:
- **FROM Pin #**: include both physical number and label where available, e.g. `3 / SCL`
- **Unused pins**: list explicitly; Notes = `Leave unconnected` or specific tie instruction (e.g. `Tie to GND`)
- **Unknown connections**: mark TO columns as `TBD`
- **Named components** (UBECs, buck converters, logic level shifters, shunts — anything with a specific functional identity) get their own table, even if 2-terminal
- **Anonymous passives** (ferrite beads, bypass caps, pull-up resistors, fuses) go in the Notes column of the affected row — never as their own table

Load and apply `references/passives.md` rules to populate the Notes column. Leave Wire Gauge blank at this stage.

**Duplication rule**: each physical wire appears in two tables — once in the FROM-component's table (natural direction) and once in the TO-component's table (FROM/TO swapped). This makes each component's table fully self-contained.

### Step 3 — Detect gaps

Flag each of the following:
- BOM components with no pin assignments → mark their connection rows TBD
- Protocol buses missing pins (I²C without both SDA and SCL; SPI without MOSI, MISO, SCK, and CS)
- Power rails with no named source, or consumers referencing a rail with no source
- 3.3V ↔ 5V signal crossings with no logic level shifter → add a level shifter table placeholder
- Ground connections not tied to a named common star-ground point

For each gap, prepare one targeted question. Ask them **one at a time** after presenting the draft.

### Step 4 — Suggest missing components

After gap detection, check whether any of the following are absent from the BOM but implied by what is present:

| Present in BOM | Likely missing |
|---|---|
| I²C devices without confirmed on-board pull-ups | 4.7kΩ pull-up resistors (one pair per bus) |
| Motor or ESC | Ferrite beads on motor leads (one per wire) |
| Battery main lead | Inline fuse |
| Both 3.3V and 5V logic devices on shared signals | Logic level shifter |
| High-current loads with supply wiring longer than ~30 cm | Bulk bypass capacitor (100–470µF) |

Present the list and ask: *"These components appear to be missing from your BOM — do you want to add them to the wiring tables?"*

### Step 5 — Wire gauge

Load `references/wire-gauge.md`. Assign a recommended gauge to every connection row.

Collect all unique gauges required. Then ask:
> "This project requires the following wire gauges: [X AWG, Y AWG, …]. Which do you have on hand?"

For any required gauge not available, mark the Wire Gauge column `⚠️ [X AWG]` and add to Notes: `Required gauge not available — nearest on hand is [Y AWG]; verify current rating before substituting.`

### Step 6 — Write output

Create `docs/wiring/` if it does not exist.

**`docs/wiring/wiring.md`** — one `##` section per component, tables as above. Display inline in chat for immediate review.

**`docs/wiring/wiring.csv`** — all connections as a flat CSV. Each wire appears as two rows: first row is natural FROM→TO direction; second row swaps FROM and TO. Header row:

```
FROM Component,FROM Pin #,FROM Descriptor,TO Component,TO Pin #,TO Descriptor,Notes,Wire Gauge
```

### Step 7 — Inventory handoff

If missing or recommended components were identified:

- Check whether `.agents/skills/inventory/SKILL.md` exists.
- **If yes**: offer — *"I identified N missing components. Would you like me to invoke the inventory skill to turn these into a parts checklist?"*
- **If no**: list the missing components as plain text and note: *"An inventory skill can be added with: `npx skills find inventory`"*

## Reference Files

| File | Load when |
|---|---|
| `references/passives.md` | Step 2 — always |
| `references/wire-gauge.md` | Step 5 — always |

## Output Contract

The generated `docs/wiring/wiring.md` and `docs/wiring/wiring.csv` are the deliverables. Do not consider the skill complete until both files exist and have been confirmed by the user.
````

- [ ] **Step 2: Verify SKILL.md frontmatter is valid YAML**

```powershell
# Check the frontmatter block (lines between the two --- delimiters) is present and well-formed
$content = Get-Content ".agents\skills\wiring\SKILL.md" -Raw
if ($content -match "(?s)^---(.+?)---") { "Frontmatter found" } else { "ERROR: no frontmatter" }
```

Expected: `Frontmatter found`

- [ ] **Step 3: Verify all 7 workflow steps are present**

```powershell
$content = Get-Content ".agents\skills\wiring\SKILL.md" -Raw
1..7 | ForEach-Object { if ($content -match "### Step $_ ") { "Step $_ OK" } else { "ERROR: Step $_ missing" } }
```

Expected: `Step 1 OK` through `Step 7 OK`

- [ ] **Step 4: Verify reference files are loadable**

```powershell
Test-Path ".agents\skills\wiring\references\passives.md"
Test-Path ".agents\skills\wiring\references\wire-gauge.md"
```

Expected: `True` twice.

- [ ] **Step 5: Commit**

```powershell
git add ".agents\skills\wiring\SKILL.md"
git commit -m "feat(wiring-skill): add main SKILL.md"
```

---

### Task 4: Validate the skill end-to-end

**Files:**
- Read: `docs/hardware.md`, `docs/pinmap.md`
- Create (generated): `docs/wiring/wiring.md`, `docs/wiring/wiring.csv`

- [ ] **Step 1: Invoke the skill on the current project**

Use the Skill tool:

```
Skill("wiring")
```

The skill should immediately begin Step 1 (scanning project files) without asking for input first.

- [ ] **Step 2: Verify Step 1 output — file discovery**

The skill should report finding at minimum:
- `docs/hardware.md` ✓
- `docs/pinmap.md` ✓  
- `boat-firmware/src/config.h` ✓

If any are missing from the report, the discovery step has a bug — re-read the SKILL.md Step 1 glob patterns.

- [ ] **Step 3: Verify Step 2 output — table format**

Each component table must have:
- A `##` heading with component name and type
- All 8 columns: `FROM Component | FROM Pin # | FROM Descriptor | TO Component | TO Pin # | TO Descriptor | Notes | Wire Gauge`
- At least one row with populated Notes (passives applied)
- Unused pins listed explicitly (not silently omitted)

Check the PCA9685 table specifically:
- CH3–CH15 should appear as `Unused` rows
- SDA/SCL rows should have pull-up resistor notes
- VCC row should have bypass cap note
- V+ row should be present (servo power rail, separate from VCC)

- [ ] **Step 4: Verify Step 5 output — wire gauge question**

The skill should ask about available gauges and list at minimum: 14–16 AWG, 18–20 AWG, 20–22 AWG, 26–28 AWG.

- [ ] **Step 5: Verify Step 6 output — files written**

```powershell
Test-Path "docs\wiring\wiring.md"
Test-Path "docs\wiring\wiring.csv"
```

Expected: `True` twice.

Check CSV has a header row and that each wire appears twice (spot-check: find one connection and verify it appears in both directions):

```powershell
$csv = Import-Csv "docs\wiring\wiring.csv"
$csv | Where-Object { $_."FROM Component" -eq "PCA9685" -and $_."FROM Pin #" -match "SDA" }
$csv | Where-Object { $_."TO Component" -eq "PCA9685" -and $_."TO Pin #" -match "SDA" }
```

Expected: one row from each command (the same wire, both directions).

- [ ] **Step 6: Verify Step 7 — inventory handoff**

The skill should either offer to invoke the inventory skill (if installed) or list missing components as plain text. Verify missing components include at minimum: pull-up resistors, ferrite beads on motor leads, inline fuse.

- [ ] **Step 7: Commit generated output**

```powershell
git add "docs\wiring\wiring.md" "docs\wiring\wiring.csv"
git commit -m "docs(wiring): add generated wiring tables for sailboat project"
```
