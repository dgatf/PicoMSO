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

`READ_DATA_BLOCK` serves stored data after capture completion; it does not stream
live acquisition data.
