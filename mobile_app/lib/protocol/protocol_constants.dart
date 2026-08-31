// Protocol constants mirroring firmware/protocol/include/protocol.h
// and firmware/protocol/include/protocol_packets.h.
//
// All numeric values must remain identical to the firmware definitions.

/// Two-byte little-endian magic: "MS" (0x4D, 0x53).
const int kPacketMagic = 0x4D53;

const int kProtocolVersionMajor = 0;
const int kProtocolVersionMinor = 3;

const int kPacketHeaderSize = 8; // sizeof(picomso_packet_header_t)
const int kDataBlockSize = 64;   // PICOMSO_DATA_BLOCK_SIZE

// ---------------------------------------------------------------------------
// Message types (picomso_msg_type_t)
// ---------------------------------------------------------------------------

const int kMsgGetInfo = 0x01;
const int kMsgGetCapabilities = 0x02;
const int kMsgGetStatus = 0x03;
const int kMsgSetMode = 0x04;
const int kMsgRequestCapture = 0x05;
const int kMsgReadDataBlock = 0x06;

const int kMsgAck = 0x80;
const int kMsgError = 0x81;
const int kMsgDataBlock = 0x82;

// ---------------------------------------------------------------------------
// Status codes (picomso_status_t)
// ---------------------------------------------------------------------------

const int kStatusOk = 0x00;
const int kStatusErrUnknown = 0x01;
const int kStatusErrBadMagic = 0x02;
const int kStatusErrBadLen = 0x03;
const int kStatusErrBadMode = 0x04;
const int kStatusErrVersion = 0x05;

// ---------------------------------------------------------------------------
// Stream masks (PICOMSO_STREAM_*)
// ---------------------------------------------------------------------------

const int kStreamNone = 0x00;
const int kStreamLogic = 0x01;
const int kStreamScope = 0x02;
const int kStreamBoth = kStreamLogic | kStreamScope;

// ---------------------------------------------------------------------------
// Stream IDs (picomso_stream_id_t) used in DATA_BLOCK
// ---------------------------------------------------------------------------

const int kStreamIdLogic = 1;
const int kStreamIdScope = 2;

// ---------------------------------------------------------------------------
// DATA_BLOCK flags
// ---------------------------------------------------------------------------

const int kFlagLogicFinalized = 1 << 0;
const int kFlagScopeFinalized = 1 << 1;
const int kFlagTerminal = 1 << 2;

// ---------------------------------------------------------------------------
// Trigger match values (picomso_trigger_match_t)
// ---------------------------------------------------------------------------

const int kTriggerMatchLevelLow = 0x00;
const int kTriggerMatchLevelHigh = 0x01;
const int kTriggerMatchEdgeLow = 0x02;
const int kTriggerMatchEdgeHigh = 0x03;

// ---------------------------------------------------------------------------
// Capture state
// ---------------------------------------------------------------------------

const int kCaptureIdle = 0x00;
const int kCaptureRunning = 0x01;

// ---------------------------------------------------------------------------
// Maximum number of trigger slots in REQUEST_CAPTURE
// ---------------------------------------------------------------------------

const int kMaxTriggerCount = 4;

// ---------------------------------------------------------------------------
// Firmware info
// ---------------------------------------------------------------------------

const int kFwIdMaxLen = 32;
