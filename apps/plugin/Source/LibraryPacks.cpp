// The Library window's Stage 10 actions: packs (install, list, remove, export a collection)
// and the Library backup (export, restore). The work runs on the Library's background thread;
// every step that changes something asks first.

#include "LibraryCapture.h"
#include "LibraryPlaces.h"

namespace osp::plugin
{

namespace
{
    namespace colour = design::colour;

    juce::String bytesOf (std::int64_t bytes)
    {
        if (bytes < 1024 * 1024)
            return juce::String (static_cast<double> (bytes) / 1024.0, 0) + " KB";
        if (bytes < 1024LL * 1024 * 1024)
            return juce::String (static_cast<double> (bytes) / (1024.0 * 1024.0), 1) + " MB";
        return juce::String (static_cast<double> (bytes) / (1024.0 * 1024.0 * 1024.0), 2) + " GB";
    }

    std::filesystem::path pathOf (const juce::File& f) { return std::filesystem::path (f.getFullPathName().toStdString()); }

    juce::String counts (const library::PackResult& r)
    {
        juce::StringArray parts;
        if (r.sounds > 0)
            parts.add (juce::String (r.sounds) + (r.sounds == 1 ? " sound" : " sounds"));
        if (r.presets > 0)
            parts.add (juce::String (r.presets) + (r.presets == 1 ? " preset" : " presets"));
        if (r.templates > 0)
            parts.add (juce::String (r.templates) + (r.templates == 1 ? " template" : " templates"));
        return parts.joinIntoString (", ");
    }

