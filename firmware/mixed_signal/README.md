# firmware/mixed_signal

Logic and scope capture backends for the current PicoMSO firmware path.

## Current contents

- `logic_capture.c` / `logic_capture.h`
- `scope_capture.c` / `scope_capture.h`

## Current behavior

- finite capture requests only
- finalized-capture storage plus fixed-size block readout
- logic, scope, or combined logic+scope stream selection through the protocol
- logic trigger configuration carried by `REQUEST_CAPTURE`
- current v1.0 scope support covers 1-channel and 2-channel analog capture
- analog depth is documented as 40 ksamples per enabled analog channel
- 1 analog channel uses 12-bit capture; 2 analog channels use round-robin
  8-bit internal capture with readout in the existing 16-bit scope stream
  format

`READ_DATA_BLOCK` serves stored data after capture completion; it does not stream
live acquisition data.
