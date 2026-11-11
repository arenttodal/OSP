#include "ProgressStore.h"

#include <set>

namespace andor::learn
{

namespace
{
    juce::CriticalSection& storeLock()
    {
        static juce::CriticalSection lock;
        return lock;
    }
    std::set<juce::String>& invitationsShown()
    {
        static std::set<juce::String> shown;
        return shown;
    }
    juce::String safeName (const juce::String& product)
    {
        auto name = product.retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789-_");
        return name.isEmpty() ? juce::String ("product") : name;
    }
} // namespace

ProgressStore::ProgressStore (juce::String productId) : product (std::move (productId)) {}

juce::File ProgressStore::directory()
{
    const auto overrideDir = juce::SystemStats::getEnvironmentVariable ("ANDOR_LEARN_DIR", {});
    if (overrideDir.isNotEmpty() && juce::File::isAbsolutePath (overrideDir))
        return juce::File (overrideDir);
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
#if JUCE_MAC
    base = base.getChildFile ("Application Support");
#endif
    return base.getChildFile ("ANDOR").getChildFile ("Learn");
}

juce::File ProgressStore::file() const
{
    return directory().getChildFile (safeName (product) + ".json");
}

juce::var ProgressStore::readVar (bool& corrupt) const
{
    corrupt = false;
    const auto f = file();
    if (! f.existsAsFile())
        return {};
    const auto text = f.loadFileAsString();
    juce::var parsed;
    if (text.isEmpty() || juce::JSON::parse (text, parsed).failed() || ! parsed.isObject())
    {
        corrupt = true;
        return {};
    }
    return parsed;
}

ProgressData ProgressStore::fromJson (const juce::var& v, const juce::String& productId)
{
    ProgressData d;
    d.product = productId;
    if (! v.isObject())
        return d;
    // A file of another product (a copied file) or a schema from the future that changed
    // meaning is read as far as it is understood; fields are optional.
    d.firstInviteSeen = static_cast<bool> (v.getProperty ("firstInviteSeen", false));
    d.learnModeEnabled = static_cast<bool> (v.getProperty ("learnModeEnabled", false));
    if (const auto* lessons = v.getProperty ("lessons", {}).getDynamicObject())
        for (const auto& entry : lessons->getProperties())
        {
            const auto& l = entry.value;
            if (! l.isObject())
                continue;
            LessonProgress p;
            p.revision = static_cast<int> (l.getProperty ("revision", 0));
            p.status = l.getProperty ("status", {}).toString();
            p.completedAt = l.getProperty ("completedAt", {}).toString();
            p.lastStep = l.getProperty ("lastStep", {}).toString();
            if (p.status.isNotEmpty())
                d.lessons[entry.name.toString()] = p;
        }
    return d;
}

juce::String ProgressStore::toJson (const ProgressData& d, const juce::var& keep)
{
    // Unknown top-level fields (a later version's) are kept as they were.
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    if (const auto* old = keep.getDynamicObject())
        for (const auto& p : old->getProperties())
            root->setProperty (p.name, p.value);
    root->setProperty ("schema", ProgressData::schemaVersion);
    root->setProperty ("product", d.product);
    root->setProperty ("firstInviteSeen", d.firstInviteSeen);
    root->setProperty ("learnModeEnabled", d.learnModeEnabled);
    juce::DynamicObject::Ptr lessons = new juce::DynamicObject();
    for (const auto& [id, p] : d.lessons)
    {
        juce::DynamicObject::Ptr l = new juce::DynamicObject();
        l->setProperty ("revision", p.revision);
        l->setProperty ("status", p.status);
        if (p.completedAt.isNotEmpty())
            l->setProperty ("completedAt", p.completedAt);
        if (p.lastStep.isNotEmpty())
            l->setProperty ("lastStep", p.lastStep);
        lessons->setProperty (id, juce::var (l.get()));
    }
    root->setProperty ("lessons", juce::var (lessons.get()));
    return juce::JSON::toString (juce::var (root.get()));
}

ProgressData ProgressStore::read() const
{
    const juce::ScopedLock sl (storeLock());
    bool corrupt = false;
    return fromJson (readVar (corrupt), product);
}

bool ProgressStore::update (const std::function<void (ProgressData&)>& change)
{
    const juce::ScopedLock sl (storeLock());
    bool corrupt = false;
    const auto old = readVar (corrupt);
    auto data = fromJson (old, product);
    change (data);
    data.product = product;
    const auto f = file();
    if (! f.getParentDirectory().createDirectory())
        return false;
    if (corrupt)
        f.copyFileTo (f.withFileExtension ("corrupt"));
    juce::TemporaryFile temp (f);
    if (! temp.getFile().replaceWithText (toJson (data, old)))
        return false;
    return temp.overwriteTargetFileWithTemporary();
}

bool ProgressStore::reset()
{
    return update ([] (ProgressData& d) {
        d.lessons.clear();
        d.firstInviteSeen = false;
        d.learnModeEnabled = false;
    });
}

bool ProgressStore::claimInvitation (const juce::String& productId)
{
    const juce::ScopedLock sl (storeLock());
    return invitationsShown().insert (productId).second;
}

void ProgressStore::releaseInvitation (const juce::String& productId)
{
    const juce::ScopedLock sl (storeLock());
    invitationsShown().erase (productId);
}

} // namespace andor::learn
