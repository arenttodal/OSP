#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace osp::plugin
{

/** One result of the Library window: a catalog record (or a built-in starting state), with what
    its row and detail show. Built on the Library thread, then read on the message thread. */
struct LibraryRow
{
    std::string id;                    ///< catalog id, or "program:<n>" for a built-in starting state
    library::AssetType type = library::AssetType::sound;
    library::Origin origin = library::Origin::user;
    juce::String name, category, notes;
    int rating = 0;
    bool favourite = false, trashed = false;
    std::int64_t created = 0, lastUsed = 0;
    juce::StringArray tags;
    // Sounds
    std::string contentHash;
    double seconds = 0.0, sampleRate = 0.0;
    int channels = 0, bitDepth = 0;
    std::int64_t bytes = 0;
    juce::String format;
    std::optional<double> rootMidi;
    // Presets and templates
    juce::File file;
    int layers = 0;
    int program = -1;
    bool isBuiltIn() const noexcept { return program >= 0; }
};

/** A sound's details for the inspector (asked for when it is selected). */
struct LibrarySoundDetail
{
    std::string id;
    juce::StringArray places;          ///< where its bytes were seen (newest first)
    juce::String resolvedWhere;        ///< "store", "original", ... or empty when missing
    std::vector<library::Collection> collections;
    int uses = 0;
};

/**
    The Library window (Stage 5, docs/library/mockups.md panels 2-5 and 10): one overlay over
    the instrument's upper part - the keyboard stays below it and plays the audition. Three
    views share one frame: Presets (complete instruments), Templates (settings without
    sounds) and Sounds (with the sound inspector and the A/B/C audition tray). Sheets inside
    it: Save, Library settings (storage, preview level), Missing sounds (relink), and the
    small prompts (a name, a destination).

    Everything it shows comes from the catalog through LibraryService::request (never on the
    message thread); previews go through Audition; loads through the processor (recoverable).
    Message thread only.
*/
class LibraryPanel final : public juce::Component,
                           public juce::DragAndDropContainer,
                           private juce::Timer
{
public:
    enum class View { presets, templates, sounds };

    explicit LibraryPanel (OspAudioProcessor& processor);
    ~LibraryPanel() override;

    void setView (View view);
    View currentView() const noexcept { return view; }

    /** The window asks to close (its close button, Escape). */
    std::function<void()> onClose;
    /** A load or save changed the patch: the instrument refreshes. */
    std::function<void()> onPatchChanged;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    // For tests and the window's own controls ------------------------------
    /** Asks the catalog again (search, filters, scope). */
    void refresh();
    /** Waits for the catalog and the preview, delivering their answers (tests). */
    bool settle (int milliseconds = 10000);
    int rowCount() const noexcept { return static_cast<int> (rows.size()); }
    const LibraryRow& row (int index) const { return rows[static_cast<std::size_t> (index)]; }
    int selectedRow() const;
    void selectRow (int index);
    void setSearchText (const juce::String& text);
    /** The scope keys the sidebar offers ("all", "factory", "user", "favourites", "recent",
        "trash", "collection:<id>", "category:<name>") and their labels with counts. */
    struct SideItem
    {
        juce::String key, label;
        int count = -1;          ///< -1: a section header
    };
    const std::vector<SideItem>& sidebarItems() const noexcept { return side; }
    void selectScope (const juce::String& key);
    juce::String scope() const { return scopeKey; }
    /** The selected row's main action: load a preset or template, load a sound into the
        first empty layer (asks which when none is). */
    void loadSelected();
    void loadSelectedInto (int layer);
    void previewSelected();
    /** Puts the selected sound into a tray slot (0..2). */
    void addSelectedToTray (int slot);
    void commitTray();
    void openSaveSheet();
    void openSettingsSheet();
    void openMissingSheet();
    /** The sheet showing now (empty: none) - "save", "settings", "missing", "prompt", "choice". */
    juce::String openSheetName() const;
    juce::Component* sheet() const noexcept { return sheetComponent.get(); }
    void closeSheet();
    juce::String statusText() const { return status; }

    /** A sound dragged from the results: its asset id. */
    static juce::var dragDescription (const std::string& assetId) { return "osp-sound:" + juce::String (assetId); }
    static std::string assetFromDrag (const juce::var& description);

    // The window's parts and sheets use these.
    void setStatus (const juce::String& text, bool offerBack = false);
    /** A catalog change on the Library thread, then a fresh query (`done`: the status line). */
    void edit (std::function<bool (library::Catalog&)> change, const juce::String& done = {});
    void showSheet (std::unique_ptr<juce::Component> component, const juce::String& name);
    void prompt (const juce::String& title, const juce::String& initial, std::function<void (const juce::String&)> done);
    void choose (const juce::String& message, juce::StringArray options, std::function<void (int)> done);

    class Sidebar;
    class Results;
    class Detail;
    class Inspector;
    class Tray;
    class Sheet;

private:
    friend class Sidebar;
    friend class Results;
    friend class Detail;
    friend class Inspector;
    friend class Tray;
    friend class Sheet;

    void timerCallback() override;
    void query();
    void selectionChanged();
    void requestDetail();
    void showRowMenu (int index, juce::Point<int> where);
    void loadRow (const LibraryRow& row);
    void loadSound (const std::string& assetId, int layer);
    void renameRow (const LibraryRow& row);
    void trashRow (const LibraryRow& row);
    void restoreRow (const LibraryRow& row);
    void setFavourite (const LibraryRow& row, bool favourite);
    void indexPresetFiles();
    juce::StringArray categoriesFor (View) const;
    library::AssetType typeFor (View) const;

    OspAudioProcessor& processor;
    View view = View::presets;
    juce::String scopeKey = "all";
    std::vector<LibraryRow> rows;
    std::vector<SideItem> side;
    std::optional<LibrarySoundDetail> detail;
    std::uint64_t queryGeneration = 0, appliedGeneration = 0;
    juce::String status;
    bool statusOffersBack = false;

    std::array<juce::TextButton, 3> tabs;
    juce::TextButton closeButton { juce::String::fromUTF8 ("\xc3\x97") }, menuButton { juce::String::fromUTF8 ("\xe2\x8b\xaf") };
    juce::TextButton saveButton { juce::String::fromUTF8 ("Save\xe2\x80\xa6") };
    juce::TextButton backButton { "Back to previous patch" };
    juce::TextEditor search;
    juce::ComboBox originFilter, lengthFilter, sortOrder;
    std::unique_ptr<Sidebar> sidebar;
    std::unique_ptr<Results> results;
    std::unique_ptr<Detail> detailStrip;
    std::unique_ptr<Inspector> inspector;
    std::unique_ptr<Tray> tray;
    std::unique_ptr<juce::Component> sheetComponent, retiredSheet;
    juce::String sheetName;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> headerArea, footerArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibraryPanel)
};

/** Preset and template artwork (decision D-09): no images, a quiet gradient made from the
    asset's identity, so each one looks the same every time. */
void drawLibraryArtwork (juce::Graphics&, juce::Rectangle<float> area, const std::string& identity, float radius);

} // namespace osp::plugin
