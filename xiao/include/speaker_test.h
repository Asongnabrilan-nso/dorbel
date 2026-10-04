#pragma once

// Subsystem 1 - MAX98357A I2S amplifier + 8 ohm speaker bring-up test.
// setupSpeakerTest() initializes I2S; loopSpeakerTest() plays a two-tone
// "ding-dong" chime every few seconds so the wiring can be verified by ear.
void setupSpeakerTest();
void loopSpeakerTest();

// Installs the I2S driver on port 1 for the MAX98357A. Returns false on failure.
bool initSpeaker();

// Plays a sine tone of the given frequency (Hz) for durationMs (blocking).
void playTone(float frequency, int durationMs);

// The doorbell "ding-dong" (~0.9 s, blocking).
void playChime();

// Writes 16 kHz mono samples to the amplifier (blocks while the DMA is full).
void speakerWrite(const int16_t *samples, size_t count);
