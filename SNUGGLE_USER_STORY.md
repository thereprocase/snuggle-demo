# Snuggle: Feature Requirements & User Story

## Panel of Four Experts
1. Senior 3D printing engineer (print farm, 100+ parts/week)
2. C++ slicer maintainer (PrusaSlicer fork)
3. OpenGL/GPU compute developer
4. UX designer for manufacturing tools

---

## Five Core Critical Features

### 1. Zero Collisions — Parts Never Overlap
After arrange completes, no two parts intersect in 3D space. One collision that makes it to the printer destroys all trust. The user will never click Snuggle again.

**Scenario:** Alex loads two pegboard boxes plus five tool holders. Snuggle packs them tight. A small holder visually appears to touch the larger box but there is actually a 0.3mm gap. The print completes with zero defects.

### 2. Bed Containment — Nothing Hangs Off the Edge
Every part is fully within the printable area, respecting printer-specific keepout zones (calibration regions, purge areas, bed clips).

**Scenario:** Alex has 7 parts that barely fit on the 256x256mm X1C bed. Snuggle places all 7 within bounds, avoiding the 18mm calibration strip. Alex does not have to remember the keepout zone.

### 3. Undo/Redo — One Ctrl+Z Restores Everything
A single undo returns all parts to pre-arrange positions as one atomic action. Redo re-applies without recomputing.

**Scenario:** Alex runs Snuggle on 11 parts, wonders if rotating a tray would pack better. Ctrl+Z, rotates the tray, runs Snuggle again. Could not have experimented without instant undo.

### 4. Overflow Handling — Excess Parts Go to New Plates Automatically
When parts don't fit on one plate, additional plates are created automatically with a clear count of what went where.

**Scenario:** Alex has 18 parts. Snuggle fits 11 on Plate 1, creates Plate 2 with 7, shows: "Snuggle placed 11 of 18 parts on Plate 1. 7 overflow to Plate 2."

### 5. Cancel Without Corruption — Escape Stops Cleanly
Cancel stops the algorithm, no partial results are applied, all parts remain in pre-arrange positions.

**Scenario:** Alex triggers Snuggle, realizes they forgot to import one more STL. Escape after 1.5 seconds. Parts unchanged. "Snuggle cancelled. Parts unchanged."

---

## Twenty-Five Nice-to-Have Behaviors

### Speed & Responsiveness
1. Sub-3-second arrangement for typical jobs (under 15 parts)
2. Non-blocking UI during computation (viewport stays pannable)
3. Instant single-part centering (no GA for one part)
4. Elapsed time display during long runs
5. Cached results on redo (Ctrl+Y instant, no recompute)

### Visual Feedback
6. Bed utilization percentage in success toast
7. Distinct button label/icon when Snuggle is active
8. Yellow warning toast for partial placement
9. Animated part transition to final positions (200ms lerp)
10. Wipe tower rendered with lock icon during arrange

### Smart Defaults
11. Rotation locked by default (parts keep user orientation)
12. Automatic fallback to standard arrange for 30+ parts
13. Sequential printing triggers hard block (toolhead safety)
14. Spacing default matches printer profile (5mm for FDM)
15. "Remember my choice" toggle for algorithm selection

### Edge Case Handling
16. Parts larger than bed flagged immediately
17. Empty objects skipped gracefully (no crash)
18. Existing parts on other plates never moved
19. Voxelization failure falls back silently
20. Bed 100% full triggers overflow without crash

### Power User Features
21. Arrange-selected-only mode (right-click selection)
22. Split-dropdown button for switching Standard/Snuggle
23. Algorithm comparison via undo/redo toggle
24. Detailed overflow distribution message (which parts, which plates)
25. Snuggle-placed items become fixed obstacles for overflow

---

## User Story: Tuesday Evening with Alex

It is Tuesday, 7:14 PM. I have seven Etsy orders open in a spreadsheet and a mug of cold coffee that I keep forgetting to drink. The orders are a mix of my usual stuff: desk organizers, pegboard accessories, a couple of cable management clips, and one custom headphone stand that a repeat customer asked for in matte black ASA. Eighteen parts total. My X1C is empty and still warm from the last job. Time to batch.

I open OrcaSlicer and start dragging STLs onto the plate. First the big ones: two headphone stand halves, about 160mm each, printed as shells — deeply concave, open on one side like half a clamshell. Then four pegboard boxes in two sizes, a caliper holder, a windscreen tool clip, and nine small cable management pieces that are basically just bent rectangles with screw holes. I have imported all 18 parts in about forty seconds. The bed looks like a bomb went off — parts piled on top of each other at the origin.

I glance at the toolbar. The button says "Snuggle" with the little cluster icon. Good — I set that as my default weeks ago with the "Remember my choice" toggle and I have not looked back. I click it.

