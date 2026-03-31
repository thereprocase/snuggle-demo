# Snuggle UX/UI Specification
### Peer-Reviewed Design Document for OrcaSlicer Integration

**Authors:** UX Researcher, Competitive Analyst, UX Designer (AI agents)
**Reviewed by:** Council of Elves (30 expectations), Uruk-Hai (5 brutal checks), Fellowship (code review)
**Date:** 2026-03-31

---

## 1. Competitive Landscape

Every major slicer uses 2D convex hull arrangement. None does true 3D-aware packing.

| Feature | PrusaSlicer | Cura | BambuStudio | OrcaSlicer | **Snuggle** |
|---------|------------|------|-------------|------------|-------------|
| Algorithm | NFP/convex | Convex BLF | NFP/convex | NFP/convex | **Genetic 3D voxel** |
| 3D awareness | No | No | No | No | **Yes** |
| Concave nesting | No | No | No | No | Planned |
| Multi-plate auto | No | No | Yes | Yes | **Yes (with fallback)** |
| Sequential safe | No | Partial | No | No | **Yes (bail to default)** |
| Gap control | Yes | Yes | Yes | Yes | Yes |
| Rotation control | Yes | Limited | Yes | Yes | Yes (locked default) |
| Fill bed | Yes | No | No | No | No |

**Snuggle's first-to-market advantage:** True 3D geometry awareness for bed packing. No competitor does this.

---

## 2. Current OrcaSlicer Arrange Workflow

### Entry Points (4 ways to trigger arrange)
1. **Toolbar button** — arranges all/selected (PREPARE_STATE_DEFAULT)
2. **Right-click canvas** — same as toolbar
3. **Right-click plate tab** — arranges current plate only (PREPARE_STATE_MENU)
4. **Plate tab click** — auto-arranges when switching plates

### Current Settings (ArrangeSettings struct)
| Setting | Default | Purpose |
|---------|---------|---------|
| distance | 0 (auto) | Min gap between parts |
| enable_rotation | false | Allow Z-rotation during arrange |
| allow_multi_materials_on_same_plate | true | Material grouping |
| avoid_extrusion_cali_region | true | BBL printer calibration zone |
| is_seq_print | false | Sequential printing mode |
| align_to_y_axis | false | Force Y alignment |

### Current Feedback
- **During:** Progress bar with "Arranging..." text, percentage updates
- **Success:** "Arranging done." toast
- **Overflow:** "Arranging is done but there are unpacked items. Reduce spacing and try again."
- **Failure:** "Arrangement ignored the following objects which can't fit into a single bed: [names]"

