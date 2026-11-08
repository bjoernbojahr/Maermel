# Contributing to Märmel

Thank you for your interest in Märmel! Märmel is a **one-person project**, developed in my spare time.
Feedback, bug reports and ideas are very welcome – but please be patient: answers may take a while,
and not every idea will make it into Märmel (see "What Märmel is – and what it is not" below).


## How you can help

- **Report bugs** – please use the bug report template when you open an issue. The more details
  (system, DAW, version, steps), the faster I can find the problem.
- **Test SFZ instruments** – if an SFZ instrument plays differently in Märmel than in other players,
  tell me which instrument it is and where to get it (only freely available instruments, please).
- **Suggest improvements** – open an issue and describe what you would like to do and why.
- **Code** – please open an issue **before** you start working on a larger change, so we can discuss
  whether it fits Märmel. Small fixes (typos, obvious bugs) are welcome as pull requests directly.


## What Märmel is – and what it is not

Märmel is deliberately simple: a fast, stage-ready player for SFZ instruments with a fixed set of
controls, where every control does the same thing for every instrument. The SFZ file defines the
mapping; the sound is shaped on the user interface.

Märmel will therefore **not** become a modular sampler with a modulation matrix, unlimited LFOs or
envelopes, scripting, encrypted or proprietary formats. There are great projects for that already.


## Code guidelines

- **C++17**, readable and well commented – a simple loop is preferred over a clever one-liner.
- **No additional third-party libraries.** The sound engine (`Source/engine/`) must stay free of JUCE.
- **Real-time rules:** no memory allocation, no file access and no waiting on locks in the audio thread.
- **Tests:** all programs in `tests/` must still pass (see `BUILDING.md`, section 5). New engine
  features should come with a small test.
- **Sound:** changes that alter how existing instruments sound must be mentioned in the pull request.
  `tests/sfzrender` helps to compare renders before and after a change.
- **Parameter IDs** in `Source/ParameterIds.h` must never change (DAW automation depends on them);
  run `python3 tests/paramidtest.py` after adding a parameter.


## Licensing of contributions

Märmel is licensed under the AGPL-3.0-or-later with additional terms (see `LICENSE` and `NOTICE.md`).
By submitting a contribution (code, documentation, sounds or other material) to this project, you confirm that:

1. you created the contribution yourself, or otherwise have the right to submit it under these terms;
2. you license your contribution under the AGPL-3.0-or-later, like the rest of Märmel; and
3. you additionally grant Björn Bojahr a perpetual, worldwide, non-exclusive, royalty-free and
   irrevocable license to use, modify and distribute your contribution under other license terms as
   well, including in other projects.

You keep the copyright of your contribution. Point 3 keeps the code of Märmel in one hand, so it can be
reused in other projects or offered under additional licenses later without having to contact every
contributor.

Please include this sentence in your pull request:

> I agree to the contribution terms in CONTRIBUTING.md.


## Contact

Issues and pull requests on GitHub, or via http://www.bjoernbojahr.de/
