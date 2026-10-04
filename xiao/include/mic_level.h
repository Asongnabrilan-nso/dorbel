#pragma once

#include <Arduino.h>

// Subsystem 2 - onboard PDM microphone live level meter. Prints the RMS of
// each 256-sample block over serial. Needs no microSD card.
void setupMicLevel();
void loopMicLevel();

// Installs the PDM RX driver on I2S port 0 (16 kHz, 16-bit mono).
bool initMic();

// Blocks until count samples are read; returns the number actually read.
size_t micRead(int16_t *samples, size_t count);
