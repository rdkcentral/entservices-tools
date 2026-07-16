# RDK EntServices Tools - Product Functionality

## Product Overview

EntServices Tools provides controlled utility capabilities through Thunder, with current focus on remote key event simulation for automation, testing, and platform tooling.

## Current Functional Surface

### Key Event Generation

The implemented API receives a JSON payload describing one or more key sequences and enqueues them for asynchronous dispatch into Linux input.

For COMRPC compatibility, `keys` can be provided as a string that contains the JSON array of key entries.

Supported value types today:

- `keyCode`: Linux input key code list
- `modifiers`: per-key modifier list (`ctrl`, `alt`, `shift`)
- `delay`: seconds before dispatch
- `duration` (optional): seconds between key down and key up

Example JSONRPC payload (COMRPC-friendly):

```json
{
  "jsonrpc": "2.0",
  "id": 42,
  "method": "org.rdk.Tools.generateKey",
  "params": {
    "keys": "[{\"keyCode\":[28],\"modifiers\":[[]],\"delay\":0,\"duration\":0}]"
  }
}
```

Equivalent key entry content:

```json
{
  "keys": [
    {
      "keyCode": [28],
      "modifiers": [["ctrl"]],
      "delay": 0.1,
      "duration": 0.05
    }
  ]
}
```

## Validation Rules

- Input must resolve to a non-empty keys array.
- Supported inputs:
  - JSON object with `keys` array
  - JSON object with `keys` string containing array JSON
  - Raw array JSON string
- Each entry must contain `keyCode`, `modifiers`, and `delay`.
- `keyCode` and `modifiers` arrays must have equal lengths.
- Allowed modifiers are only `ctrl`, `alt`, and `shift`.
- `delay` and `duration` must be non-negative.
- `keyCode` values must be discrete numeric values in Linux key range.

Invalid input is rejected before queueing.

## Runtime Behavior

- Events are queued and processed in order.
- A worker thread performs key dispatch.
- Modifier keys are pressed before the main key and released after it.
- Plugin deinitialization terminates worker activity and releases resources.

## Intended Use Cases

- Automated end-to-end test input simulation
- Tool-assisted navigation flows
- Validation of input handling without physical remote interaction

## Non-Goals (Current Version)

- No public API surface for generic shell command execution
- No current UI-facing feature set beyond key event generation
- No persistent user preference model in current implementation