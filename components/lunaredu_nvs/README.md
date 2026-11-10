# NVS initialization

LunarOS settings, remote-control binding and calibration share this lower-level
ESP-IDF component. Initialization is serialized and reports errors without
erasing the default NVS partition. Products retain ownership of namespace keys
and data formats. Partition recovery must be explicit; missing or incompatible
data in one namespace never authorizes deletion of another namespace.

Regression: `tools/repo/test-lunaros.ps1` exercises initialization failures,
non-destructive retries and bounded settings reads.
