# Building Märmel

This document describes how to build Märmel from source on macOS, Windows and Linux,
and how to run the engine tests.

**About the name:** Internally the plugin is called `Maermel` everywhere (ASCII only: project name,
plugin name, file names, bundle, settings folder, class names `MaermelAudioProcessor` /
`MaermelAudioProcessorEditor`, namespace `maermel`). Only the user interface shows "Märmel".
This avoids problems with the "ä" in any operating system, DAW or file system.


## Requirements

| What | Version |
| --- | --- |
| JUCE (with Projucer) | JUCE 9 – used under the GNU AGPL v3 |
| C++ standard | C++17 (no newer language features, no `std::filesystem`) |
| macOS | Xcode 13 or later; deployment target macOS 10.11 |
| Windows | Visual Studio 2022 or later |
| Linux | GCC with `make`; Ubuntu 22.04 or newer recommended (see below) |

No other third-party libraries are needed. The sound engine (`Source/engine`) is plain C++17
without any JUCE dependency; only the plugin, MIDI and user-interface layer uses JUCE.


## Repository layout

| Path | Contents |
| --- | --- |
| `Maermel.jucer` | Projucer project (all settings described below are already stored in it) |
| `Source/` | Plugin, editor and user interface (JUCE) |
| `Source/engine/` | Sound engine: SFZ parser, sample loading, voices, envelopes, filter, LFOs, export (no JUCE) |
| `Source/engine/exs/` | EXS file reader and SFZ writer for the EXS import |
| `Source/engine/chorus/` | Chorus effect (header only, included via `Synth.h`) |
| `Resources/Fonts/` | Barlow Semi Condensed (SIL Open Font License 1.1, see `OFL.txt`) |
| `Resources/Icon/` | Application icon for the standalone version |
| `tests/` | Command-line test programs and test data (not part of the Projucer project) |


## 1. Open the project

1. Open `Maermel.jucer` in the Projucer.
2. Make sure the Projucer knows where your JUCE modules are: **Projucer → Global Paths…**
   (or set the module path per exporter under **Modules**).
3. Choose the exporter for your platform and click **Save and Open in IDE**.

The project already contains:

- Plugin formats: VST3, AU (macOS), LV2 (Linux) and Standalone.
- Plugin characteristics: *Plugin is a Synth* and *Plugin MIDI Input*.
- The two font files as **Binary Resources** (the code finds them via `BinaryData.h`; without them the
  system font is used). The icon is *not* a binary resource; it is only used as the exporter icon.
- Module options: `JUCE_USE_CURL` and `JUCE_WEB_BROWSER` disabled (Märmel uses neither network nor a web view;
  the link in the About window opens the default browser through the operating system), unused audio
  codecs (FLAC, Ogg Vorbis, Opus, MP3, Windows Media) disabled.
- "Plugin Channel Configurations" is left empty on purpose – the code decides which output layouts
  (mono or stereo) are allowed.

### Plugin identity – please read before distributing a modified version

| Setting | Value |
| --- | --- |
| Plugin Manufacturer Code | `BJBO` |
| Plugin Code | `Bjsp` |
| Bundle Identifier | `de.bjoernbojahr.bjsampleplayer` |
| LV2 URI | `https://www.bjoernbojahr.de/plugins/maermel` |

DAWs identify the plugin by these codes; saved projects find Märmel again only as long as they stay the
same. They must therefore never change for the official version.

If you distribute a **modified version**, please give it its own manufacturer code, plugin code,
bundle identifier, LV2 URI and a different name. Otherwise DAWs cannot tell the two plugins apart, and
the additional terms in `NOTICE.md` (attribution, marking of modified versions, name) require it anyway.


## 2. macOS

1. In Xcode, build the scheme **Maermel - Standalone Plugin** first and start it. Choose MIDI input and
   audio output in the settings of the standalone window, then load an SFZ file and play.
2. Build **Maermel - AU** and **Maermel - VST3**. The plugins are copied to
   `~/Library/Audio/Plug-Ins/Components` and `~/Library/Audio/Plug-Ins/VST3`.
