# RDK EntServices Tools - Architecture

## Overview

The EntServices Tools component is a Thunder plugin pair:

- `Tools` (plugin-facing proxy): host-facing lifecycle and JSON-RPC registration
- `ToolsImplementation` (service logic): validates requests and performs key event injection

Current implementation focuses on generating Linux key events asynchronously.

## High-Level Architecture

```text
Client Applications
        |
        v
Thunder JSON-RPC / COM-RPC
        |
        v
Tools Plugin (Tools.cpp)
        |
        v
ToolsImplementation Service (ToolsImplementation.cpp)
        |
        v
uinput dispatcher (UINPUT_*)
        |
        v
Linux input subsystem
```

## Core Components

### Plugin Layer

- `plugin/Tools.h`
- `plugin/Tools.cpp`

Responsibilities:

- Plugin lifecycle (`Initialize`, `Deinitialize`)
- Acquire implementation via `Root<Exchange::ITools>`
- Register and unregister JSON-RPC bindings through `Exchange::JTools`

### Implementation Layer

- `plugin/ToolsImplementation.h`
- `plugin/ToolsImplementation.cpp`

Responsibilities:

- Parse and validate key generation request payloads
- Queue key events in FIFO order
- Dispatch queued events on a worker thread
- Manage key down/up emission with optional delay and duration

### Module Layer

- `plugin/Module.h`
- `plugin/Module.cpp`

Responsibilities:

- Thunder module declaration via `MODULE_NAME_DECLARATION(BUILD_REFERENCE)`
- Shared include point for plugin and implementation sources

## Concurrency Model

- Request thread validates payload and pushes events into `_sendKeyQueue`
- `std::mutex` and `std::condition_variable` protect queue access
- Dedicated worker thread waits for work and dispatches events in order
- Deinitialization stops the worker thread and clears queue state

## Key Injection Flow

1. Client sends `GenerateKey` payload.
2. Implementation validates envelope and each key entry.
3. Valid entries are converted to queued key events.
4. Worker thread processes queue:
   - optional pre-key delay
   - key down for modifiers
   - key down/up for main key
   - key up for modifiers (reverse order)

## Error Handling

- Invalid payload shape or values return `Core::ERROR_INVALID_INPUT_LENGTH`.
- uinput initialization failure returns `Core::ERROR_GENERAL` from `Configure`.
- On success, methods return `Core::ERROR_NONE`.

## Build and Packaging

- Root build entry: `CMakeLists.txt`
- Service options: `services.cmake`
- Plugin build: `plugin/CMakeLists.txt`
- Packaging follows standard WPEFramework plugin package variables.