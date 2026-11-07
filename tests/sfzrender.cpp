// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// sfzrender.cpp – Märmel Testwerkzeug
// Spielt ein SFZ-Instrument nach einer Ereignisliste ab und schreibt eine WAV-Datei
// (32-Bit-Float, Stereo). Dient zum Anhören der Engine ohne JUCE.
//
// Aufruf: sfzrender instrument.sfz ereignisse.txt ausgabe.wav [samplerate]
//
// Ereignisliste (Zeit in Sekunden, # = Kommentar):
//   set attack|decay|release <Sekunden>
//   set velattack / veldecay <Sekunden>      Zeit bei Velocity 127 (nach attack/decay angeben)
//   set sustaintime <Sekunden, mit Vorzeichen> / set envkey <0...1>
//   set sustain <0...1>
//   set bendrange <Halbtöne>
//   set velrange <dB>                        Dynamikbereich der Velocity (Standard 84)
//   set ampkeyscale <-1...1>   (Amp Pan entfiel am 07.10.2026)
//   set wheel_amp <0...1>     Wheel LFO der Amp-Sektion (LFO-Volume und -Pan)
//   set mode legato|mono|<Stimmenzahl>
//   set glide <Sekunden>
//   set start <0...1> / set startvel <-1...1> / set startrnd <0|1>
//   set coarse <Halbtöne> / set keyshift <Halbtöne> / set fine <Cent> / set crush <0...100> / set chorus 0|1
//   set filtertype 0..4 (aus, LP12, LP24, HP, BP) / set cutoff <0...1> / set reso <0...1> / set drive <0...1>
//   set filterkey <0...1> / set filterenv <-1...1> / set filtervel <0...1> (Vel Env) / set filtervelcut <0...1> (Vel direkt)
//   set plfo_dc / mlfo_dc 0|1                 LFO auf DC (konstant +1)
//   set fattack / fdecay / frelease <Sekunden> / set fsustain <0...1>
//   set plfo_rate / mlfo_rate <Hz>, plfo_wave / mlfo_wave 0..3 (Tri, Sin, Saw, Sqr),
//   set plfo_invert / mlfo_invert 0|1, plfo_env / mlfo_env 0|1, plfo_delay / mlfo_delay <Sekunden>
//   set lfo_pitch / lfo_filter / lfo_volume / lfo_pan <-1...1>, set wheel_pitch / wheel_filter <0...1>
//   <zeit> wheel <0...127>                   Modulationsrad
//   <zeit> at <wert>                       Aftertouch auf allen Kanälen (wirkt wie das Modulationsrad)
//   <zeit> cat <kanal 1...16> <wert>        Kanal-Aftertouch (wirkt nur auf Noten dieses Kanals)
//   <zeit> pat <note> <wert>               polyphoner Aftertouch (wirkt nur auf diese Taste)
//   <zeit> con <note> <velocity> <kanal 1...16>   Note an auf einem bestimmten Kanal
//   <zeit> ampkey <-1...1> / filterkey <0...1>  Keytracking-Regler während des Spielens ändern
//   <zeit> lofi <0...100>                  Anteil des gewählten Effekts (Lo-Fi bzw. Chorus) während des Spielens ändern
//   <zeit> fx lofi|chorus                  Effekt umschalten (wie der Umschalter unter dem Regler); set fx … am Anfang
//   <zeit> cc <nummer> <wert>                Controller (64 schaltet auch das Sustain-Pedal)
//   <zeit> load <datei.sfz>                  Instrument wechseln (Pfad relativ zum Aufruf)
//   <zeit> note <note> <velocity> <dauer>    Note mit Dauer
//   <zeit> on <note> <velocity>
//   <zeit> off <note>
//   <zeit> pedal down|up
//   <zeit> bend <0...16383>

#include "../Source/engine/Synth.h"
#include "../Source/engine/ExsImport.h"

#include <algorithm>
#include <cstdio>
#if defined (__SSE__) || defined (_M_X64)
 #include <xmmintrin.h>
