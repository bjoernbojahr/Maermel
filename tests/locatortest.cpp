// Märmel – SFZ Sample Player
// Copyright (C) 2026 Björn Bojahr – http://www.bjoernbojahr.de/
// SPDX-License-Identifier: AGPL-3.0-or-later
// Additional terms under section 7 of the license (attribution, name): see NOTICE.md
//
// locatortest.cpp – Test: Instrument im DAW-Projekt wiederfinden (Fingerabdruck und Auswahl)
// Aufruf: g++ -std=c++17 -I Source/engine -o locatortest tests/locatortest.cpp Source/engine/*.cpp Source/engine/exs/*.cpp && ./locatortest

#include "InstrumentLocator.h"
#include <cstdio>
#include <cassert>
using namespace maermel;
int fails=0;
void check(bool c,const char* what){ printf("%s %s\n", c?"OK  ":"FAIL", what); if(!c) ++fails; }
int main(){
  auto piano = sampleFingerprintFromText("<control>\ndefault_path=Piano BB/\n<region> sample=P_48.wav key=48\n<region> sample=P_52.wav key=52\n<region> sample=P_56.wav key=56\n<region> sample=P_60.wav key=60\n");
  auto pianoEdited = sampleFingerprintFromText("<region> sample=Piano BB\\p_48.WAV key=48 volume=-3\n<region> sample=P_52.wav key=52\n<region> sample=P_56.wav key=56\n<region> sample=P_64.wav key=64\n");
  auto other = sampleFingerprintFromText("<region> sample=Steinway C4.wav key=60\n<region> sample=Steinway C5.wav key=72\n");
  check(piano.size()==4,"4 Samples im Fingerabdruck");
  check(fingerprintFromString(fingerprintToString(piano))==piano,"Text hin und zurueck");
  check(fingerprintOverlap(piano,pianoEdited)==0.75,"bearbeitet: 75 % (Ordner, Gross/klein, Backslash egal)");
  check(fingerprintOverlap(piano,other)==0.0,"fremdes Piano: 0 %");
  auto umlautNFC = sampleFingerprintFromText("<region> sample=Fl\xc3\xb6te.wav\n");
  auto umlautNFD = sampleFingerprintFromText("<region> sample=Flo\xcc\x88te.wav\n");
  check(umlautNFC==umlautNFD,"Umlaut NFC == NFD");
  // Auswahl
  auto c1 = chooseCandidate({"Keys","Fremd"},{pianoEdited,other},"Keys",piano);
  check(c1.index==0 && !c1.ambiguous,"bearbeitete Datei gefunden, fremde verworfen");
  auto c2 = chooseCandidate({"Fremd"},{other},"Keys",piano);
  check(c2.index==-1 && !c2.ambiguous,"nur fremde gleichnamige Datei: nichts laden");
  auto c3 = chooseCandidate({"Gig","Keys"},{piano,piano},"Keys",piano);
  check(c3.index==1,"zwei Kopien: gleicher Unterordner gewinnt");
  auto c4 = chooseCandidate({"Gig A","Gig B"},{piano,piano},"Keys",piano);
  check(c4.index==-1 && c4.ambiguous,"zwei Kopien in fremden Ordnern: mehrdeutig, nichts laden");
  auto c5 = chooseCandidate({"Gig A","Gig B"},{piano,pianoEdited},"Keys",piano);
  check(c5.index==0,"groessere Uebereinstimmung gewinnt");
  auto c6 = chooseCandidate({"Keys","Other"},{other,other},"keys",{});
  check(c6.index==0,"altes Projekt ohne Fingerabdruck: gleicher Ordner (Gross/klein egal)");
  auto c7 = chooseCandidate({"Other"},{piano},"Keys",{});
  check(c7.index==-1,"altes Projekt, anderer Ordner: nichts laden");
  auto c8 = chooseCandidate({"Keys","Keys"},{piano,piano},"Keys",{});
  check(c8.ambiguous,"altes Projekt, zwei gleiche Ordnernamen: mehrdeutig");
  return fails;
}