    /** A pack id from a name, unique to this user ("user.warm-keys.1a2b3c4d"). */
    std::string packIdFor (const juce::String& name)
    {
        std::string slug;
        for (const auto c : name.toLowerCase().toStdString())
            if (std::isalnum (static_cast<unsigned char> (c)))
                slug += c;
            else if (! slug.empty() && slug.back() != '-')
                slug += '-';
        while (! slug.empty() && slug.back() == '-')
            slug.pop_back();
        if (slug.empty())
            slug = "pack";
        return "user." + slug.substr (0, 60) + "." + juce::Uuid().toString().substring (0, 8).toStdString();
    }
}

//==============================================================================
// Packs

void LibraryPanel::installPack (const juce::File& file)
{
    const auto places = packPlaces();
    juce::Component::SafePointer<LibraryPanel> safe (this);
    setStatus (juce::String::fromUTF8 ("Checking ") + file.getFileName() + juce::String::fromUTF8 ("\xe2\x80\xa6"));
    processor.libraryService().requestBackground<library::PackResult> (
        [path = pathOf (file), places] (library::Catalog& c) { return library::inspectPack (c, path, places); },
        [safe, file, places] (library::PackResult look) {
            if (safe == nullptr)
                return;
            if (! look.ok)
            {
                safe->setStatus (juce::String (look.error));
                return;
            }
            const auto& m = look.manifest;
            juce::String message = juce::String (m.name) + " " + juce::String (m.version) + (m.creator.empty() ? juce::String() : " by " + juce::String (m.creator)) + ": "
                                 + counts (look) + ".";
            if (look.upgraded)
                message += " Updates " + juce::String (look.previousVersion) + "; your favourites, ratings and tags on it stay.";
            if (look.alreadyInstalled)
                message += " It is installed already: installing again repairs it.";
            if (! m.license.empty())
                message += " Licence: " + juce::String (m.license).substring (0, 160) + (m.license.size() > 160 ? juce::String::fromUTF8 ("\xe2\x80\xa6") : juce::String());
            safe->choose (message, { look.upgraded ? "Update" : "Install", "Cancel" }, [safe, file, places] (int choice) {
                if (safe == nullptr || choice != 0)
                    return;
                safe->setStatus (juce::String::fromUTF8 ("Installing ") + file.getFileNameWithoutExtension() + juce::String::fromUTF8 ("\xe2\x80\xa6"));
                safe->processor.libraryService().requestBackground<library::PackResult> (
                    [path = pathOf (file), places] (library::Catalog& c) { return library::installPack (c, path, places); },
                    [safe] (library::PackResult done) {
                        if (safe == nullptr)
                            return;
                        safe->setStatus (done.ok ? "Installed " + juce::String (done.manifest.name) + " " + juce::String (done.manifest.version) + ": " + counts (done)
                                                 : juce::String (done.error));
                        safe->refresh();
                    });
            });
        });
}

class LibraryPanel::PacksSheet final : public LibraryPanel::Sheet, private juce::ListBoxModel
{
public:
    explicit PacksSheet (LibraryPanel& p) : Sheet (p, "Packs", { 760, 520 })
    {
        list.setModel (this);
        list.setRowHeight (44);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.setTitle ("Installed packs");
        addAndMakeVisible (list);
        install.setTitle ("Install a pack");
        install.onClick = [this] { choosePack(); };
        remove.setTitle ("Remove the selected pack");
        remove.onClick = [this] { removeSelected(); };
        done.onClick = [this] { panel.closeSheet(); };
        for (auto* c : std::initializer_list<juce::Component*> { &install, &remove, &done })
            addAndMakeVisible (c);
        refreshList();
    }
    void refreshList()
    {
        juce::Component::SafePointer<PacksSheet> safe (this);
        panel.processor.libraryService().request<std::vector<library::Catalog::PackRecord>> ([] (library::Catalog& c) { return c.packs(); },
                                                                                               [safe] (std::vector<library::Catalog::PackRecord> installed) {
                                                                                                   if (safe == nullptr)
                                                                                                       return;
                                                                                                   safe->packs = std::move (installed);
                                                                                                   safe->list.updateContent();
                                                                                                   safe->list.repaint();
                                                                                                   safe->repaint();
                                                                                               });
    }
    void paintContent (juce::Graphics& g) override
    {
        auto r = content();
        g.setColour (colour::textMicro);
        g.setFont (type::popupLabel (12.0f));
        g.drawText ("INSTALLED (SOUNDS AND PRESETS FROM PACKS ARE LISTED UNDER PACKS)", r.getX(), r.getY(), 600, 16, juce::Justification::centredLeft);
        if (packs.empty())
        {
            g.setColour (colour::textMicro);
            g.setFont (type::micro (14.0f));
            g.drawFittedText (juce::String::fromUTF8 ("No packs installed. Install pack\xe2\x80\xa6 adds a .osppack file: its sounds, presets and templates join the Library."),
                              list.getBounds().reduced (20), juce::Justification::centred, 3);
        }
        g.setColour (colour::textMicro);
        g.setFont (type::micro (13.0f));
        g.drawFittedText ("Removing a pack removes its presets and templates (to the computer's Trash) and moves its sounds to the Library's trash. "
                          "Your own sounds and presets are never touched, and presets that use a pack's sound keep working.",
                          juce::Rectangle<int> (r.getX(), list.getBottom() + 8, r.getWidth(), 40), juce::Justification::topLeft, 2);
    }
    void resized() override
    {
        auto r = content().withTrimmedTop (22);
        auto buttons = r.removeFromBottom (38);
        done.setBounds (buttons.removeFromRight (110));
        buttons.removeFromRight (8);
        remove.setBounds (buttons.removeFromRight (150));
        buttons.removeFromRight (8);
        install.setBounds (buttons.removeFromRight (150));
        r.removeFromBottom (56);
        list.setBounds (r);
    }
    juce::ListBox list;

private:
    int getNumRows() override { return static_cast<int> (packs.size()); }
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (row < 0 || row >= getNumRows())
            return;
        const auto& p = packs[static_cast<std::size_t> (row)];
        if (selected)
        {
            g.setColour (colour::panelBottom.darker (0.05f));
            g.fillRoundedRectangle (juce::Rectangle<float> (0.0f, 1.0f, static_cast<float> (width), static_cast<float> (height) - 2.0f), 6.0f);
        }
        g.setColour (colour::text);
        g.setFont (type::micro (15.0f));
        g.drawText (juce::String (p.name) + "  " + juce::String (p.version), 12, 2, width - 24, 22, juce::Justification::centredLeft, true);
        g.setColour (colour::textMicro);
        g.setFont (type::micro (12.5f));
        g.drawText ((p.creator.empty() ? juce::String() : "by " + juce::String (p.creator) + juce::String::fromUTF8 (" \xc2\xb7 ")) + juce::String (p.id)
                        + (juce::String (p.id).startsWith ("osp.factory") ? juce::String::fromUTF8 (" \xc2\xb7 factory") : juce::String()),
                    12, 22, width - 24, 18, juce::Justification::centredLeft, true);
    }
    juce::String getNameForRow (int row) override
    {
        return row >= 0 && row < getNumRows() ? juce::String (packs[static_cast<std::size_t> (row)].name) + " " + juce::String (packs[static_cast<std::size_t> (row)].version)
                                              : juce::String();
    }
    void choosePack()
    {
        chooser = std::make_unique<juce::FileChooser> ("Install a pack", juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads"), "*.osppack");
        juce::Component::SafePointer<LibraryPanel> owner (&panel);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [owner] (const juce::FileChooser& fc) {
            if (owner != nullptr && fc.getResult().existsAsFile())
            {
                owner->closeSheet();
                owner->installPack (fc.getResult());
            }
        });
    }
    void removeSelected()
    {
        const int row = list.getSelectedRow();
        if (row < 0 || row >= getNumRows())
            return;
        const auto pack = packs[static_cast<std::size_t> (row)];
        juce::Component::SafePointer<LibraryPanel> owner (&panel);
        panel.closeSheet();
        owner->choose ("Remove " + juce::String (pack.name) + "? Its presets and templates go to the computer's Trash and its sounds to the Library's trash; "
                       "your own content stays.",
                       { "Remove", "Cancel" }, [owner, id = pack.id] (int choice) {
                           if (owner == nullptr || choice != 0)
                               return;
                           owner->processor.libraryService().requestBackground<library::PackResult> (
                               [id, places = packPlaces()] (library::Catalog& c) { return library::removePack (c, id, places, moveToSystemTrash); },
                               [owner] (library::PackResult removed) {
                                   if (owner == nullptr)
                                       return;
                                   owner->setStatus (removed.ok ? "Removed " + juce::String (removed.manifest.name) : juce::String (removed.error));
                                   owner->refresh();
                               });
                       });
    }