#endif
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace maermel;

namespace
{
struct Event
{
    double time = 0.0;
    enum Type { On, Off, PedalDown, PedalUp, Bend, Load, Wheel, Controller, Aftertouch, AmpKey, FilterKey, LoFi, Fx,
                ChanPressure, PolyPressure } type = On;
    int a = 0, b = 0;
    std::string file;
    int c = 0;                 // Kanal bei „con“
};

bool writeFloatWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int sr)
{
    std::ofstream f (path, std::ios::binary);
    if (! f)
        return false;

    auto u32 = [&] (uint32_t v) { f.write (reinterpret_cast<const char*> (&v), 4); };
    auto u16 = [&] (uint16_t v) { f.write (reinterpret_cast<const char*> (&v), 2); };

    const uint32_t frames = (uint32_t) l.size();
    const uint32_t dataBytes = frames * 2 * 4;
    f.write ("RIFF", 4); u32 (36 + dataBytes); f.write ("WAVE", 4);
    f.write ("fmt ", 4); u32 (16); u16 (3); u16 (2); u32 ((uint32_t) sr); u32 ((uint32_t) sr * 8); u16 (8); u16 (32);
    f.write ("data", 4); u32 (dataBytes);
    for (uint32_t i = 0; i < frames; ++i)
    {
        f.write (reinterpret_cast<const char*> (&l[i]), 4);
        f.write (reinterpret_cast<const char*> (&r[i]), 4);
    }
    return (bool) f;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 4)
    {
        std::fprintf (stderr, "Aufruf: sfzrender instrument.sfz ereignisse.txt ausgabe.wav [samplerate]\n");
        return 1;
    }
    const int sr = (argc > 4) ? std::atoi (argv[4]) : 48000;

   #if defined (__SSE__) || defined (_M_X64)
    // Winzige Zahlen (Denormals) auf null setzen, wie JUCEs ScopedNoDenormals im Plugin
    _mm_setcsr (_mm_getcsr() | 0x8040);
   #endif

    std::shared_ptr<const Instrument> inst = loadAnyInstrument (argv[1]);
    std::printf ("%s\n", inst->summary().c_str());
    for (const auto& w : inst->warnings)
        std::printf ("  ! %s\n", w.c_str());

    Synth synth;
    Envelope::Params env;
    std::vector<Event> events;
    float bendRange = 2.0f;
    float velRange = 84.0f, ampKeyScale = 0.0f;
    Synth::VoiceMode mode = Synth::VoiceMode::Poly;
    int polyVoices = 32;
    float glide = 0.0f;
    float start = 0.0f, startVel = 0.0f, fine = 0.0f, crush = 0.0f, crushBitsDirect = 0.0f, crushRateDirect = 0.0f;
    bool chorusMode = false;
    bool startRnd = false;
    int filterTypeValue = 0;
    float cutoff = 1.0f, reso = 0.0f, drive = 0.0f, filterKey = 0.0f, filterEnv = 0.0f, filterVel = 0.0f, filterVelCut = 0.0f;
    Envelope::Params fenv;
    LfoParams plfo, mlfo;
    float lfoPitch = 0, lfoFilter = 0, lfoVolume = 0, lfoPan = 0, wheelPitchAmt = 0, wheelFilterAmt = 0, wheelAmpAmt = 0;
    int coarse = 0, keyShiftValue = 0;

    std::ifstream in (argv[2]);
    std::string line;
    while (std::getline (in, line))
    {
        line = line.substr (0, line.find ('#'));
        std::istringstream ss (line);
        std::string first;
        if (! (ss >> first))
            continue;

        if (first == "set")
        {
            std::string name, text;
            ss >> name >> text;
            const float value = (float) std::atof (text.c_str());
            if (name == "mode")
            {
                if (text == "legato")    mode = Synth::VoiceMode::Legato;
                else if (text == "mono") mode = Synth::VoiceMode::Mono;
                else { mode = Synth::VoiceMode::Poly; polyVoices = std::atoi (text.c_str()); }
                continue;
            }
            if      (name == "attack")    env.setAttack (value);
            else if (name == "decay")     env.setDecay (value);
            else if (name == "velattack") env.attackSeconds = value;      // Zeit bei Velocity 127 (nach "attack" setzen)
            else if (name == "veldecay")  env.decaySeconds = value;
            else if (name == "sustaintime") env.sustainTimeSeconds = value;
            else if (name == "envkey")    env.keyTrack = value;
            else if (name == "sustain")   env.sustainLevel = value;
            else if (name == "release")   env.releaseSeconds = value;
            else if (name == "bendrange") bendRange = value;
            else if (name == "velrange")  velRange = value;
            else if (name == "ampkeyscale") ampKeyScale = value;
            else if (name == "glide")     glide = value;
            else if (name == "start")     start = value;
            else if (name == "startvel")  startVel = value;
            else if (name == "startrnd")  startRnd = value > 0.5f;
            else if (name == "coarse")    coarse = (int) value;
            else if (name == "keyshift")  keyShiftValue = (int) value;
            else if (name == "fine")      fine = value;
            else if (name == "crush")     crush = value;
            else if (name == "chorus")    chorusMode = value > 0.5f;   // 1 = Regler steuert den Chorus statt Lo-Fi
            else if (name == "crushbits") crushBitsDirect = value;   // nur für Tests: direkt statt über den Knopf
            else if (name == "crushrate") crushRateDirect = value;
            else if (name == "filtertype") filterTypeValue = (int) value;
            else if (name == "cutoff")    cutoff = value;
            else if (name == "reso")      reso = value;
            else if (name == "drive")     drive = value;
            else if (name == "filterkey") filterKey = value;
            else if (name == "filterenv") filterEnv = value;
            else if (name == "filtervel") filterVel = value;
            else if (name == "filtervelcut") filterVelCut = value;
            else if (name == "plfo_dc")     plfo.dc = value > 0.5f;
            else if (name == "mlfo_dc")     mlfo.dc = value > 0.5f;
            else if (name == "fattack")   fenv.setAttack (value);
            else if (name == "fdecay")    fenv.setDecay (value);
            else if (name == "fsustaintime") fenv.sustainTimeSeconds = value;
            else if (name == "fenvkey")   fenv.keyTrack = value;
            else if (name == "fsustain")  fenv.sustainLevel = value;
            else if (name == "frelease")  fenv.releaseSeconds = value;
            else if (name == "plfo_rate")   plfo.rateHz = value;
            else if (name == "mlfo_rate")   mlfo.rateHz = value;
            else if (name == "plfo_wave")   plfo.wave = (LfoWave) (int) value;
            else if (name == "mlfo_wave")   mlfo.wave = (LfoWave) (int) value;
            else if (name == "plfo_invert") plfo.invert = value > 0.5f;
            else if (name == "mlfo_invert") mlfo.invert = value > 0.5f;
            else if (name == "plfo_env")    plfo.envMode = value > 0.5f;
            else if (name == "mlfo_env")    mlfo.envMode = value > 0.5f;
            else if (name == "plfo_delay")  plfo.delaySeconds = value;
            else if (name == "mlfo_delay")  mlfo.delaySeconds = value;
            else if (name == "lfo_pitch")   lfoPitch = value;
            else if (name == "lfo_filter")  lfoFilter = value;
            else if (name == "lfo_volume")  lfoVolume = value;
            else if (name == "lfo_pan")     lfoPan = value;
            else if (name == "wheel_pitch") wheelPitchAmt = value;
            else if (name == "wheel_filter") wheelFilterAmt = value;
            else if (name == "wheel_amp")    wheelAmpAmt = value;
            continue;
        }

        const double t = std::atof (first.c_str());
        std::string cmd;
        ss >> cmd;
        if (cmd == "note")
        {
            int n, v; double dur;
            ss >> n >> v >> dur;
            events.push_back ({ t, Event::On, n, v, {} });
            events.push_back ({ t + dur, Event::Off, n, 0, {} });
        }
        else if (cmd == "on")    { Event e { t, Event::On, 0, 0, {} };  ss >> e.a >> e.b; events.push_back (e); }
        else if (cmd == "off")   { Event e { t, Event::Off, 0, 0, {} }; ss >> e.a;        events.push_back (e); }
        else if (cmd == "pedal") { std::string s; ss >> s; events.push_back ({ t, s == "down" ? Event::PedalDown : Event::PedalUp, 0, 0, {} }); }
        else if (cmd == "bend")  { Event e { t, Event::Bend, 0, 0, {} }; ss >> e.a;       events.push_back (e); }
        else if (cmd == "cc")    { Event e { t, Event::Controller, 0, 0, {} }; ss >> e.a >> e.b; events.push_back (e); }
        else if (cmd == "wheel") { Event e { t, Event::Wheel, 0, 0, {} }; ss >> e.a;       events.push_back (e); }
        else if (cmd == "at")    { Event e { t, Event::Aftertouch, 0, 0, {} }; ss >> e.a;  events.push_back (e); }
        else if (cmd == "cat")   { Event e { t, Event::ChanPressure, 0, 0, {} }; ss >> e.a >> e.b; events.push_back (e); }
        else if (cmd == "pat")   { Event e { t, Event::PolyPressure, 0, 0, {} }; ss >> e.a >> e.b; events.push_back (e); }
        else if (cmd == "con")   { Event e { t, Event::On, 0, 0, {} };  ss >> e.a >> e.b >> e.c; e.c = std::max (0, e.c - 1); events.push_back (e); }
        else if (cmd == "fx")    { std::string m; ss >> m; events.push_back ({ t, Event::Fx, m == "chorus" ? 1 : 0, 0, {} }); }
        else if (cmd == "lofi")  { double value = 0.0; ss >> value; events.push_back ({ t, Event::LoFi, (int) std::lround (value * 1000.0), 0, {} }); }
        else if (cmd == "ampkey" || cmd == "filterkey")   // Regler während des Spielens ändern (Wert × 1000)
        {
            double value = 0.0; ss >> value;
            events.push_back ({ t, cmd == "ampkey" ? Event::AmpKey : Event::FilterKey, (int) std::lround (value * 1000.0), 0, {} });
        }
        else if (cmd == "load")  { Event e { t, Event::Load, 0, 0, {} }; ss >> e.file;    events.push_back (e); }
    }
    std::stable_sort (events.begin(), events.end(), [] (const Event& x, const Event& y) { return x.time < y.time; });

    synth.prepare (sr);
    synth.setEnvelope (env);
    synth.setPitchBendRange (bendRange);
    synth.setVelocityRange (velRange);
    synth.setAmpKeyScale (ampKeyScale);
    synth.setVoiceMode (mode, polyVoices);
    synth.setGlideTime (glide);
    synth.setSampleStart (start, startVel, startRnd);
    synth.setCoarseTune (coarse);
    synth.setKeyShift (keyShiftValue);
    synth.setFineTune (fine);
    synth.setCrush (crush);
    synth.setFxMode (chorusMode ? Synth::FxMode::Chorus : Synth::FxMode::LoFi);
    if (crushBitsDirect > 0.0f || crushRateDirect > 0.0f)
        synth.setCrushDirect (crushBitsDirect, crushRateDirect);
    synth.setFilterType ((FilterType) filterTypeValue);
    synth.setFilterCutoff (cutoff);
    synth.setFilterResonance (reso);
    synth.setDrive (drive);
    synth.setFilterKeyTrack (filterKey);
    synth.setFilterEnvAmount (filterEnv);
    synth.setFilterVelocity (filterVel);
    synth.setFilterVelocityCutoff (filterVelCut);
    synth.setFilterEnvelope (fenv);
    synth.setPolyLfo (plfo);
    synth.setMonoLfo (mlfo);
    synth.setLfoRouting (lfoPitch, lfoFilter, lfoVolume, lfoPan);
    synth.setWheelAmounts (wheelPitchAmt, wheelFilterAmt, wheelAmpAmt);
    synth.setInstrument (inst);

    std::vector<std::shared_ptr<const Instrument>> retired;   // im Plugin übernimmt das der Message-Thread
    std::vector<float> outL, outR;
    const int block = 256;
    std::vector<float> bl (block), br (block);
    int64_t pos = 0;
    size_t next = 0;
    int maxVoices = 0;
    const int64_t tailLimit = (int64_t) (12.0 * sr);
    int64_t lastEventFrame = events.empty() ? 0 : (int64_t) (events.back().time * sr);

    while (true)
    {
        // Ereignisse sample-genau ausführen
        while (next < events.size() && (int64_t) (events[next].time * sr) <= pos)
        {
            const Event& e = events[next++];
            switch (e.type)
            {
                case Event::On:        synth.noteOn (e.a, e.b, e.c); break;
                case Event::Off:       synth.noteOff (e.a); break;
                case Event::PedalDown: synth.sustainPedal (true); break;
                case Event::PedalUp:   synth.sustainPedal (false); break;
                case Event::Bend:      synth.pitchBend (e.a); break;
                case Event::Wheel:     synth.modWheel (e.a); break;
                case Event::Aftertouch: synth.aftertouch (e.a); break;
                case Event::ChanPressure: synth.channelPressure (e.a - 1, e.b); break;
                case Event::PolyPressure: synth.polyPressure (e.a, e.b); break;
                case Event::AmpKey:    synth.setAmpKeyScale ((float) e.a / 1000.0f); break;
                case Event::FilterKey: synth.setFilterKeyTrack ((float) e.a / 1000.0f); break;
                case Event::LoFi:      synth.setCrush ((float) e.a / 1000.0f); break;
                case Event::Fx:        synth.setFxMode (e.a == 1 ? Synth::FxMode::Chorus : Synth::FxMode::LoFi); break;
                case Event::Controller:
                    synth.controller (e.a, e.b);
                    if (e.a == 64) synth.sustainPedal (e.b >= 64);
                    if (e.a == 1)  synth.modWheel (e.b);
                    break;
                case Event::Load:
                {
                    std::shared_ptr<const Instrument> nextInstrument = loadInstrument (e.file);
                    std::printf ("  Wechsel zu %s: %s\n", e.file.c_str(), nextInstrument->summary().c_str());
                    retired.push_back (synth.exchangeInstrument (nextInstrument));
                    break;
                }
            }
        }

        int n = block;
        if (next < events.size())
            n = (int) std::min<int64_t> (block, std::max<int64_t> (1, (int64_t) (events[next].time * sr) - pos));

        std::fill (bl.begin(), bl.begin() + n, 0.0f);
        std::fill (br.begin(), br.begin() + n, 0.0f);
        synth.render (bl.data(), br.data(), n);
        if (auto finished = synth.takeFinishedInstrument())
            retired.push_back (finished);
        outL.insert (outL.end(), bl.begin(), bl.begin() + n);
        outR.insert (outR.end(), br.begin(), br.begin() + n);
        pos += n;
        maxVoices = std::max (maxVoices, synth.activeVoiceCount());

        if (next >= events.size() && (synth.activeVoiceCount() == 0 || pos > lastEventFrame + tailLimit))
            break;
    }

    float peak = 0.0f;
    for (size_t i = 0; i < outL.size(); ++i)
        peak = std::max ({ peak, std::fabs (outL[i]), std::fabs (outR[i]) });

    if (! writeFloatWav (argv[3], outL, outR, sr))
    {
        std::fprintf (stderr, "Konnte %s nicht schreiben\n", argv[3]);
        return 1;
    }
    std::printf ("-> %s: %.2f s, Spitze %.2f dBFS, max. %d Stimmen gleichzeitig\n", argv[3],
                 (double) outL.size() / sr, 20.0 * std::log10 (std::max (peak, 1.0e-9f)), maxVoices);
    return 0;
}
