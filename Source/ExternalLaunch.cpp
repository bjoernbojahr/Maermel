// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// ExternalLaunch.cpp – siehe ExternalLaunch.h

#include "ExternalLaunch.h"

#if JUCE_LINUX || JUCE_BSD
 #include <spawn.h>
 #include <sys/wait.h>
 #include <cstdlib>
 #include <cstring>
 #include <unistd.h>
 #include <string>
 #include <vector>
 extern char** environ;     // Umgebung des Host-Prozesses (Standard-POSIX)
#endif

namespace maermel
{
namespace external
{

#if JUCE_LINUX || JUCE_BSD

namespace
{
    // Variablen, die nur für den Host gedacht sind und ein anderes Programm stören können
    bool isHostOnlyVariable (const char* entry)
    {
        return std::strncmp (entry, "LD_LIBRARY_PATH=", 16) == 0
            || std::strncmp (entry, "LD_PRELOAD=", 11) == 0;
    }


    // Gibt es das Programm? Mit "/" im Namen direkt prüfen, sonst in allen Ordnern von PATH suchen.
    // (Die Shell startet es im Hintergrund – einen Fehler dort könnten wir sonst nicht mehr bemerken.)
    bool programExists (const std::string& program)
    {
        if (program.find ('/') != std::string::npos)
            return access (program.c_str(), X_OK) == 0;

        const char* path = std::getenv ("PATH");
        std::string folders = (path != nullptr) ? path : "/usr/local/bin:/usr/bin:/bin";
        size_t start = 0;
        while (start <= folders.size())
        {
            size_t end = folders.find (':', start);
            if (end == std::string::npos)
                end = folders.size();
            const std::string folder = folders.substr (start, end - start);
            if (! folder.empty() && access ((folder + "/" + program).c_str(), X_OK) == 0)
                return true;
            start = end + 1;
        }
        return false;
    }
}

bool startProgram (const juce::StringArray& args)
{
    if (args.isEmpty() || ! programExists ((*args.begin()).toStdString()))
        return false;

    // Gestartet wird eine kurze Shell, die das eigentliche Programm im Hintergrund startet und sich sofort beendet.
    // So hängt das Programm danach nicht an Märmel bzw. am Host (kein "Zombie"-Prozess, wenn es später endet).
    // "$0" "$@" übergibt Programm und Argumente unverändert – Leerzeichen und Sonderzeichen in Pfaden sind sicher.
    std::vector<std::string> strings;
    strings.push_back ("/bin/sh");
    strings.push_back ("-c");
    strings.push_back ("\"$0\" \"$@\" </dev/null >/dev/null 2>&1 &");
    for (const auto& arg : args)
        strings.push_back (arg.toStdString());

    std::vector<char*> argv;
    for (auto& s : strings)
        argv.push_back (&s[0]);
    argv.push_back (nullptr);

    // Umgebung des Hosts übernehmen, aber ohne LD_LIBRARY_PATH/LD_PRELOAD (siehe ExternalLaunch.h)
    std::vector<char*> envp;
    for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry)
        if (! isHostOnlyVariable (*entry))
            envp.push_back (*entry);
    envp.push_back (nullptr);

    // Alle offenen Dateien des Hosts ab Nummer 3 im neuen Prozess schließen (0–2 = Ein-/Ausgabe bleiben).
    // posix_spawn_file_actions_addclosefrom_np gibt es seit glibc 2.34 – Märmel setzt ohnehin 2.35 voraus.
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init (&actions);
   #if defined (__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34))
    posix_spawn_file_actions_addclosefrom_np (&actions, 3);
   #endif

    pid_t pid = 0;
    const int result = posix_spawn (&pid, "/bin/sh", &actions, nullptr, argv.data(), envp.data());
    posix_spawn_file_actions_destroy (&actions);
    if (result != 0)
        return false;

    // Die Shell beendet sich sofort (das Programm läuft im Hintergrund weiter) – kurz auf sie warten
    int status = 0;
    if (waitpid (pid, &status, 0) != pid)
        return false;
    return WIFEXITED (status) && WEXITSTATUS (status) == 0;
}

bool openFolder (const juce::File& folder)
{
    return folder.isDirectory() && startProgram ({ "xdg-open", folder.getFullPathName() });
}

bool openUrl (const juce::String& url)
{
    return startProgram ({ "xdg-open", url });
}

#else   // macOS, Windows: wie bisher über JUCE

bool openFolder (const juce::File& folder)
{
    // Ein Ordner als "Programm" gestartet öffnet sich im Finder (Mac) bzw. Explorer (Windows)
    return folder.isDirectory() && folder.startAsProcess();
}

bool openUrl (const juce::String& url)
{
    return juce::URL (url).launchInDefaultBrowser();
}

#endif

}
}