### Multi-Plate Handling
- Parts overflow to plates 2, 3, etc. automatically
- Locked plates are respected (items don't move)
- Items on other plates become fixed obstacles during per-plate arrange
- Unprintable items moved to virtual overflow plate

---

## 3. Snuggle UI Design

### 3.1 Primary Access: Arrange Button Split-Dropdown

The existing Arrange toolbar button becomes a split button:

```
[  Arrange  |v]        →  Click: arrange with last-used method
              |            Dropdown:
              |              Standard Arrange  (2D outlines)
              |              Snuggle Arrange   (3D-aware packing)
              |            ────────────────────────────
              |              [x] Remember my choice
```

When Snuggle is selected, the button label changes to **Snuggle** with a distinct cluster icon. The user always knows which algorithm is active.

### 3.2 Settings Panel

Snuggle options appear in the existing arrange options panel when Snuggle is the active method:

```
Arrangement Method:  [Snuggle v]

Snuggle Options:
  Part spacing:     [5.0] mm        (min gap between parts)
  Rotation:         [Locked v]      (Locked = XY only, Free = XY+Z)

  [Arrange]  [Reset]
```

When Standard is selected, these options are hidden and the existing settings appear.

### 3.3 Tooltip Text

**Toolbar button (hover):**
> **Snuggle Arrange** — Packs parts using their full 3D shapes instead of 2D outlines. Produces tighter blob-shaped clusters. Best with 2-30 parts. Takes a few seconds.

**Rotation setting (?):**
> **Locked:** Parts keep their current Z-rotation. Snuggle only adjusts XY position. Safest option — collision detection is fully verified.
>
> **Free:** Snuggle may rotate parts around the Z axis for tighter packing. Note: rotated collision detection is approximate in this version. Inspect results before printing.

### 3.4 Progress and Feedback

| State | What User Sees |
|-------|---------------|
| Running | Non-modal progress bar: `Snuggling 5 parts...` with elapsed time |
| Success | Green toast: `Snuggle complete — 5 parts arranged. Bed usage: 34%` |
| Partial | Yellow toast: `Snuggle placed 3 of 5 parts. 2 overflow to Plate 2.` |
| Cancel | Toast: `Snuggle cancelled. Parts unchanged.` |
| Fallback | Yellow info bar: `40 parts loaded. Snuggle works best with 2-30. Using standard arrange. [Use Snuggle anyway] [OK]` |
| Sequential | Info bar: `Sequential printing active. Snuggle doesn't check toolhead clearance. Using standard arrange.` (hard block, no override) |
| Parts don't fit | Red toast: `Could not fit all parts on bed. 2 parts moved to Plate 2.` |

---

## 4. Workflow Scenarios

### Scenario 1: Basic — 5 STLs, pack tight
- User loads 5 files, clicks Snuggle
- Progress bar: `Snuggling 5 parts...` (2-5 seconds)
- Parts animate to new positions (200ms lerp)
- Toast: `Snuggle complete — 5 parts arranged`
- Undo with Ctrl+Z restores original positions

### Scenario 2: Incremental — 3 placed + 2 new
- User selects the 2 new parts
- Right-click > Snuggle Selected (or click Snuggle button with selection)
- 3 existing parts become fixed obstacles
- Only the 2 selected parts move
- Toast: `Snuggle complete — 2 parts arranged (3 fixed)`

### Scenario 3: Too many parts (40+)
- Info bar: `40 parts loaded. Snuggle works best with 2-30. Using standard arrange for speed. [Use Snuggle anyway] [OK]`
- OK → standard arrange runs immediately
- Use Snuggle anyway → runs with time estimate and cancel button

### Scenario 4: Sequential printing
- **Hard block.** Info bar: `Sequential printing active. Snuggle doesn't check toolhead clearance. Using standard arrange.`
- No override — toolhead crash risk is too high
- User must disable sequential printing to use Snuggle

### Scenario 5: Wipe tower
- Wipe tower rendered with lock icon during arrange
- Snuggle treats wipe tower footprint as exclusion zone
- Parts placed around it
- Toast: `Snuggle complete — 5 parts arranged (wipe tower area reserved)`

### Scenario 6: Cancel mid-arrange
- User clicks X on progress bar or presses Escape
- Algorithm stops at end of current generation
- Parts remain in pre-Snuggle positions — nothing applied
- Toast: `Snuggle cancelled. Parts unchanged.`

### Scenario 7: Undo
- Ctrl+Z after Snuggle → all parts return to pre-Snuggle positions (one atomic undo step)
- Edit menu shows: `Undo Snuggle Arrange`
- Ctrl+Y re-applies without re-running algorithm

### Scenario 8: Compare algorithms
- User runs Snuggle, notes result
- Ctrl+Z to undo
- Switches to Standard Arrange in dropdown
- Runs Standard, compares
- Undo/Redo toggles between results
- Future: dedicated "Compare Arrangements" mode

---

## 5. Overflow and Multi-Plate Requirements

### 5.1 Overflow Flow

```
Snuggle runs on all selected parts
  │
  ├─ Parts with valid voxels → nester places them
  │   ├─ Fits on bed? → bed_idx = 0 (placed, LOCKED)
  │   └─ Out of bounds? → UNARRANGED
  │
  ├─ Parts with empty/failed voxels → UNARRANGED
  │
  └─ UNARRANGED items → default arranger
      ├─ Snuggle-placed items are FIXED OBSTACLES
      ├─ Default arranger places overflow around them
      │   ├─ Fits on Plate 0? → bed_idx = 0
      │   └─ Doesn't fit? → bed_idx = 1, 2, ... (multi-plate)
      └─ Still doesn't fit → moved to virtual overflow plate + warning
```

### 5.2 Position Locking Rules

| Item State | Position Source | Locked? | Can Move? |
|-----------|---------------|---------|-----------|
| Snuggle-placed | Snuggle nester | YES | No — fixed obstacle for overflow |
| Overflow (placed by default) | Default arranger | YES | No — final position |
| Overflow (can't fit) | Unchanged | N/A | Moved to overflow plate |
| Already on other plate | Original position | YES | Not touched |
| Locked plate items | Original position | YES | Not touched |
| Wipe tower | Printer config | YES | Not touched by Snuggle |

### 5.3 Multi-Plate Requirements

1. **Snuggle operates on a single plate at a time.** It places as many parts as fit on the current plate.
2. **Overflow goes to the default arranger**, which handles multi-plate distribution using Orca's existing plate system.
3. **Parts already on other plates are never moved** by Snuggle.
4. **New plates are created automatically** when overflow items don't fit on the current plate.
5. **Plate count is not limited** — overflow can create plates 2, 3, 4, etc.
6. **The user sees a clear count:** `Snuggle placed 8 of 12 parts on Plate 1. 4 overflow to Plates 2-3.`

### 5.4 Edge Cases

| Scenario | Behavior |
|----------|----------|
| 1 part | Center on bed instantly (no GA) |
| 0 parts selected | Arrange all (same as current) |
| All parts UNARRANGED by Snuggle | Entire job goes to default arranger (transparent fallback) |
| Part larger than bed | Marked UNARRANGED in pre-flight, goes to overflow plate with warning |
| >128 parts | Immediate fallback to default arranger with info bar |
| Bed 100% full | Warning toast, no crash, overflow to next plate |
| Part with no mesh (empty object) | Skipped, marked UNARRANGED |

---

## 6. Settings Interaction Matrix

When Snuggle is active, some existing settings change meaning or are ignored:

| Existing Setting | Under Standard | Under Snuggle | Notes |
|-----------------|---------------|---------------|-------|
| distance (gap) | Min gap in 2D | Min gap in 3D voxel space | Same user-facing meaning |
| enable_rotation | Allows 2D rotation | Hidden — replaced by Snuggle rotation control | Avoid two rotation toggles |
| align_to_y_axis | Aligns parts | Not used by Snuggle | Grey out or hide |
| is_seq_print | Seq mode | Snuggle bails entirely | Hard block |
| avoid_extrusion_cali_region | Excludes zone | Logged but not enforced by Snuggle | Overflow fallback handles it |
| allow_multi_materials | Material grouping | Not used by Snuggle | Future work |

---

## 7. Visual Language

| Element | Standard Arrange | Snuggle |
|---------|-----------------|---------|
| Button label | "Arrange" | "Snuggle" |
| Button icon | Grid/scatter pattern | Cluster/blob pattern |
| Progress text | "Arranging..." | "Snuggling N parts..." |
| Toast (success) | "Arranging done." | "Snuggle complete — N parts. Bed: X%" |
| Toast (partial) | "Unpacked items..." | "N of M placed. K overflow to Plate 2." |
| Toast (fail) | "Objects can't fit..." | "Could not fit N parts. Try fewer parts." |

---

## 8. Not in v1 (Documented Future Work)

| Feature | Why Deferred | When to Add |
|---------|-------------|-------------|
| Concavity exploitation | Algorithm not ready | When concavity detection ships |
| Compare arrangements split-view | Undo/redo is sufficient | If users request it |
| Per-part lock toggles | Selection-based arrange covers this | If workflow proves insufficient |
| Material grouping | Default arranger handles overflow | When Snuggle adds material awareness |
| Adaptive voxel resolution | Fixed 1mm works for 90% of parts | When extreme size ratios cause issues |
| GPU-accelerated nesting | CPU is fast enough for 2-30 parts | When users want 100+ parts |

---

## 9. Implementation Mapping

| UX Element | Code Location | Status |
|-----------|--------------|--------|
| use_snuggle setting | ArrangeSettings, ArrangeParams | Done |
| snuggle_lock_rotation | ArrangeSettings, ArrangeParams | Done |
| Snuggle hook in ArrangeJob | ArrangeJob.cpp:568 | Done |
| Overflow fallback | ArrangeJob.cpp:572-603 | Done |
| Cancel wiring | SnuggleArrange.cpp nester callback | Done |
| Progress wiring | SnuggleArrange.cpp nester callback | Done |
| Sequential bail | SnuggleArrange.cpp:186-191 | Done |
| Pre-flight size check | SnuggleArrange.cpp:88-101 | Done |
| Split-dropdown button | Not yet implemented | **Needs UI work** |
| Settings panel toggle | Not yet implemented | **Needs UI work** |
| Toast messages | Partial (generic "complete" only) | **Needs detail messages** |
| Animation lerp | Not implemented | **Future polish** |

---

*This document represents the consensus of three independent design reviews plus five audit passes. All claims about current code behavior are verified against the Syms branch commit 7944021aa2.*
