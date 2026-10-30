"""Purpose: Mark the virtual CAN tools as an importable Python package.

Implementation is separated into protocol (wire bytes), scenario (validated input),
engine (clocked response planning/dispatch), transport (simulation endpoint guards)
and __main__ (CLI). Importing this package alone starts no bus or simulation."""
