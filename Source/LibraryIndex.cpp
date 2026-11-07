// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// LibraryIndex.cpp – Märmel

#include "LibraryIndex.h"

namespace
{
// Bei Änderungen am Inhalt des Caches erhöhen (2: EXS-Dateien aufgenommen)
constexpr int kCacheVersion = 2;

struct NaturalFileOrder
{
    static int compareElements (const juce::File& a, const juce::File& b)
    {
        return a.getFileName().compareNatural (b.getFileName());   // "2 Bass" vor "10 Bass"
    }
};

bool sameTree (const LibraryIndex::Folder& a, const LibraryIndex::Folder& b)
{
    if (a.directory != b.directory || a.modified != b.modified
        || a.instruments != b.instruments || a.subfolders.size() != b.subfolders.size())
        return false;
    for (size_t i = 0; i < a.subfolders.size(); ++i)
        if (! sameTree (a.subfolders[i], b.subfolders[i]))
            return false;
    return true;
}

std::unique_ptr<juce::XmlElement> toXml (const LibraryIndex::Folder& f)
{
    auto xml = std::make_unique<juce::XmlElement> ("F");
    xml->setAttribute ("p", f.directory.getFullPathName());
    xml->setAttribute ("m", juce::String (f.modified));
    for (const auto& file : f.instruments)
        xml->createNewChildElement ("I")->setAttribute ("n", file.getFileName());
    for (const auto& sub : f.subfolders)
        xml->addChildElement (toXml (sub).release());
    return xml;
}

LibraryIndex::Folder fromXml (const juce::XmlElement& xml)
{
    LibraryIndex::Folder f;
    f.directory = juce::File (xml.getStringAttribute ("p"));
    f.name = f.directory.getFileName();
    f.modified = xml.getStringAttribute ("m").getLargeIntValue();
    for (auto* child : xml.getChildIterator())
    {
        if (child->hasTagName ("I"))
            f.instruments.add (f.directory.getChildFile (child->getStringAttribute ("n")));
        else if (child->hasTagName ("F"))
            f.subfolders.push_back (fromXml (*child));
    }
    return f;
}

void filterInto (const LibraryIndex::Folder& source, const juce::String& text, LibraryIndex::Folder& target)
{
    target.name = source.name;
    target.directory = source.directory;
    target.modified = source.modified;
    for (const auto& file : source.instruments)
        if (file.getFileNameWithoutExtension().containsIgnoreCase (text))
            target.instruments.add (file);
    for (const auto& sub : source.subfolders)
    {
        LibraryIndex::Folder f;
        filterInto (sub, text, f);
        if (f.countInstruments() > 0)
            target.subfolders.push_back (std::move (f));
    }
}

// Kopie ohne EXS-Instrumente; die Ordnerstruktur bleibt vollständig (für das Blättern im Ordner)
void withoutExsInto (const LibraryIndex::Folder& source, LibraryIndex::Folder& target)
{
    target.name = source.name;
    target.directory = source.directory;
    target.modified = source.modified;
    for (const auto& file : source.instruments)
        if (! file.hasFileExtension ("exs"))
            target.instruments.add (file);
    for (const auto& sub : source.subfolders)
    {
        LibraryIndex::Folder f;
        withoutExsInto (sub, f);
        target.subfolders.push_back (std::move (f));
    }
}

void flattenInto (const LibraryIndex::Folder& f, juce::Array<juce::File>& out)
{
    // Menü-Reihenfolge: erst Unterordner, dann Instrumente dieses Ordners
    for (const auto& sub : f.subfolders)
        flattenInto (sub, out);
    out.addArray (f.instruments);
}
} // namespace

// ===========================================================================

int LibraryIndex::Folder::countInstruments() const
{
    int n = instruments.size();
    for (const auto& sub : subfolders)
        n += sub.countInstruments();
    return n;
}