3. Validate the Audio Unit in the Terminal:

       auval -v aumu Bjsp BJBO

4. If a DAW does not show the plugin (or shows an old parameter list), run
   `killall -9 AudioComponentRegistrar` and rescan the plugins in the DAW.

**Build date in the About window:** The About window shows the date and time at which `AboutPanel.cpp`
was compiled. Because Xcode only recompiles changed files, the Xcode exporter has this
"Pre-Build Shell Script", so the date is correct after every build:

    touch "$PROJECT_DIR/../../Source/AboutPanel.cpp"

**Signing and notarization** are not part of the project. Official builds are signed with a
"Developer ID Application" certificate and notarized by Apple; for your own builds this is only
needed if you want to pass them on to other Macs.


## 3. Windows

The Visual Studio exporter already contains these settings:

- **Extra Compiler Flags:** `/utf-8` – without it the Microsoft compiler does not read the source files
  as UTF-8, and texts such as "Märmel" or the arrows ◂ ▸ in the display would be wrong.
- **Runtime Library:** "Use static runtime" (`/MT`) in all configurations. The Visual C++ runtime is then
  part of the plugin itself; no "Visual C++ Redistributable" is needed on the target computer.

Formats: VST3 and Standalone (there is no AU on Windows). The VST3 plugin belongs in
`C:\Program Files\Common Files\VST3`. The automatic copy step needs administrator rights; otherwise
copy the file by hand.


## 4. Linux

Packages (Ubuntu/Debian):

    sudo apt install build-essential libasound2-dev libjack-jackd2-dev libfreetype-dev \
        libfontconfig1-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev \
        libxcomposite-dev libgl1-mesa-dev

(`libcurl4-openssl-dev` and WebKit are not needed, because the network and web-view functions are
disabled in the project.)

Build in `Builds/LinuxMakefile`:

    make CONFIG=Release

The Linux exporter uses `JUCE_JACK=1` and the linker flag `-Wl,--as-needed`, so only libraries that are
really used are linked. Formats: VST3 and LV2. Install by copying `Maermel.vst3` to `~/.vst3` and
`Maermel.lv2` to `~/.lv2`.

Checks:

    # Dependencies – only libfontconfig, libfreetype, libstdc++, libm, libgcc_s and libc
    # (plus their own dependencies) should remain. X11 is loaded by JUCE at run time.
    ldd Maermel.vst3/Contents/x86_64-linux/Maermel.so

    # Newest glibc / libstdc++ version required by the build
    objdump -T Maermel.vst3/Contents/x86_64-linux/Maermel.so \
        | grep -o 'GLIBC_[0-9.]*\|GLIBCXX_[0-9.]*' | sort -V | uniq | tail -3

For builds that should run on many distributions, build on the oldest system you want to support
(for example Ubuntu 22.04): glibc and libstdc++ on the target system must be at least as new as on the
build system.


## 5. Engine tests (without JUCE)

