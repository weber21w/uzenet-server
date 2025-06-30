# uzenet-sim

`uzenet-sim` is a server used to simulate the behavior of remote Uzebox devices for purposes such as game logic validation, debugging, and headless replay. It allows a developer to test how their Uzebox program will behave under networked multiplayer conditions, including lockstep synchronization and input queuing without needing physical devices or real players.

## Features

- Headless simulation of Uzebox behavior for connected games
- Can simulate:
  - Lockstep-style gameplay (every device receives inputs for the same frame before advancing)
  - Delayed or queued input streams to simulate lag
  - Input echoing for debugging and diagnostics
- Future expansion to support prediction and rollback simulation testing
- Console logging for detailed frame, input, and desync analysis

## Usage

The server listens for incoming simulation control connections, which may be initiated by test scripts, developer tools, or Uzebox development environments. Input data and simulation commands are exchanged via a simple IPC or TCP format (to be documented).

Expected usage pattern:

1. A test harness connects to `uzenet-sim` to register a simulation session.
2. It provides fake or pre-recorded input streams for each simulated player.
3. The server processes simulated game frames, sending frame-level sync updates to each simulated Uzebox session.
4. Logging or statistics are reported to the console or files.

## Intended Clients

- Developer-side simulation runners
- Automated test harnesses for multiplayer logic
- Possible future integration with visual frontend emulators

## Future Enhancements

- Support for rollback-style emulation
- Runtime input injection via command socket
- WebSocket control interface for browser-based orchestration
- Integration with `uzenet-room` to serve as a "ghost client" or replay agent

## Source Location

This service lives in `uzenet-sim/` and currently includes:
- `uzenet-sim-server.c`: Main server logic (to be developed or stubbed initially)
- `Makefile`: Build instructions

## Build Instructions

```bash
cd uzenet-sim
make
```

## License

Same as Uzenet core – MIT-style license.
