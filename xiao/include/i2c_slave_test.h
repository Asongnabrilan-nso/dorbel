#pragma once

// Subsystem 4 - XIAO as I2C slave (0x08) to the UNO Q master. Logs every
// command received and answers status reads with the STATUS_* flags.
void setupI2cSlaveTest();
void loopI2cSlaveTest();