LibraryIndex::LibraryIndex()
    : juce::Thread ("Maermel Library")
{
    // Eigene Datei „Maermel-Global.settings“ (seit 15.10.2026). Vorher hieß sie „Maermel.settings“ – unter Windows
    // derselbe Pfad wie die Einstellungsdatei der JUCE-Standalone-Hülle (%APPDATA%\Maermel\Maermel.settings: JUCE
    // nimmt dort ohne Ordnernamen den Programmnamen als Ordner). Beide schrieben abwechselnd ihren kompletten Stand
    // hinein; die Standalone-Hülle überschrieb beim Beenden Library-Pfad, GUI-Größe usw. (Mac: getrennte Pfade).
    // Die alte Datei wird nicht mehr gelesen (keine Übernahme alter Einträge).
    juce::PropertiesFile::Options options;
    options.applicationName = "Maermel-Global";
    options.filenameSuffix = ".settings";
   #if JUCE_LINUX || JUCE_BSD
    options.folderName = ".config/Maermel";   // JUCE legt unter Linux sonst einen sichtbaren Ordner ~/Maermel an
   #else
    options.folderName = "Maermel";
   #endif
    options.osxLibrarySubFolder = "Application Support";

    settings = std::make_unique<juce::PropertiesFile> (options);

    rootFolder = juce::File (settings->getValue ("libraryFolder"));
    cacheFile = settings->getFile().getSiblingFile ("Maermel-LibraryIndex.xml");

    loadCache();
    startThread (juce::Thread::Priority::low);
    notify();                                   // gleich im Hintergrund abgleichen
}

LibraryIndex::~LibraryIndex()
{
    stopThread (5000);
}

juce::File LibraryIndex::getRootFolder() const
{
    const juce::ScopedLock sl (lock);
    return rootFolder;
}

void LibraryIndex::setRootFolder (const juce::File& folder)
{
    {
        const juce::ScopedLock sl (lock);
        rootFolder = folder;
        snapshot = nullptr;
    }
    settings->setValue ("libraryFolder", folder.getFullPathName());
    settings->saveIfNeeded();
    sendChangeMessage();
    notify();
}

void LibraryIndex::requestRefresh()
{
    notify();
}

std::shared_ptr<const LibraryIndex::Folder> LibraryIndex::getSnapshot() const
{
    const juce::ScopedLock sl (lock);
    return snapshot;
}

bool LibraryIndex::isSampleFolderOf (const juce::File& dir) const
{
    // "<Instrument> Samples" neben "<Instrument>.sfz" (oder .exs/.sfzp) wird übersprungen
    const juce::String name = dir.getFileName();
    if (! name.endsWithIgnoreCase (" Samples"))
        return false;
    const juce::String instrument = name.dropLastCharacters (8);
    const juce::File parent = dir.getParentDirectory();
    return parent.getChildFile (instrument + ".sfz").existsAsFile()
        || parent.getChildFile (instrument + ".exs").existsAsFile()
        || parent.getChildFile (instrument + ".sfzp").existsAsFile();
}

LibraryIndex::Folder LibraryIndex::scanFolder (const juce::File& dir, const Folder* cached)
{
    Folder f;
    f.name = dir.getFileName();
    f.directory = dir;
    f.modified = dir.getLastModificationTime().toMilliseconds();

    juce::Array<juce::File> subdirs;

    if (cached != nullptr && cached->directory == dir && cached->modified == f.modified)
    {
        // Ordner unverändert: Inhalt aus dem Zwischenspeicher, nicht neu auflisten
        f.instruments = cached->instruments;
        for (const auto& c : cached->subfolders)
            subdirs.add (c.directory);
    }
    else
    {
        juce::Array<juce::File> exsFiles;
        for (const auto& child : dir.findChildFiles (juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles, false))
        {
            if (threadShouldExit())
                return f;
            if (child.isDirectory())
            {
                if (! isSampleFolderOf (child))
                    subdirs.add (child);
            }
            else if (child.hasFileExtension ("sfz"))
            {
                f.instruments.add (child);
            }
            else if (child.hasFileExtension ("exs"))
            {
                exsFiles.add (child);
            }
        }
        // EXS-Instrumente nur, wenn es daneben kein gleichnamiges SFZ gibt (sonst doppelt im Menü)
        for (const auto& exs : exsFiles)
            if (! exs.withFileExtension ("sfz").existsAsFile())
                f.instruments.add (exs);
        NaturalFileOrder order;
        f.instruments.sort (order);
        subdirs.sort (order);
    }

    for (const auto& sub : subdirs)
    {
        if (threadShouldExit())
            return f;
        if (! sub.isDirectory())
            continue;   // inzwischen gelöscht

        const Folder* cachedSub = nullptr;
        if (cached != nullptr)
            for (const auto& c : cached->subfolders)
                if (c.directory == sub) { cachedSub = &c; break; }

        f.subfolders.push_back (scanFolder (sub, cachedSub));
    }
    return f;
}

