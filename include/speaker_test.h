#pragma once

// Subsystem 1 - MAX98357A I2S amplifier + 8 ohm speaker bring-up test.
// setupSpeakerTest() initializes I2S; loopSpeakerTest() plays a two-tone
// "ding-dong" chime every few seconds so the wiring can be verified by ear.
void setupSpeakerTest();
void loopSpeakerTest();

// Plays a sine tone of the given frequency (Hz) for durationMs (blocking).
void playTone(float frequency, int durationMs);
