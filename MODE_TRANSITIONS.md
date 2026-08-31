# GATT / Mesh mode transitions

The extension is additive. Existing FC40/FC41 UUIDs, characteristic formats,
and legacy opcodes `0x00` through `0x0D` are unchanged. When no valid mode
journal exists, boot behavior is inferred exactly as before from the persisted
provisioning request/result.

## GATT control framing

Requests use the existing FC41 control framing:

```text
[opcode][payload_length][payload]
```

Responses use:

```text
[opcode | 0x80][status][payload_length][payload]
```

New opcodes:

- `0x10 ENTER_MESH`: payload is a 32-bit little-endian `transition_id`.
  Accepted only for a provisioned node. The response echoes the ID, then the
  node stores `MESH_OPERATIONAL` and resets.
- `0x11 GET_MODE_STATUS`: empty payload. Response payload is
  `[version, actual, requested, transition, provision_result, last_error,
  crash_count, reserved, transition_id_le32]`.
- `0x12 GET_CAPABILITIES`: empty payload. Response payload is
  `[version, capability_flags_le32, max_control_frame]`.

Clients must probe `GET_CAPABILITIES`. A timeout or an unknown-command result
means legacy firmware and keeps the existing manual power-cycle workflow.

## Provision then configure

1. Connect to the existing GATT service and use the existing provisioning
   command `0x0A`.
2. Provision over PB-GATT (`0x1827`) as today.
3. Once provisioning has been committed, the firmware stores
   `MESH_OPERATIONAL` and performs `NVIC_SystemReset()` automatically.
4. Scan for Mesh Proxy (`0x1828`), connect, and actively verify the node before
   configuration. Disappearance of the old advertisement is only a hint.
5. Configure AppKey, bindings, subscriptions, and publications as today.
6. On the Proxy disconnect, the firmware commits the configuration image and
   performs one controlled reset. The client waits for a valid `0x1828`
   advertisement before continuing.

## Mesh runtime persistence

Configuration and high-frequency runtime state do not use the same write
path:

- Config Server changes rotate and verify the complete 3660-byte Mesh image.
- Runtime RPL/SEQ/model changes are coalesced in RAM and appended every 15
  minutes as 8-byte deltas in the dedicated page below the mode journal.
- Each delta group has per-record CRCs and a final transaction footer. A torn
  group is ignored completely after reboot.
- `PalNvmRead()` overlays committed deltas on the selected base image before
  the ST library sees it.
- When the delta page is full, the effective image is atomically consolidated
  through the existing double-page rotation, then the delta page is erased.
- Factory unprovisioning erases both the Mesh image and its runtime journal.

Vendor commands never request a controlled reset. The ST library already
reserves outgoing sequence numbers in blocks; the journal primarily prevents
RPL persistence from rotating the complete image for every received message.

## Mesh to GATT maintenance

The existing Vendor Data Control command (`0x0E`) gains two subcommands:

- `0x27 ENTER_GATT_MAINTENANCE`: data is
  `[0x27, transition_id_le32]`. Send the Vendor command with the response bit
  set. Status data is `[0x27, status, transition_id_le32]`.
- `0x28 GET_MODE_STATUS`: data is `[0x28]`. Status data is
  `[0x28, version, actual, requested, transition, provision_result,
  last_error, crash_count, transition_id_le32]`.

After the acknowledged request, the node stores `GATT_MAINTENANCE`, waits for
the access response to be queued, and resets. The client treats an FC40
connection followed by a matching `GET_MODE_STATUS` as success even if the
Mesh acknowledgement was lost.

To return to Mesh, issue GATT `ENTER_MESH` (`0x10`) with a new transition ID.
Mesh keys, sequence state, RPL, and IV state are not erased during maintenance.

## Recovery

- Three consecutive IWDG/fault resets in the same mode force a non-destructive
  `GATT_RECOVERY` boot. A healthy uptime of 60 seconds clears the counter.
- A requested operational Mesh boot whose Mesh NVM is unprovisioned falls back
  to `GATT_RECOVERY`; credentials are never erased automatically.
- Fault context is kept in an `UNINIT` RAM region and reported on the next boot.
- The IWDG timeout is approximately 32 seconds and is refreshed only after the
  sequencer returns or reaches idle.
