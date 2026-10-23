"""Purpose: Package the full synthetic dashboard execution and saved playback tools.

The runner connects production C CAN/UI/loggers to python-can virtual responses
and fake peripherals; __main__ loads user inputs and optionally opens results.
Importing the package does not start the C subprocess or access hardware."""