    std::vector<library::Catalog::PackRecord> packs;
    juce::TextButton install { juce::String::fromUTF8 ("Install pack\xe2\x80\xa6") }, remove { "Remove pack" }, done { "Done" };
    std::unique_ptr<juce::FileChooser> chooser;
};

void LibraryPanel::openPacksSheet()
{
    showSheet (std::make_unique<PacksSheet> (*this), "packs");
}

void LibraryPanel::exportCollectionAsPack (const std::string& collectionId, const juce::String& name, const juce::File& out, bool confirmRights)
{
    juce::Component::SafePointer<LibraryPanel> safe (this);
    setStatus (juce::String::fromUTF8 ("Making the pack\xe2\x80\xa6"));
    processor.libraryService().requestBackground<library::PackResult> (
        [collectionId, name, path = pathOf (out), confirmRights] (library::Catalog& c) {
            library::PackExport request;
            library::SearchQuery q;
            q.collection = collectionId;
            q.limit = 100000;
            for (const auto& a : c.search (q))
                request.assetIds.push_back (a.id);
            request.id = packIdFor (name);
            request.name = name.toStdString();
            request.confirmUnknownRights = confirmRights;
            return library::exportPack (c, packPlaces().storeDir, request, path, JucePlugin_VersionString);
        },
        [safe, collectionId, name, out] (library::PackResult done) {
            if (safe == nullptr)
                return;
            if (done.ok)
            {
                safe->setStatus ("Saved the pack " + out.getFileName() + ": " + counts (done)
                                 + (done.notes.empty() ? juce::String() : ". " + juce::String (done.notes.front())));
                return;
            }
            if (juce::String (done.error).contains ("no known rights"))
            {
                safe->choose (juce::String (done.error) + ".", { "I may share them", "Cancel" }, [safe, collectionId, name, out] (int choice) {
                    if (safe != nullptr && choice == 0)
                        safe->exportCollectionAsPack (collectionId, name, out, true);
                });
                return;
            }
            safe->setStatus ("The pack was not made: " + juce::String (done.error));
        });
}

