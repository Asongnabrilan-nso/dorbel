#pragma once

// Subsystem 2 - onboard PDM microphone live level meter. Prints the RMS of
// each 256-sample block over serial. Needs no microSD card.
void setupMicLevel();
void loopMicLevel();
