# Inventory Skill — Design Spec

**Date:** 2026-05-09
**Status:** Approved

---

## Purpose

A general-purpose Claude Code skill that reads project files and design docs, identifies the features the project intends to implement, checks whether the component inventory covers those features, suggests implied components and consumables the BOM is missing, cross-references against a user-maintained on-hand list, and writes a structured parts checklist (Markdown + CSV) ready for ordering.

The skill works standalone and as a companion to the wiring skill. When invoked after the wiring skill, it optionally receives the wiring skill's gap list as additional context to produce a more precise checklist.

---

## Relationship to the Wiring Skill

| Phase | Recommended skill order | What it produces |
|---|---|---|
| Planning — "what do I need to buy?" | Inventory first | Feature coverage, component selection gaps, shopping list |
| Wiring — "how do I connect it?" | Wiring second | Per-component connection tables, passive placement |
| Gap check — "did I miss anything?" | Inventory again (optional) | Refined checklist using wiring-identified passives as context |

Both skills share the same discovery step (they read the same project files). Running inventory before wiring produces a general checklist; running it after wiring (via the handoff in wiring Step 7) produces a more precise one with quantities and placement detail carried over from the wiring analysis.

The inventory skill accepts an **optional gap list** passed by the wiring skill, but never requires it.

---

## Workflow

The skill runs this sequence when invoked:

1. **Discover** — scan for known project files: `docs/hardware.md`, `docs/pinmap.md`, firmware `config.h`, `README.md`, `TODO.md`, and any file matching `**/bom*.md`, `**/wiring*.md`, `**/hardware*.md`. Extract: component inventory, power rails, protocol buses, and GPIO assignments.

2. **Detect features** — identify intended capabilities from project docs (README, TODO.md). Present the detected feature list and ask: *"I found these in-scope features — is this list complete, and are all of them in scope for this build phase?"* Use the confirmed list for all downstream steps. Features marked out of scope are noted but excluded from the checklist.

3. **Identify feature gaps** — for each confirmed in-scope feature, check whether the BOM contains a component that implements it. Flag uncovered features in the output with a plain-language note. Where multiple implementation approaches exist (e.g., float switch vs. resistive vs. capacitive for water detection), describe the alternatives but do not force a choice — leave resolution to the user.

4. **Apply reference rules** — load `references/components.md` and `references/consumables.md`. For each rule whose trigger condition is satisfied by the current BOM, add the implied component or consumable to the checklist if it is not already present. Load `references/features.md` to enrich feature gap notes with common implementation options.

5. **Cross-reference on-hand** — load `docs/inventory/on-hand.md` if present. Match each checklist item against the on-hand list (fuzzy name match — "4.7k resistor" matches "4.7kΩ pull-up resistor"). Mark each item as ✅ Have or 🛒 Buy. If the on-hand file does not exist, mark all items 🛒 Buy and note that the file can be created to enable filtering on future runs.

6. **Ask targeted questions** — one per genuine gap: unresolved quantities, unspecified voltage ratings, or ambiguous component choices that cannot be resolved from the project files. Only ask about things that would change the checklist.

7. **Write output** — create `docs/inventory/` if it does not exist. Write `docs/inventory/checklist.md` and `docs/inventory/checklist.csv`.

---

## Reference Files

```
.agents/skills/inventory/references/
  components.md    — electronics and passive rules, keyed by what is present in the BOM
  consumables.md   — consumables rules, keyed by activity type
  features.md      — feature-to-component mapping with common alternatives
```

### components.md format

Keyed by trigger condition → implied component:

| If present in BOM | Likely needed | Specification | Search hint |
|---|---|---|---|
| I²C bus | 4.7kΩ pull-up resistors | Qty 2, 1/4W through-hole | search Mouser: "4.7k ohm resistor through-hole" |
| Motor or ESC | Ferrite clamp cores | Qty 4+, snap-fit Fair-Rite 31 material | search Mouser: "ferrite snap clamp 31 material" |
| Motor or ESC | Ceramic capacitors 100nF | Qty 3, 50V, for motor terminal noise suppression | search Mouser: "100nF 50V ceramic cap" |
| Battery main lead | Inline fuse holder + fuses | Rated to motor stall current | search Mouser: "ATC blade fuse holder" |
| Battery main lead | Battery connector pair | XT60 or EC5, rated for peak current | search Mouser: "XT60 connector pair" |
| 3.3V ↔ 5V signal crossings | Logic level shifter module | Bidirectional, I²C compatible | search Amazon: "logic level shifter 4-channel" |
| LiPo battery | LiPo-safe charging bag | Sized for battery pack | search Amazon: "LiPo safe bag" |
| LiPo battery | Battery voltage alarm | Plugs into balance lead, piezo buzzer | search Amazon: "LiPo battery alarm" |

### consumables.md format

Keyed by activity type → consumable:

