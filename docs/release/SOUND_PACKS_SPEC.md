# OSP sound packs: one core installer, expansion packs, and how to sell them

**Status:** decision and spec, 9 November 2026. The pack format, the factory install, publishing
and the build step exist and are tested (section 6). The website catalogue and the in-app
"More sounds" page are proposals (sections 4 and 5).

## 1. Recommendation

1. **One OSP installer** carries the plug-ins and a **Core** factory pack of 50–100 presets.
2. **Expansion packs** (instruments and styles) are sold on the Evenant website as **`.osppack`
   downloads**. The musician installs one with **Library › Packs… › Install pack…**.
3. **Later (phase 2):** a read-only **"More sounds"** page in OSP lists the packs, plays their
   demos, shows which are installed and which have updates, and opens the website to buy.
4. **Not now:** checkout, accounts and downloads inside the plug-in. Build them only if phase 2
   shows that many people browse in OSP and are lost at the website (section 5).

**Why not an in-app store first.** A store inside a plug-in needs:
- accounts and sign-in;
- a licence server;
- a payment integration;
- a download manager that resumes and checks files.

It runs inside other companies' DAWs, behind proxies and offline studios. That is weeks of
work, a security surface and a support load before the first pack sells. OSP is sold outside
the Mac App Store, so Apple's in-app purchase rules and its cut do not apply. Paying on the
website is normal for plug-ins and costs nothing extra.

What an in-app store would add is **discovery**: people who own OSP see what else exists while
they make music. Phase 2's catalogue gives most of that at a fraction of the cost, because it
only reads a public list and links out.

**Why the website.** It sells to people who do not own OSP yet: search, ads, the email list,
YouTube demos. Packs are sold in the same store as the OSP and Analog installers, with the same checkout,
receipts and refunds. Analog's purchase-flow spec (`docs/release/PURCHASE_AND_DOWNLOAD_FLOW.md`
in the analog repository) recommends a merchant-of-record store, because it collects VAT and
sales tax worldwide. If the Evenant site's own shop is used instead, check how it handles tax
on digital goods sold abroad. Either way, the store's digital delivery sends the file after
payment.

## 2. How it works, end to end

### Making a pack (the publisher, inside OSP)

1. Put the presets (and any loose sounds) in a **collection** in the Library.
2. Right-click the collection › **Publish as pack (id, version, licence)…**
   - **Pack id:** `com.evenant.osp.<name>`, for example `com.evenant.osp.core` or
     `com.evenant.osp.dusty-keys`. **Never change it**: updates find the installed pack by it.
   - **Version:** `1.0.0` for the first release. The sheet suggests the next version each time.
   - **Creator, licence, description:** shown to the musician before installing.
   - **Factory pack:** tick it for the Core pack only.
3. Save the `.osppack`. Every preset brings the sounds it needs. A sound whose rights are
   unknown (found on a drive) is named, and the pack is not made until you confirm you may
   ship it.

The sheet remembers the fields per collection (`<Library>/Publishing/`). Publish every version
of a pack from the **same collection in the same Library**: the items keep their identities,
and that is what keeps a customer's favourites and tags through an update.

### The Core pack (in the installer)

1. Copy the Core `.osppack` into `packaging/factory-packs/` (or point
   `-DOSP_FACTORY_PACKS_DIR` at its folder).
2. Build. Every plug-in bundle gets it in `Contents/Resources/Factory`:
   - macOS: AU, VST3 and the app;
   - elsewhere: the VST3.
   `scripts/package-macos.sh` then signs and packages as before.
3. **First start:** OSP installs it into `<Library>/Factory/<id>`. This is apart from the
   musician's own presets, which are never touched.
4. **Updates:** ship a higher version of the same id with a new OSP release. It replaces the
   old factory content, and the musician's favourites, ratings and tags stay.

With no pack in the folder, the build is exactly what it was: no file is added to any bundle.
The Core pack is not committed to git (size, licensed audio). Keep it with the release (a
GitHub Release asset with its SHA-256), and copy it in before building the installer.

### An expansion pack (sold)

1. Publish it (Factory unticked).
2. Upload it to the store as a digital product. Add a page with audio demos, a short video,
   the preset list and the size.
3. The customer pays and gets the download link by email and on their account page.
4. In OSP: **Library › Packs… › Install pack…** and choose the file. OSP shows the name,
   version, creator, contents and licence, and installs only when they agree. It checks every
   file first: a damaged or tampered pack is refused whole, and nothing in a pack ever runs.
5. **Updates:** publish a higher version with the same id and replace the file in the store.
   Owners download it again and install it over the old one; their edits stay. Removing a pack
   (Packs… › Remove) can be undone from the Trash.

