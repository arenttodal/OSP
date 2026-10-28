# 04 — Interaction

All of this is implemented in the shared components. A product inherits it by composing them
(the shell does). Nothing here is to be re-implemented.

| Pattern | Behaviour | Where |
|---|---|---|
| Scaling | The window keeps the instrument's proportions (869–1810 px wide). Content is never cut off; a host's odd shape gets a margin in the surround colour. | `frame::fit`, `frame::applyWindowShape` |
| Knobs | Rotary drag (horizontal or vertical), from 1.25π to 2.75π. Double-click resets. A value bubble on graphite shows the value while turning. | `OspLookAndFeel`, `setUpMacro`, `LayerKnob` |
| Macro names | When the macro has a popover, hover darkens and underlines the name, and a click opens it. A second click on the name closes it. Without a popover the name is plain text. | `MacroLabel`, `setUpMacro (…, opensPopup)` |
| Popovers | Opening: a 110 ms fade and a 3 px settle. They are placed above their door (a macro name, Advanced, the modulation panel) and kept inside the instrument. Escape, the close button or a click elsewhere closes them. One is open at a time. | `MiniPanel`, `frame::placePopover`, `frame::animatePopoverOpening` |
| Cards | Adapt to 1/2/3 layers (hero/dual/triple). A new layer settles in with a 200 ms fade. A click focuses a card. The badge shows the sound's details on rest. | `EngineCard`, `frame::sourceColumn` |
| Drop a sound | On the instrument: a new layer. On a card: that layer's sound. While dragging, the cards make room and the new place says ADD LAYER. | the editor (OSP's and the shell's) |
| Granular | Resting on the display shows POS SIZE DENS TUNE SPREAD (fading). | `EngineCard` |
| EQ / DAMPEN | The EQ key opens the layer's EQ over its waveform. DAMPEN is its second page. One is open at a time. | `EqEditor`, `DampenEditor` |
| Modulation | Drag a tab (AMP … NOTE) or the MOD wheel's caption onto a control to make a route. Halos show and edit the routes; a halo's ring takes depth modulation. Right-click offers "Assign". Refusals say why. | `ModulationPanel`, `ModulationOverlay`, `InstrumentHost::assignModulation` |
| LINK | Changing START, TUNE, PAN, LEVEL or REIMAGINED on a linked layer moves the other linked layers by the same amount. | `InstrumentHost::applyLinkedDelta` |
| MUTE/SOLO | Alt-click on S: solo this layer alone. | `InstrumentHost::soloOnly` |
| Keyboard | Plays MIDI. Held keys warm. PITCH springs back; MOD stays where it is put. | `OspKeyboard`, `Wheel` |
| ARP | The light switches it on or off; a click on the card opens its editor, and the window grows. | `ArpControl`, `ArpInlinePanel` |
| Disabled | A control whose parameter the product's engine does not read cannot be used and is shown at 0.4 alpha. The look-and-feel also dims it. | `ShellEditor::applyBindings`, `binding::` |
