# Security policy

Märmel reads files that may come from anywhere: SFZ and EXS files, WAV and AIFF samples, presets
(`.sfzp`) and DAW projects. A file that makes Märmel crash, hang or read outside of its memory can
also crash the DAW – and in the worst case be abused. Reports about such problems are very welcome.


## Supported versions

Only the **latest release** receives fixes. Please check whether the problem still occurs with the
newest version before reporting it.


## How to report a security problem

Please **do not open a public issue** for security problems. Instead, use GitHub's private reporting:

1. Open the **Security** tab of this repository.
2. Click **Report a vulnerability**.

The report is only visible to me. If you cannot use GitHub, please contact me via
http://www.bjoernbojahr.de/

Helpful information:

- Märmel version, operating system, DAW and plugin format (AU / VST3 / LV2)
- what happens (crash, hang, wrong memory access, …)
- the smallest file that triggers the problem, if possible (zipped)
- for crashes: the crash log of the DAW or the operating system


## What happens next

Märmel is a one-person project, developed in my spare time, so please be patient. I will confirm
your report as soon as I can, fix the problem in a new release and mention it in the release notes.
If you like, I will credit you there by name.


## What is not a security problem

- An instrument that sounds wrong or does not load correctly – please open a normal bug report.
- Very large instruments that use a lot of memory – Märmel loads all samples into RAM by design
  and warns before the memory runs out.