The folder `tests/` contains small command-line programs that test the sound engine directly, together
with their test data. They need only a C++17 compiler, no JUCE. Each program is built together with the
engine sources – from the project folder, for example on macOS or Linux:

    g++ -std=c++17 -O2 -Wall -Wextra -ISource/engine -ISource \
        tests/stucktest.cpp Source/engine/*.cpp Source/engine/exs/*.cpp -o stucktest

(On macOS `clang++` works the same way.) `texttest` needs only its own file:

    g++ -std=c++17 -O2 tests/texttest.cpp -o texttest

Run the programs from inside the `tests` folder, so the relative sample paths of the test instruments
are found:

| Program | What it checks | Example (in `tests/`) |
| --- | --- | --- |
| `stucktest` | thousands of random note sequences – no voice may keep hanging | `stucktest layer/layer-test.sfz offby/offby-test.sfz xfade/xfade-test.sfz` |
| `layertest` | voice limit counts notes, layers are never cut in half | `layertest layer/layer-test.sfz` |
| `offbytest` | `group` / `off_by` (e.g. closed hi-hat stops the open one) | `offbytest offby/offby-test.sfz` |
| `texttest` | typing values into the value fields | `texttest` |
| `headertest` | reading root key and loop directly from WAV/AIFF headers, compared with full loading | `headertest doppelchunks/*.wav doppelchunks/*.aif` |
| `veltest` | velocity curves and offsets | `veltest` |
| `locatortest` | finding an instrument of a DAW project on another computer | `locatortest` |
| `exporttest` | "Export instrument…" (rewrite SFZ, collect samples) | `exporttest instrument.sfz targetfolder name` |
| `sfzrender` | plays an SFZ file from an event list and writes a WAV file – to listen to the engine without a DAW | `sfzrender xfade/xfade-test.sfz xfade/ev-pedal.txt out.wav` |
| `sfzinfo` | shows what Märmel reads from an SFZ file | `sfzinfo layer/layer-test.sfz` |

Each program prints its result at the end (in German), for example `Alle Prüfungen bestanden`
("all checks passed"), `0 Fehler` ("0 errors"), `0 Abweichung(en)` ("0 deviations") or – for `stucktest` –
the number of sequences with hanging voices, which must be 0. The event-list format of `sfzrender` is described at the top of `tests/sfzrender.cpp`.

Test data:

- `layer/` – generated sine samples, used by the layer, `off_by` and crossfade tests
  (recreate them with `python3 erzeugen.py` in that folder if needed).
- `offby/`, `xfade/` – test instruments for `group`/`off_by` and for crossfades by velocity, modulation
  wheel and sustain pedal. `xfade/pruefen.py` measures the level of single frequencies in a rendered file,
  e.g. `python3 xfade/pruefen.py out.wav 220,660 0.2,0.7,1.2`.
- `doppelchunks/`, `fuellbyte/`, `umbenannt/` – damaged or unusual files (duplicate chunks, missing pad
  bytes, renamed instrument folder). Run `python3 erzeugen.py` in the folder first; the `LIESMICH.md`
  (German) in each folder explains the expected result.
- `pedalwechsel/` – an event list for a typical piano pedal change; works with any instrument.

Two Python scripts:

- `python3 tests/paramidtest.py` – checks that the parameter numbers stay fixed (DAW automation depends
  on them) and that no number appears twice. Run it after adding or renaming a parameter.
- `python3 tests/rootkeycheck.py "/path/to/library"` – compares the root key in the sample file names,
  the SFZ files and the WAV/AIFF headers of a library (read only, changes nothing).


## 6. Settings and cache

| System | Folder |
| --- | --- |
| macOS | `~/Library/Application Support/Maermel/` |
| Windows | `%APPDATA%\Maermel\` |
| Linux | `~/.config/Maermel/` |

- `Maermel-Global.settings` – library folder, switching instruments via MIDI, velocity curve, editor, GUI size and
  other global settings (shared by all Märmel instances).
- `Maermel-LibraryIndex.xml` – the scanned folder structure of the library. It can be deleted safely; it
  is rebuilt on the next start.

On Windows the standalone version also writes a file `Maermel.settings` into the same folder. It belongs
to the JUCE standalone wrapper (window position, audio device) and is not used by the plugin.


## 7. Platform notes

- **File names with umlauts:** On Windows, all file access of the engine goes through UTF-8 paths
  (`engine/FileUtil.h`, `openForReading`).
- **Two spellings of accented letters:** An "ö" can be stored as one character or as "o" plus a combining
  diaeresis. macOS treats both as equal, Windows and Linux do not. If a file is not found,
  `openForReading` automatically tries the other spelling, so libraries copied from a Mac work on Windows
  and Linux as well. Mixed spellings within one path are not covered.
- **Upper/lower case:** If a sample is still not found, `findIgnoringCase` (`engine/FileUtil.h`) searches
  folder by folder for a name that differs only in case. This matters on Linux and for SFZ files that
  write all paths in lower case. Disabled on Windows, where it is not needed.