| If the project involves | Likely needed | Specification | Search hint |
|---|---|---|---|
| Any soldered connections | Solder | 60/40 or lead-free, 0.6–1.0mm | search Amazon: "electronics solder 0.8mm" |
| Any soldered connections | Flux pen | No-clean rosin flux | search Amazon: "flux pen no-clean" |
| Any soldered connections | Heat shrink assortment | Mixed sizes 2:1 ratio | search Amazon: "heat shrink assortment" |
| Any soldered connections | Helping hands / PCB holder | — | — |
| Wire runs | Wire stripper | Rated for 14–26 AWG range | — |
| Crimped connectors | Crimping tool | Rated for connector type | — |
| Waterproofing required | Conformal coat spray | — | search Amazon: "conformal coat spray electronics" |
| Waterproofing required | Silicone sealant | — | — |

### features.md format

Keyed by feature name → implementation options:

| Feature keyword | Common implementations | Notes |
|---|---|---|
| water detection / bilge | Resistive probe (e.g., DYIables), float switch, capacitive sensor | Resistive is simplest; float switch is binary; capacitive needs calibration |
| GPS positioning | u-blox M8N (e.g., BN-880), u-blox M9N, Neo-6M | M8N is best balance of accuracy and cost; Neo-6M is budget option |
| compass / heading | HMC5883L (often bundled with GPS), QMC5883L, onboard IMU | BN-880 includes HMC5883L; verify no magnetic interference from motor |
| current monitoring | INA228, INA219, INA226 | INA228 has higher resolution; all are I²C compatible |
| motor speed | Brushed ESC, brushless ESC | Match to motor type; brushed ESC for brushed motors only |
| wind sensing | Ultrasonic anemometer, mechanical cup anemometer | Not yet addressed in this project |

---

## Output Format

### checklist.md structure

```markdown
# Parts Checklist — <Project Name>
_Generated <date>._

## Feature Coverage

| Feature | Status | Notes |
|---|---|---|
| Bilge monitoring | ✅ Covered | DYIables resistive sensor + MOSFET pump driver |
| GPS positioning | ✅ Covered | BN-880 (u-blox M8N + HMC5883L) |
| Water detection sensor type | ⚠ Gap | Sensor listed as DYIables resistive — confirm this is the intended choice |

## Parts

| Status | Component | Qty | Specification | Purpose | Search hint |
|---|---|---|---|---|---|
| ✅ Have | 4.7kΩ resistors | 2 | 1/4W through-hole | I²C pull-ups | — |
| 🛒 Buy | Ferrite clamp cores | 4 | Snap-fit, Fair-Rite 31 material | Motor/ESC EMI suppression | search Mouser: "ferrite snap clamp 31" |
| 🛒 Buy | LiPo safe charging bag | 1 | Sized for battery pack | Safe LiPo charging and storage | search Amazon: "LiPo safe bag" |

## Consumables

| Status | Item | Qty | Specification | Purpose | Search hint |
|---|---|---|---|---|---|
| ✅ Have | Solder | — | 60/40 0.8mm | Soldering connections | — |
| 🛒 Buy | Conformal coat spray | 1 can | — | Waterproofing PCB joints | search Amazon: "conformal coat electronics" |
```

### checklist.csv structure

Flat CSV with header row. No sections — all rows in one table:

```
Status,Component,Qty,Specification,Purpose,Search hint
✅ Have,4.7kΩ resistors,2,1/4W through-hole,I²C pull-ups,—
🛒 Buy,Ferrite clamp cores,4,"Snap-fit, Fair-Rite 31 material",Motor/ESC EMI suppression,"search Mouser: ferrite snap clamp 31"
```

### on-hand.md format

User-maintained. Simple list with optional quantities:

```markdown
# On-Hand Inventory

- 4.7kΩ resistors (qty 20)
- Heat shrink assortment
- XT60 connectors (qty 4)
- Solder (60/40)
```

The skill matches component names loosely — "4.7k resistor" matches "4.7kΩ pull-up resistor". If a quantity is specified and the required quantity exceeds it, the item is marked 🛒 Buy with a note.

---

## Wiring Skill Integration

At the end of the wiring skill workflow (Step 7), if missing or recommended components were identified and `.agents/skills/inventory/SKILL.md` exists, the wiring skill offers:

> *"I identified N components that aren't in your BOM. Would you like me to invoke the inventory skill to turn these into a parts checklist?"*

When invoked this way, the wiring skill passes its gap list as inline context. The inventory skill uses this to:
- Pre-populate checklist items with wiring-specific quantities and placement notes
- Skip re-asking questions the wiring skill already resolved
- Add the wiring gap items to the output before applying its own reference rules

If the inventory skill is not installed, the wiring skill lists missing components as plain text and notes: *"An inventory skill can be added with: `npx skills find inventory`"*

---

## File Structure

```
.agents/skills/inventory/
  SKILL.md
  references/
    components.md    — electronics and passive rules
    consumables.md   — consumables rules
    features.md      — feature-to-component mapping

docs/inventory/
  on-hand.md         — user-maintained list of parts already owned
  checklist.md       — generated output
  checklist.csv      — generated output
```
