# Märmel

<img width="1032" height="505" alt="Image" src="https://github.com/user-attachments/assets/c4e4c3a6-0b7d-49eb-8e89-03f09a1a9fce" />

Märmel is a compact, RAM-based, stage-ready software sampler for instruments in SFZ format. Its user interface is simple, with only a few controls, yet it offers a great-sounding filter with drive and a set of well-designed modulation options that you can reach quickly – even during a live performance – to shape the sound with just a few clicks. And if you're into chip music or classic video game sounds: with KeyShift and Lo-Fi, you can bend even the highest-quality samples in that direction.

Instruments are selected from a clear list that mirrors a freely configurable folder structure on your hard drive, which makes organizing your instruments very easy.

Märmel uses little CPU, is written entirely in native C++, has a scalable user interface and runs on macOS 10.11 and Windows 10 and later. The binaries are signed and notarized. A Linux version (VST3 and LV2) is now available as well.

Of course, Märmel respects your privacy: it does not collect any personal data, and there are no "call home" functions, no installation manager or anything similar. It loads your SFZ instruments and turns them into a playable instrument within seconds. That's all.

Included is a small factory library with short samples that show off the sampler's sound and modulation capabilities. Unzip the sounds into any folder, open Märmel in your DAW and select this folder with the PATH button. Märmel updates its instrument list, and you're ready to play.

For me, this is a dream come true. I wrote the first routines more than 20 years ago in THINK Pascal, and over the years I kept moving the project forward with various versions of Visual C++, Xcode and JUCE. I gave up on the project several times because it was too big to handle on my own. Only with the help of Claude (Anthropic), who wrote most of the new code, was it finally completed - and implemented better than I could have done on my own. And that's why you can try it out now, too.


Technical overview of the source code (in German): see `inhalt.md`.


## Feedback and contributing

Märmel is a one-person project, developed in my spare time. Bug reports, feedback and ideas are very welcome – please use the issue templates on GitHub, and be patient: answers may take a while. Before contributing code, please read `CONTRIBUTING.md`.

How to build Märmel and run the tests: see `BUILDING.md`.


## License

Märmel – SFZ Sample Player
Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/

Licensed under the GNU Affero General Public License, version 3 or later (AGPL-3.0-or-later),
with additional terms under section 7 (attribution and name) – see `NOTICE.md` and `LICENSE`.

EXS24 and Logic are trademarks of Apple Inc. Märmel is not affiliated with Apple.

The factory sounds are licensed under CC BY-SA 4.0. This license applies only to redistributing the sample files themselves. As the licensor, I additionally grant everyone permission to use the sounds – with or without Märmel – to create their own music, songs and commercial audio productions without any restrictions. Such works are not subject to CC BY-SA 4.0.

