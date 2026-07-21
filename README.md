# RDK EntServices Tools

WPEFramework (Thunder) plugin that exposes device tool operations through JSON-RPC and COM-RPC.

## Current Scope

- Plugin name: `Tools`
- Callsign: `org.rdk.Tools`
- Autostart: `true`
- Primary implemented capability: generate Linux key events through a queued worker thread

## Repository Layout

- `plugin/`: plugin interface and implementation sources
- `CMakeLists.txt`: top-level build entry
- `services.cmake`: plugin feature selection options

## Build

Typical out-of-tree CMake flow:

```sh
cmake -S . -B build
cmake --build build
```

Build includes:

- Shared plugin library (`${NAMESPACE}Tools`)
- Shared implementation library (`${NAMESPACE}ToolsImplementation`)
- Generated plugin config from `plugin/Tools.conf.in` and `plugin/Tools.config`

## API Payload Format

`GenerateKey` accepts key entries as a JSON array represented as a string, which is compatible with COMRPC limitations around nested arrays.

Recommended request pattern:

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

The implementation also accepts legacy payload forms for compatibility:

- `keys` as a real JSON array
- Entire input parsed directly as a JSON array string

## Runtime Configuration

Configuration template is defined in:

- `plugin/Tools.conf.in`
- `plugin/Tools.config`

Important fields:

- `callsign`: `org.rdk.Tools`
- `root.locator`: `lib@PLUGIN_IMPLEMENTATION@.so`
- `root.mode`: `@PLUGIN_TOOLS_MODE@`

## Notes

- The implementation source file is currently named `ToolsImplementation.cpp`.
- JSON payload validation for key generation is strict and rejects malformed key, modifier, delay, and duration values.
- Device-specific KED to Linux key mappings are documented in `PRODUCT.md` under `Key Mapping Reference`.