void LibraryIndex::run()
{
    while (! threadShouldExit())
    {
        wait (-1);
        if (threadShouldExit())
            break;

        juce::File root;
        std::shared_ptr<const Folder> previous;
        {
            const juce::ScopedLock sl (lock);
            root = rootFolder;
            previous = snapshot;
        }
        if (! root.isDirectory())
            continue;

        scanning = true;
        Folder fresh = scanFolder (root, (previous && previous->directory == root) ? previous.get() : nullptr);
        scanning = false;

        if (threadShouldExit())
            break;
        if (previous && sameTree (*previous, fresh))
            continue;   // nichts geändert

        auto result = std::make_shared<const Folder> (std::move (fresh));
        {
            const juce::ScopedLock sl (lock);
            if (rootFolder != root)
                continue;   // Library-Ordner wurde währenddessen geändert
            snapshot = result;
        }
        saveCache (*result);
        sendChangeMessage();
    }
}

void LibraryIndex::loadCache()
{
    auto xml = juce::XmlDocument::parse (cacheFile);
    if (xml == nullptr || ! xml->hasTagName ("MaermelLibrary") || xml->getIntAttribute ("version") != kCacheVersion)
        return;   // fehlt oder älteres Format: wird im Hintergrund neu eingelesen
    if (auto* top = xml->getChildByName ("F"))
    {
        auto root = std::make_shared<Folder> (fromXml (*top));
        if (root->directory == rootFolder)
            snapshot = std::move (root);
    }
}

void LibraryIndex::saveCache (const Folder& root)
{
    juce::XmlElement xml ("MaermelLibrary");
    xml.setAttribute ("version", kCacheVersion);
    xml.addChildElement (toXml (root).release());

    juce::TemporaryFile temp (cacheFile);        // erst vollständig schreiben, dann ersetzen
    if (xml.writeTo (temp.getFile()))
        temp.overwriteTargetFileWithTemporary();
}

std::shared_ptr<const LibraryIndex::Folder> LibraryIndex::filtered (const std::shared_ptr<const Folder>& root,
                                                                   const juce::String& text)
{
    if (root == nullptr || text.trim().isEmpty())
        return root;
    auto result = std::make_shared<Folder>();
    filterInto (*root, text.trim(), *result);
    return result;
}

std::shared_ptr<const LibraryIndex::Folder> LibraryIndex::getVisibleSnapshot()
{
    auto root = getSnapshot();
    if (root == nullptr || isShowingExs())
        return root;
    auto result = std::make_shared<Folder>();
    withoutExsInto (*root, *result);
    return result;
}

juce::Array<juce::File> LibraryIndex::flatten (const Folder& root)
{
    juce::Array<juce::File> out;
    flattenInto (root, out);
    return out;
}

const LibraryIndex::Folder* LibraryIndex::findFolder (const Folder& root, const juce::File& directory)
{
    if (root.directory == directory)
        return &root;
    for (const auto& sub : root.subfolders)
        if (directory.isAChildOf (sub.directory) || directory == sub.directory)
            if (auto* found = findFolder (sub, directory))
                return found;
    return nullptr;
}