**Deliver the `.osppack` itself, not a zip around it.** It already is one checked file. Safari
unpacks `.zip` downloads but leaves `.osppack` alone. If a PDF guide is wanted, link it from the
product page.

### Sizes

- **Core:** 50–100 presets. Aim for at most about 500 MB of audio, because it is in every
  installer download. Sounds shared by several presets are stored once.
- **Expansions:** sized per instrument; a few hundred MB is typical.
- The format takes WAV, AIFF and FLAC. FLAC is lossless and roughly halves the download. To
  use it, convert the recordings before they go into the Library.

## 3. Copy protection and licence

- **A plain download for 1.0.** Most preset and sample-pack sellers work this way. Anyone with
  the file can install it. The licence text in the pack is shown before installing.
- **Keep the store's download limits on:** links that expire and a limited number of
  downloads.
- **Later, if needed:** pack licences tied to an account, through the phase 3 entitlement
  service. The pack format does not need to change for this: a signed licence file beside the
  pack would do. This is listed with copy protection as post-launch.

## 4. Selling more packs

**Inside OSP (phase 1, with the content):**
- **Tasters in Core.** Put one or two presets from each expansion in the Core pack, named and
  tagged with their expansion ("Dusty Keys: Tape Wurli"). People find the style they like in
  the sounds they already have.
- Every pack has a clear **name, creator and description**, and its presets carry
  categories. The Library already lists pack content under **Packs**.

**On the website:**
- A **page per pack** with audio demos made only with OSP, a 30–60 second video, the preset
  list and the size.
- A **free mini pack** (5–10 presets) in exchange for an email address. It fills the list that
  sells the paid packs.
- **Bundles:** "All expansions" and "Core + 3" at a clear discount. Seasonal offers, including
  Black Friday.
- **Updates are free** for owners. Announce them by email: every update is a reason to open
  OSP again.
- A steady release rhythm (for example one pack a month for the first half year) beats a large
  catalogue at launch.

**Phase 2, "More sounds" in OSP:**
- A JSON list on the website (`packs.json`). Each entry has the id, name, latest version,
  price, a cover image, a demo MP3 and the product URL.
- OSP shows the list in the Packs sheet:
  - **Installed** and **Update available** (the list's version against the installed pack's);
  - **Not installed**, with a demo and a **Buy on evenant.com** button that opens the browser.
- It reads one public file: no account, no identifiers, no payment code. It reads it on a
  background thread, never the audio thread, fails silently offline, and can be turned off in
  the Library settings.

**Phase 3 (only if phase 2 earns it): in-app delivery.**
- Sign in to the Evenant account (device-code sign-in in the browser).
- An entitlement service lists the packs the account owns, with signed download links.
- OSP downloads, checks and installs them, and keeps them up to date.
- Third-party sound designers can already make packs with Publish. A marketplace with revenue
  share could follow.

## 5. Options compared

| | Build effort | Risk | Reaches new buyers | Reaches owners while they play | Works offline |
|---|---|---|---|---|---|
| `.osppack` from the website (phase 1) | none (done) | low | yes | no (email only) | yes |
| + "More sounds" catalogue (phase 2) | about 1 week | low | yes | yes | yes (the list is skipped) |
| Full in-app store (phase 3) | weeks, plus a backend | medium–high | no (owners only) | yes | partly |
| Zip of loose WAVs and presets | none | medium (manual install, support) | yes | no | yes |

## 6. What exists now (OSP default branch)

- **The pack format** (`docs/library/packs.md`): a checked install, updates that keep the
  user's edits, removal, and refusal of hostile archives.
- **Factory packs** install at startup from the bundle into `<Library>/Factory`.
- **Publishing:**
  - `PackExport::factory` (default off; every earlier export is unchanged);
  - the Library's **Publish as pack…** sheet, which remembers the fields per collection.
  - Tests: `[unit][library][packs]` ("a publisher's factory export installs from the plugin's
    folder…") and `[plugin][library-packs]` ("Library publishing…").
- **The build step:** `apps/plugin/CMakeLists.txt` with `cmake/CopyFactoryPacks.cmake` puts
  every pack in `OSP_FACTORY_PACKS_DIR` into each bundle. It does nothing when the folder has
  no packs.

**Not built yet:**
- **Drop to install:** dragging a `.osppack` onto OSP, or double-clicking it, to install
  (small; phase 1).
- **The "More sounds" catalogue** (phase 2).
- **A Windows OSP installer.**

## 7. Owner decisions

- The Core list (50–100 presets) and the first expansions. Their rights must be clear: own
  recordings, or licensed for redistribution inside a product.
- Pack ids, prices and bundles.
- The store's delivery settings (link expiry, download count).
- Whether to start phase 2 after launch.