The progress bar appears: "Snuggling 18 parts..." with a live timer ticking up. The 3D viewport stays responsive — I pan around to check my filament spool while I wait. I am using eSun PLA+ in black for most of these, bed at 55C, nozzle at 220. The ASA headphone stand halves need 60C bed and an enclosure, so those will be a separate plate anyway. At 2.1 seconds the progress bar finishes.

A green toast pops up: "Snuggle complete — 16 parts arranged. Bed usage: 52%." And then immediately below it, a yellow toast: "2 parts overflow to Plate 2."

I look at the result and the first thing I notice is the two headphone case halves. They are sitting open-side-up on the bed, and two of the smallest cable clips are tucked inside the concavity of the larger half. They are physically sitting inside the bowl of the part, taking up zero additional bed footprint. I did not ask for this. I did not expect it. But of course — that is the whole point of 3D-aware packing. The 2D arranger would have treated those shells as solid rectangles and wasted 80mm of bed space. I zoom in to confirm there is clearance between the nested clips and the shell wall. There is. About 4mm of gap, exactly matching my spacing setting. Clean.

The pegboard boxes are arranged in a tight cluster near the center, with the caliper holder and windscreen tool clip filling the L-shaped gap between them. The remaining cable clips are packed around the periphery. Nothing is hanging off the edge — the algorithm respected the bed bounds including the calibration strip at the back of the X1C bed. I do not even have to check for it anymore.

But wait — two parts overflowed. I click the Plate 2 tab. Two medium cable clips are there, arranged neatly. These are the ones I want to print in ASA anyway for the headphone stand order, so the overflow actually worked in my favor. I will swap the filament profile on Plate 2 to ASA and run it as a separate job.

Actually, hold on. Let me try something. I want to see if I can fit those two ASA clips onto Plate 1 by tightening the spacing. I hit Ctrl+Z. One keystroke, all 18 parts snap back to their pre-arrange positions — one atomic undo, not eighteen individual moves. I change the part spacing from 5.0mm to 3.0mm and click Snuggle again.

1.8 seconds this time. "Snuggle complete — 18 parts arranged. Bed usage: 61%." All 18 on one plate. Nice. But looking at it, the gaps are tight — 3mm between parts on a 7-hour print is asking for stringing artifacts between adjacent walls. I know from experience that 5mm is my safe minimum for PLA+ at 220C. I hit Ctrl+Z again — instant restore, no recomputation — and run Snuggle one more time at 5mm. The previous result reappears. Actually, no, I used Ctrl+Y. It re-applied the cached 5mm result from memory without rerunning the algorithm. Even faster.

I decide to keep the two-plate arrangement. Plate 1 gets 16 PLA+ parts, Plate 2 gets 2 ASA parts. But now I realize I forgot to import the lid for one of the desk organizers. I drag the STL onto Plate 1. It lands on top of the existing arrangement. Instead of re-arranging everything, I select just the new lid, right-click, and choose "Snuggle Selected." The algorithm runs on just that one part, treating the other 16 as fixed obstacles. The lid slides into a gap between two pegboard boxes. Toast: "Snuggle complete — 1 part arranged (16 fixed)." The tight cluster I liked is preserved.

Before I slice, I switch the dropdown from Snuggle to Standard Arrange, just to compare. I run it. The standard 2D arranger spreads everything out in a grid — neat, orderly, and using 71% of the bed with three parts overflowing to Plate 2. The headphone case halves are treated as solid rectangles with nothing nested inside. I Ctrl+Z back to the Snuggle result. 52% bed usage, everything on fewer plates. No contest.

I am about to slice when I notice something: one of the pegboard boxes has a degenerate face — I exported it from Fusion 360 with a known mesh error that I keep meaning to fix. In the old workflow this would sometimes crash the arranger mid-run. But Snuggle handled it transparently. Checking the log, I see the part's voxelization succeeded despite the bad face, and it was placed normally. If it had failed, it would have been routed to the standard arranger as a fallback. Either way, no crash, no corruption.

I slice Plate 1. 6 hours 42 minutes, 147g of PLA+. Plate 2: 1 hour 8 minutes, 23g of ASA. I send Plate 1 to the X1C over LAN and queue Plate 2 for after. The printer starts its bed leveling sequence.

While the first layer goes down, I think about what this used to be like. Six months ago, arrange meant clicking the button, seeing parts spread out in a conservative grid, then spending ten minutes manually dragging small parts closer together because I knew they would fit. The headphone case halves would be placed as fat rectangles with dead space inside them that I could see but the slicer could not. I would manually drag clips into the gaps, eyeballing clearance, sometimes getting it wrong and discovering the collision only after a failed print at 3 AM.

Now I click one button and the software understands that a shell has an inside. That small parts can sit in the bowl of a larger part. That 3D geometry is not the same as a 2D shadow. The arrangement is not just compact — it is intelligent. And when it cannot fit everything, it tells me exactly what went where and lets me undo the whole thing in one keystroke.

It is 7:41 PM. Twenty-seven minutes from opening the slicer to the printer running. Seven orders batched across two plates. I pick up my coffee. It is still cold. But the print is warm.
