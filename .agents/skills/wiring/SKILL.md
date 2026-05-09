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

### Step 0 — Inventory check

Before starting, check whether the inventory skill has already been run:

- **If `docs/inventory/checklist.md` exists**: proceed to Step 1. The BOM has been reviewed and a parts checklist is in place.
- **If `docs/inventory/checklist.md` does not exist**:
  - Check whether `.agents/skills/inventory/SKILL.md` exists.
  - **If yes**: prompt — *"The inventory skill hasn't been run yet. Running it first is recommended to ensure your component list is complete before generating wiring tables — it may identify missing parts that would otherwise appear as TBD rows. Would you like to run the inventory skill now, or proceed with wiring anyway?"* Wait for the user's response before continuing.
  - **If no**: note — *"The inventory skill is not installed. Consider running it before wiring to check for missing components (`npx skills find inventory`)."* Then proceed to Step 1 without waiting.

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

Load and apply `references/passives.md` (located at `.agents/skills/wiring/references/passives.md` relative to the project root) rules to populate the Notes column. Leave Wire Gauge blank at this stage. Note: some passives.md rules specify placement other than the Notes column (e.g., motor capacitors go in the wiring.md motor noise section) — follow passives.md's own guidance on placement.

**Duplication rule**: each physical wire appears in two tables — once in the FROM-component's table (natural direction) and once in the TO-component's table (FROM/TO swapped). This makes each component's table fully self-contained.

### Step 3 — Detect gaps

Flag each of the following:
- BOM components with no pin assignments → mark their connection rows TBD
- Protocol buses missing pins (I²C without both SDA and SCL; SPI without MOSI, MISO, SCK, and CS)
- Power rails with no named source, or consumers referencing a rail with no source
- 3.3V ↔ 5V signal crossings with no logic level shifter → add a level shifter table placeholder
- Ground topology: if no explicit star-ground or common-ground note is present in the project files, add a standing advisory in the power architecture section: "⚠ All component grounds must share a single star-ground point. Verify this physically before powering."

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

Load `references/wire-gauge.md` (located at `.agents/skills/wiring/references/wire-gauge.md` relative to the project root). Assign a recommended gauge to every connection row, including any components added in Step 4. For gauge ranges (e.g. "14–16 AWG"), recommend the heavier end (lower AWG number).

Collect all unique gauges required. Then ask:
> "This project requires the following wire gauges: [X AWG, Y AWG, …]. Which do you have on hand?"

For any required gauge not available, mark the Wire Gauge column `⚠️ [X AWG]` and add to Notes: `Required gauge not available — nearest on hand is [Y AWG]; verify current rating before substituting.`

### Step 6 — Write output

Create `docs/wiring/` if it does not exist.

**`docs/wiring/wiring.md`** structure:

1. **Power architecture section** (before any component tables):
   - Narrative paragraph describing overall power flow
   - ASCII block diagram showing battery → conversion stages → consumers
   - Power rail summary table (rail, voltage, source, consumers)
   - Load/power budget table (load, typical draw, peak draw, rail)
   - Wire gauge table (path, recommended gauge, reason)
   - Motor noise suppression subsection (include only when a motor or ESC is present in the BOM): capacitor placement at motor terminals (three 100nF caps: one across terminals, one from each terminal to motor case), then ferrite ring placement table (location, priority, how to apply)

2. **Per-component sections** — for each component, in order:
   - `##` heading: `## <Component Name> (<Type>[— <Protocol/Address>])`
   - **Introduction paragraph** (1–3 sentences): the component's role and key wiring constraints or design decisions
   - **8-column wiring table**
   - **`>` rationale blockquotes**: one or more blockquote notes for important design decisions, dangerous mistakes to avoid, or cross-component dependencies

   Prefix safety-critical notes with `⚠` wherever they appear — in table Notes cells, blockquotes, or introductions. Examples: conflicting power sources, voltage mismatches, wrong power source for a component, pins that must never be connected to a given voltage.

**`docs/wiring/wiring.csv`** — all connections as a flat CSV. Each wire appears as two rows: first row is natural FROM→TO direction; second row swaps FROM and TO. Header row:

```
FROM Component,FROM Pin #,FROM Descriptor,TO Component,TO Pin #,TO Descriptor,Notes,Wire Gauge
```

Display wiring.md inline in chat for immediate review after writing.

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
