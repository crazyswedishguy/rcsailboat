---
name: inventory
description: Build a structured parts checklist for electronics projects. Reads project BOM, design docs, and README to identify in-scope features, check component coverage, suggest missing parts and consumables, and cross-reference against an on-hand list. Outputs Markdown + CSV ready for ordering. Invoke when planning a build, checking what to buy, or after the wiring skill identifies component gaps.
license: MIT
metadata:
  author: crazyswedishguy
  version: "1.0.0"
  domain: specialized
  triggers: inventory, parts list, shopping list, what do I need, missing components, parts checklist, bill of materials, BOM, what should I buy
  role: specialist
  scope: planning + documentation
  output-format: markdown + csv
  related-skills: wiring
---

# Inventory Assistant

Electronics inventory specialist. Reads project files and design docs to identify in-scope features, checks BOM coverage, suggests missing components and consumables, cross-references an on-hand parts list, and writes a structured checklist (Markdown + CSV) ready for ordering.

This skill works standalone and as a companion to the wiring skill. When invoked after the wiring skill, it optionally receives the wiring skill's gap list as additional context. It never requires this input — the same project files are read regardless.

## Workflow

### Step 1 — Discover project files

Scan for and read these files if present:
- Any file named `hardware.md`, `bom.md`, `schematic.md`, or `components.md` — BOM and power rails
- Any file named `pinmap.md`, `pinout.md`, `pins.md` — GPIO and pin assignments
- Any file matching `**/config.h`, `**/config.hpp`, `**/pins.h`, `**/pinout.h` — GPIO and hardware defines
- `README.md` — project overview and feature descriptions
- `TODO.md`, `ROADMAP.md` — planned features
- Any file matching `**/bom*.md`, `**/hardware*.md`, `**/wiring*.md`, `**/schematic*.md`

Extract from every file found:
- **Component inventory**: names, types, communication protocols, I²C addresses
- **Power rails**: voltage sources and their consumers
- **Intended features**: capabilities described in narrative text (README, TODO.md) and implied by component names and GPIO identifiers
- **GPIO assignments**: MCU GPIO number → peripheral

### Step 2 — Detect features

Load `references/features.md` (in the `references/` subdirectory alongside this `SKILL.md`). For each row in the Feature Map, search discovered project content for the keywords in the "Keywords" column. A keyword match in any component name, GPIO identifier, or narrative text counts as a detected feature.

Present the detected feature list:
> "I detected these intended features in your project: [list]. Which are in scope for this build phase? Reply with 'all', or specify which to include or exclude."

Wait for the user's response. Use the confirmed list for all downstream steps. Out-of-scope features appear in the Feature Coverage table as "Out of scope" and are excluded from the parts checklist.

### Step 3 — Identify feature gaps

For each confirmed in-scope feature, check the BOM for a component matching the "BOM coverage check" column in `references/features.md`.

- **Covered**: mark ✅ Covered in the Feature Coverage table, noting the component name.
- **Not covered, one implementation**: add to the Parts checklist as 🛒 Buy with a ⚠ Gap note. Mark the feature ⚠ Gap in the Feature Coverage table.
- **Not covered, multiple implementations**: add a ⚠ Gap note to the Feature Coverage table using the "Alternatives (if gap)" column verbatim. Do not choose an implementation for the user. Add a TBD placeholder row to the Parts table.

### Step 4 — Apply reference rules

Load:
- `references/components.md` (in the `references/` subdirectory alongside this `SKILL.md`)
- `references/consumables.md` (in the `references/` subdirectory alongside this `SKILL.md`)

For each rule whose trigger condition is satisfied by the current BOM or confirmed feature list, add the implied item to the checklist if it is not already present. Apply **all** matching rules — do not stop at the first match.

If the wiring skill passed a gap list as inline context, merge those items now: carry over wiring-specific quantities and placement notes for items already in the checklist; add new rows for items unique to the wiring gap list.

### Step 5 — Cross-reference on-hand inventory

Check whether `docs/inventory/on-hand.md` exists.

- **If present**: for each checklist item, search the on-hand list for a loose name match ("4.7k resistor" matches "4.7kΩ pull-up resistor"). If a quantity is specified on-hand and the required quantity exceeds it, mark 🛒 Buy with a note: "Have [X], need [Y]." Otherwise mark ✅ Have.
- **If absent**: mark all items 🛒 Buy. Add this note at the top of `checklist.md`: *"Tip: create `docs/inventory/on-hand.md` with a list of parts you already own — the skill will cross-reference it on future runs."*

### Step 6 — Ask targeted questions

Ask one question per genuine gap that cannot be resolved from project files or reference rules:
- Unresolved quantities
- Unspecified voltage or current ratings
- Component choices still marked TBD

Ask questions one at a time. Do not ask about anything already determinable from files.

### Step 7 — Write output

Create `docs/inventory/` if it does not exist.

**`docs/inventory/checklist.md`** structure:

```markdown
# Parts Checklist — <Project Name>
_Generated <date>._

> <on-hand tip if docs/inventory/on-hand.md is absent>

## Feature Coverage

| Feature | Status | Notes |
|---|---|---|
| <feature> | ✅ Covered | <component name> |
| <feature> | ⚠ Gap | <alternatives or TBD note> |
| <feature> | Out of scope | Excluded from this build phase |

## Parts

| Status | Component | Qty | Specification | Purpose | Search hint |
|---|---|---|---|---|---|
| ✅ Have / 🛒 Buy / ⚠ Gap TBD | <name> | <qty or TBD> | <spec> | <purpose> | <hint or —> |

## Consumables

| Status | Item | Qty | Specification | Purpose | Search hint |
|---|---|---|---|---|---|
| ✅ Have / 🛒 Buy | <name> | <qty or —> | <spec or —> | <purpose> | <hint or —> |
```

**`docs/inventory/checklist.csv`** — flat CSV of all Parts and Consumables rows combined. No Feature Coverage section. Each item appears once. Header:

```
Status,Component,Qty,Specification,Purpose,Search hint
```

Display `checklist.md` inline in chat after writing.

## Reference Files

| File | Load when |
|---|---|
| `references/features.md` | Steps 2 and 3 — always |
| `references/components.md` | Step 4 — always |
| `references/consumables.md` | Step 4 — always |

## Output Contract

The generated `docs/inventory/checklist.md` and `docs/inventory/checklist.csv` are the deliverables. Do not consider the skill complete until both files exist and have been confirmed by the user.
