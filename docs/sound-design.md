# Designing OSP's stock content

OSP ships three kinds of stock content. Each is made in OSP itself, never written by hand.

| What | Where it ships | How you make it |
|---|---|---|
| **Built-in starting states** (Natural, Alive, …), the STARTING STATES tiles and the header's first entries | Embedded in the plugin: `apps/plugin/Resources/StartingStates/*.ospstate` | Design a state, **Save Starting State**, copy the file into that folder |
| **Factory presets, templates and sounds** (the Core pack) | One `.osppack` bundled into each plugin and installed into `<Library>/Factory` | Library: **Publish as pack…**, with *Factory pack* ticked |
| **The state a new instance opens on** | The first built-in state | The file that sorts first in the folder |

## Built-in starting states

A built-in state is a **whole state**:
- every setting;
- the modulation;
- the FX route and its effects;
- the USER MACROS;
- the number of source slots.

Choosing one applies all of it, as a starting state from the Library does. The loaded sounds stay.

1. In OSP, set the instrument up as the state should start. Its sounds don't matter: a state is
   settings only.
2. Choose **Save Starting State** (the SAVE menu). It writes
   `Documents/OSP/Starting States/<Name>.ospstate`.
3. Copy the file to `apps/plugin/Resources/StartingStates/` and name it `NN Name.ospstate`.
   - The number sets the order, and is not shown: `01 Natural.ospstate` appears as *Natural*.
   - Replace a file to change a state; add or remove files to change the set.
4. Rebuild. CMake picks the folder up by itself (`CONFIGURE_DEPENDS`).
5. Run the tests: `osp_plugin_tests "[built-in-states]"`. They check seven states named Natural …
   Wide. When you rename, add or remove states, update the expected names in
   `tests/plugin/BuiltInStatesTests.inc`, in the same commit.

Notes:
- **A new instance opens on the first state, applied in full.** The header names it, and the
  controls hold it.
- A saved session, a preset or a host recall restores over it, as before. Old sessions are
  unaffected.
- The seven files in the folder now were baked from the previous built-ins: each is the
  defaults plus their eight macro settings. Replace them with your designs.
  - To bake them again from whatever the built-ins are now:
    `OSP_BAKE_STATES=<folder> osp_plugin_tests "[bake-starting-states]"`.
- A file that cannot be read is skipped, never a crash. With no files at all, the previous
  table (names plus eight settings) stands in.

## The Core factory pack

See `docs/library/packs.md` (Publishing) and `packaging/factory-packs/README.md`.

1. Make a collection of the presets, templates and sounds to ship.
2. Choose **Publish as pack…** on that collection, tick *Factory pack*, and set its version.
3. Attach the `.osppack` to a GitHub Release of this repository.
4. Add its line to `packaging/factory-packs/packs.lock` (tag, file name, SHA-256).

CI fetches and verifies every pack in `packs.lock` before it configures. The build copies them
into the bundles, and OSP installs a factory pack the first time it starts, and again whenever
a newer version ships. Packs are never committed: their audio is large and licensed.