//==============================================================================
// Backup

void LibraryPanel::exportBackup (const juce::File& folder, bool includeExternal)
{
    auto target = folder;
    if (! target.getFileName().endsWithIgnoreCase (".ospbackup"))
        target = target.getSiblingFile (target.getFileName() + ".ospbackup");
    juce::Component::SafePointer<LibraryPanel> safe (this);
    setStatus (juce::String::fromUTF8 ("Backing up the Library\xe2\x80\xa6"));
    processor.libraryService().requestBackground<library::BackupResult> (
        [path = pathOf (target), includeExternal] (library::Catalog& c) {
            library::BackupOptions options;
            options.includeExternal = includeExternal;
            return library::exportBackup (c, backupPlaces(), path, options);
        },
        [safe, target] (library::BackupResult done) {
            if (safe == nullptr)
                return;
            if (! done.ok)
            {
                safe->setStatus ("No backup was made: " + juce::String (done.error));
                return;
            }
            juce::String message = "Backed up " + juce::String (done.records) + " records, " + juce::String (done.sounds) + " sounds (" + bytesOf (done.bytes) + ") to "
                                 + target.getFileName();
            if (! done.notIncluded.empty())
                message += ". Not included: " + juce::String (static_cast<int> (done.notIncluded.size())) + " (" + juce::String (done.notIncluded.front()) + ")";
            safe->setStatus (message);
        });
}

void LibraryPanel::restoreBackup (const juce::File& folder)
{
    juce::Component::SafePointer<LibraryPanel> safe (this);
    processor.libraryService().requestBackground<library::BackupInfo> (
        [path = pathOf (folder)] (library::Catalog&) { return library::inspectBackup (path); },
        [safe, folder] (library::BackupInfo info) {
            if (safe == nullptr)
                return;
            if (! info.ok)
            {
                safe->setStatus (juce::String (info.error));
                return;
            }
            const auto message = "Restore " + folder.getFileName() + " (" + juce::String (info.created).substring (0, 10) + "): " + juce::String (info.records) + " records, "
                               + juce::String (info.sounds) + " sounds, " + juce::String (info.presets) + " presets, " + juce::String (info.templates)
                               + " templates. It adds what is missing; nothing here is lost. When a record is in both:";
            safe->choose (message, { "Keep mine", "Newer wins", "Cancel" }, [safe, folder] (int choice) {
                if (safe == nullptr || choice < 0 || choice > 1)
                    return;
                safe->setStatus (juce::String::fromUTF8 ("Restoring\xe2\x80\xa6"));
                safe->processor.libraryService().requestBackground<library::RestoreReport> (
                    [path = pathOf (folder), newerWins = choice == 1] (library::Catalog& c) {
                        library::RestoreOptions options;
                        options.newerWins = newerWins;
                        options.moveToTrash = moveToSystemTrash;
                        return library::restoreBackup (c, backupPlaces(), path, options);
                    },
                    [safe] (library::RestoreReport done) {
                        if (safe == nullptr)
                            return;
                        safe->processor.reloadFavourites();
                        safe->setStatus (done.ok ? "Restored: " + juce::String (done.added) + " records added, " + juce::String (done.kept) + " kept as they were, "
                                                       + juce::String (done.updated) + " updated; " + juce::String (done.soundsAdded) + (done.soundsAdded == 1 ? " sound and " : " sounds and ")
                                                       + juce::String (done.filesAdded + done.filesReplaced + done.filesBeside)
                                                       + (done.filesAdded + done.filesReplaced + done.filesBeside == 1 ? " file copied" : " files copied")
                                                       + (done.notes.empty() ? juce::String() : ". " + juce::String (done.notes.front()))
                                                 : "Not restored: " + juce::String (done.error));
                        safe->refresh();
                    });
            });
        });
}

} // namespace osp::plugin
