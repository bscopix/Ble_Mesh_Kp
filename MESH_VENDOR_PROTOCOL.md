# TOEM vendor Mesh protocol

This protocol exposes the operational data already available through the
legacy GATT services without changing the GATT framing or the ctor10w UART
protocol.

The model is company `0x0030`, model `0x0001`. The messages below use vendor
command `APPLI_DATA_CNTRL_CMD` (`0x0E`). Multi-byte integers are little-endian.
Requests expecting a reply must use the acknowledged vendor opcode variant.

## Event publications

The publication address and AppKey are configured through Config Model
Publication Set. A unicast publication address is recommended so segmented
SHOT messages benefit from lower-transport acknowledgements.

| Subcommand | Parameters | Behaviour |
| --- | --- | --- |
| `0x13` SHOT_EVENT | `event_seq:u32, shot[13]` | FIFO, one publication per shot |
| `0x14` TARGET_EVENT | `event_seq:u32, target[4]` | Coalesced to the latest state while busy |
| `0x15` RAW_EVENT | reserved | Automatic RAW publication is disabled |
| `0x16` BATTERY_EVENT | `event_seq:u32, level:u8` | Coalesced to the latest level while busy |

`event_seq` starts at zero after a firmware boot. A gap indicates that an
intermediate event was coalesced or missed. SHOT events can be recovered from
the in-memory queue while the same firmware boot remains active.

Events are queued from the ctor10w decoder and sent later by `Vendor_Process`.
No Mesh send is performed from the UART receive path. SHOT has priority over
TARGET and BATTERY. When the Mesh transport is busy, pending events remain
queued and are retried without spinning the sequencer.

## Reads and recovery

| Subcommand | Request parameters | Successful response parameters |
| --- | --- | --- |
| `0x10` SHOT_READ | none | `shot[13]` |
| `0x11` TARGET_READ | none | `target[4]` |
| `0x12` BATTERY_READ | none | `level:u8` |
| `0x17` SHOT_QUEUE_STATUS | none | `count:u16, oldest_seq:u32, next_seq:u32` |
| `0x18` SHOT_QUEUE_READ | `sequence:u32` | `sequence:u32, shot[13]` |
| `0x19` RAW_READ | none | `raw[20]` |
| `0x1A` HW_REV_READ | none | UTF-8/ASCII hardware revision, no terminator |
| `0x1B` FW_VER_READ | none | UTF-8/ASCII firmware revision, no terminator |
| `0x1C` MODEL_READ | none | UTF-8/ASCII model, no terminator |
| `0x1D` MANUFACTURER_READ | none | UTF-8/ASCII manufacturer, no terminator |

The first byte of every response remains the requested subcommand, as for the
existing data-control replies.

The shot queue is a 1024-entry RAM ring. Valid sequences are in
`[oldest_seq, next_seq)`. The bridge should read queue status, then request each
missing sequence. If a requested sequence has already been overwritten, the
firmware returns an error status and the bridge must restart at `oldest_seq`.
The queue intentionally does not survive a firmware reset in this phase.

## Compatibility and operating constraints

- Existing GATT characteristics, notifications and commands are unchanged.
- Existing Mesh subcommands `0x20` through `0x28` are unchanged.
- Events are accepted only in provisioned `MESH_OPERATIONAL` mode.
- RAW stays available by polling but is not published automatically, avoiding
  continuous three-segment traffic on a populated network.
- OTA continues through `ENTER_GATT_MAINTENANCE` (`0x27`) and the existing GATT
  OTA services.
- `BLEMesh_SetVendorCbMap` is not installed: this firmware uses the registered
  `MODEL_Vendor_cb_t` path, whose process callback already dispatches to
  `Vendor_WriteLocalDataCb`, `Vendor_ReadLocalDataCb`, and
  `Vendor_OnResponseDataCb`.
