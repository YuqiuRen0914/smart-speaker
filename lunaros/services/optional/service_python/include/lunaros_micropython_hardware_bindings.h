#pragma once

// Register the optional LunarEdu hardware facade for the MicroPython VM.
// Call this before setup_micropython_runtime(). This is a no-op unless
// CONFIG_LUNAREDU_MICROPYTHON_ENABLE_HARDWARE_BINDINGS=y.
void setup_micropython_hardware_bindings();
