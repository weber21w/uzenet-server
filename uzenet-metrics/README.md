# Uzenet Metrics Server

The `uzenet-metrics-server` provides lightweight monitoring and health statistics for running Uzenet services. It offers an HTTP-based interface for both human-readable status and machine-readable data aggregation.

## Features

- Exposes a `/metrics` endpoint over HTTP
- Returns memory usage, CPU time, and runtime for each instrumented service
- Format compatible with Prometheus or simple text parsing
- Human-readable output at root (`/`) for browser access
- Minimal overhead and no dependencies beyond `libmicrohttpd`
- May eventually support per-room or per-game usage statistics

## Port

- Listens on configurable HTTP port (default TBD, typically 57432 or similar)

## Installation

Use the Makefile:

```bash
make install
```

This:
- Installs the server binary to `/usr/local/bin/`
- Creates and enables the systemd service `uzenet-metrics`

## Removal

```bash
make remove
```

## Service Status

```bash
make status
```

## Instrumentation

- Individual Uzenet daemons can be compiled with `metrics_enable()` support
- Metrics can include:
  - Resident set size (RSS) memory
  - CPU usage time (user + sys)
  - Uptime in seconds
  - Optional per-request counters

## Logging

- All errors logged via `syslog()` for consistency
- Not designed for authentication or rate limiting—intended for internal/admin use

## Use Cases

- Monitor live RAM/CPU usage of Uzenet services
- Web dashboard or Grafana integration
- Cron-based snapshotting for debugging or auditing

## Related Services

- `uzenet-admin-server`: May expose live metrics for administrator view
- `uzenet-score`: Could use metrics to correlate load with gameplay
- `fail2ban`: Uses logs but not metrics directly
