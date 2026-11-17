# Popovers stay open through macro and modulation editing (17 November 2026)

## Verified root cause

The shared `ModulationOverlay` is one layer above the whole instrument, popovers included. It
claims two kinds of presses through its `hitTest`:
- a right-click (or macOS Control-click) on any destination;
- a press on a halo's range handle or depth ring.

The sequence:
1. JUCE routes such a press to the overlay, not to the popover's knob. The tests assert this:
   `getComponentAt` with the right button held returns the overlay.
2. The overlay's `mouseDown` opens the knob's menu, or starts the range / depth gesture.
3. JUCE then tells the desktop listeners. The editor's outside-click watcher
   (`OspAudioProcessorEditor::mouseDownAnywhere`; the shell's `ShellEditor` likewise) receives
   the press with `eventComponent == overlay`.
4. The overlay is neither the popover nor inside it, so the watcher treated the press as an
   outside click and closed the popover.

The menu had already been built from the knob, which is why "Assign to Macro" still worked
while LIFE vanished. The same thing happened on a halo-handle press.

A right-click that reaches the knob itself (the overlay not claiming it) was already internal:
the knob is inside the popover. So was Escape during a range drag: the overlay holds the
keyboard focus while it edits, and cancels first.

## The fix (logical ownership)

- `ModulationOverlay::pressedControl()` / `pressBelongsTo (owner)`: the overlay remembers which
  control its latest press acted on (a `SafePointer`, set in `mouseDown`).
- The outside-click watchers (OSP's editor and the shared `ShellEditor`) treat an overlay press
  as internal only when that control is inside the open popover.
- An overlay press for a control elsewhere, or on nothing, is an outside click as before.
- Nothing is disabled globally. The popover is never closed and reopened.
- `UserMacroController::mouseDownAnywhere`: a press on a control the chosen macro moves (turning
  the knob being worked on) keeps its range shown.

Unchanged:
- Occlusion was already right: `findCovers`, so a background control's halo under a popover
  neither draws nor takes presses.
- Gesture ownership was already right: the overlay keeps the pressed component through the drag,
  so leaving the popover's bounds abandons nothing.

## Tests (`[popover]`)

Both event paths are covered:
- **Real window events:** the editor on the desktop, presses given to its native peer. These run
  whenever a display is available (Linux with `DISPLAY`, or `OSP_REAL_EVENTS=1`). Xvfb needs the
  window-manager atoms JUCE looks up: see the session's `realx.sh`, which runs
  `Xvfb -noreset` and interns them first.
- **JUCE's routing replayed step by step:** the component under the point with the buttons held,
  its handler, the root's nested listeners, then the desktop watcher.

Covered:
- LIFE, through a real right-click on PITCH: Assign to Macro → Macro 1, the focused halo, the 100 %
  end dragged out of LIFE and back, the 0 % end, inverted, Shift fine, Escape mid-drag (mapping,
  knob and undo untouched), Reset from the menu, a cancelled menu, the knob turned normally (halo
  still focused), undo / redo; then a genuine outside click closes LIFE.
- Every popover with eligible knobs (LIFE, DRIVE, FILTER, MOVEMENT, SPACE, ECHO, REIMAGINED,
  PITCH BEND, MOD WHEEL, ADVANCED): a right-click on its own knob keeps it open; Escape with
  nothing active closes it.
- An LFO depth dragged on a halo inside a popover.
- The editor destroyed while a menu is pending: its items are safe.
- macOS Control-click: checked only on macOS (JUCE makes Control a popup press there only).
