#pragma once
#include "RocketBuzzer.h"

// Named melodies for the buzzer. `static` = internal linkage so this header can
// be included wherever without multiple-definition errors. Add new songs here
// and they become selectable by the (future) server "play a song" mode.
namespace RocketMelodies {
using namespace RocketNote;

// Hedwig's Theme (Harry Potter) - opening phrase.
static const uint16_t hedwig_f[] = {
  B4, E5,  G5, Fs5, E5,  B5,  A5,  Fs5, E5,  G5, Fs5, Ds5, F5,  B4 };
static const uint16_t hedwig_d[] = {
  300, 450, 150, 300, 600, 300, 900, 900, 450, 150, 300, 600, 300, 900 };
static const Melody HEDWIG = { hedwig_f, hedwig_d, 14, "hedwig" };

// "Close Encounters" 5-note motif + rising resolve.
static const uint16_t ce_f[] = { D4,  E4,  C4,  C3,  G4,  REST, C5,  E5,  G5 };
static const uint16_t ce_d[] = { 400, 400, 400, 400, 700, 200,  200, 200, 600 };
static const Melody CLOSE_ENCOUNTERS = { ce_f, ce_d, 9, "close_encounters" };

// Classic alarm/timer siren: two tones alternating (rising-falling wail).
static const uint16_t siren_f[] = { A5, E5, A5, E5, A5, E5, A5, E5 };
static const uint16_t siren_d[] = { 350, 350, 350, 350, 350, 350, 350, 350 };
static const Melody SIREN = { siren_f, siren_d, 8, "siren" };

}  // namespace RocketMelodies
