# RDK EntServices Tools - Product Functionality

## Product Overview

EntServices Tools provides controlled utility capabilities through Thunder, with current focus on remote key event simulation for automation, testing, and platform tooling.

## Current Functional Surface

### Key Event Generation

The implemented API receives a JSON payload describing one or more key sequences and enqueues them for asynchronous dispatch into Linux input.

For COMRPC compatibility, `keys` can be provided as a string that contains the JSON array of key entries.

Supported value types today:

- `keyCode`: single Linux input key code (integer)
- `modifiers`: modifier list (`ctrl`, `alt`, `shift`)
- `delay`: seconds before dispatch
- `duration` (optional): seconds between key down and key up

Example JSONRPC payload (COMRPC-friendly):

```json
{
  "jsonrpc": "2.0",
  "id": 42,
  "method": "org.rdk.Tools.generateKey",
  "params": {
	"keys": "[{\"keyCode\":28,\"modifiers\":[\"ctrl\"],\"delay\":0,\"duration\":0}]"
  }
}
```

Equivalent key entry content:

```json
{
  "keys": [
    {
    "keyCode": 28,
    "modifiers": ["ctrl"],
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
- Allowed modifiers are only `ctrl`, `alt`, and `shift`.
- `delay` and `duration` must be non-negative.
- `keyCode` values must be discrete numeric values in Linux key range.

Invalid input is rejected before queueing.

## Key Mapping Reference

The following table documents action-to-key mappings used by the platform. Some mappings intentionally differ from what one might expect from the action name.

For rows that include a modifier, use the API modifier string shown in `Modifiers`.

| Action | Linux Key Symbol | Linux Key Code | Modifiers |
| --- | --- | ---: | --- |
| Menu | KEY_HOME | 102 | - |
| Guide | KEY_HOME | 102 | - |
| Info | KEY_F9 | 67 | - |
| Star | KEY_F6 | 64 | - |
| TV Power | KEY_F1 | 59 | - |
| Input | KEY_F15 | 185 | - |
| OK | KEY_OK | 352 | - |
| Select | KEY_ENTER | 28 | - |
| Enter | KEY_ENTER | 28 | - |
| Exit | KEY_ESC | 1 | - |
| Back | KEY_ESC | 1 | - |
| Period | KEY_F5 | 63 | - |
| Push To Talk | KEY_F8 | 66 | - |
| Power | KEY_POWER | 116 | - |
| Channel Up | KEY_UP | 103 | ctrl |
| Channel Down | KEY_DOWN | 108 | ctrl |
| Volume Up | KEY_KPPLUS | 78 | - |
| Volume Down | KEY_KPMINUS | 74 | - |
| Mute | KEY_KPASTERISK | 55 | - |
| Digit 1 | KEY_1 | 2 | - |
| Digit 2 | KEY_2 | 3 | - |
| Digit 3 | KEY_3 | 4 | - |
| Digit 4 | KEY_4 | 5 | - |
| Digit 5 | KEY_5 | 6 | - |
| Digit 6 | KEY_6 | 7 | - |
| Digit 7 | KEY_7 | 8 | - |
| Digit 8 | KEY_8 | 9 | - |
| Digit 9 | KEY_9 | 10 | - |
| Digit 0 | KEY_0 | 11 | - |
| Fast Forward | KEY_F12 | 88 | - |
| Rewind | KEY_F10 | 68 | - |
| Pause | KEY_F11 | 87 | - |
| Play | KEY_F11 | 87 | - |
| Stop | KEY_S | 31 | ctrl |
| Record | KEY_F7 | 65 | - |
| Arrow Up | KEY_UP | 103 | - |
| Arrow Down | KEY_DOWN | 108 | - |
| Arrow Left | KEY_LEFT | 105 | - |
| Arrow Right | KEY_RIGHT | 106 | - |
| Page Up | KEY_PAGEUP | 104 | - |
| Page Down | KEY_PAGEDOWN | 109 | - |
| Last | KEY_L | 38 | ctrl |
| Favorite | KEY_N | 49 | ctrl |
| Key A | KEY_INSERT | 110 | - |
| Key B | KEY_END | 107 | - |
| Key C | KEY_F4 | 62 | - |
| Key D | KEY_DELETE | 111 | - |
| Help | KEY_F2 | 60 | - |
| Setup | KEY_SETUP | 141 | - |
| Next | KEY_NEXT | 407 | - |
| Previous | KEY_PREVIOUS | 412 | - |
| On Demand | KEY_F5 | 63 | - |
| Pound | KEY_BATTERY | 236 | - |
| Audio | KEY_F23 | 193 | - |
| Closed Captioning | KEY_F24 | 194 | - |
| Replay | KEY_B | 48 | ctrl |
| Search | KEY_F3 | 61 | - |
| RF Pair Ghost | KEY_BLUETOOTH | 237 | - |
| Undefined | KEY_UNKNOWN | 240 | - |

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