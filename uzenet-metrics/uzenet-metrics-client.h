#ifndef METRICS_CLIENT_H
#define METRICS_CLIENT_H

// Initialize connection to uzenet-metrics sidecar.
// Returns 0 on success, -1 on failure.
int metrics_init(const char *socket_path);

// Send a gauge: set the current value of <name>.
void metrics_gauge(const char *name, double value);

// Increment (or decrement) a counter by <delta>.
void metrics_counter(const char *name, int delta);

// Close the metrics connection cleanly.
void metrics_close(void);

#endif // METRICS_CLIENT_H
