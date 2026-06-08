#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <math.h>

#define VIRTUAL_PORT "/dev/ttys003"
#define CUSUM_THRESHOLD 8.0f    // Sensitivity baseline for fault trip
#define DRIFT_COMPENSATION 0.2f // Filter out normal analog white noise
#define STATE_IDLE 0
#define STATE_CHARGING 1

// Binary packet structure matching Python's struct.pack layout
typedef struct __attribute__((packed))
{
    uint32_t timestamp_ms;
    uint8_t can_state;
    float measured_voltage;
} TelemetryPacket;

typedef struct
{
    float cumulative_drift;
    uint32_t last_timestamp;
    uint8_t relay_tripped;
} SafetyEngine;

// Initialize the sequential change detection parameters
void init_engine(SafetyEngine *engine)
{
    engine->cumulative_drift = 0.0f;
    engine->last_timestamp = 0;
    engine->relay_tripped = 0;
}

// CUSUM Sequential Change Detection Loop
void evaluate_safety_step(SafetyEngine *engine, TelemetryPacket *packet)
{
    if (engine->relay_tripped)
        return;

    // 1. Networking Profile Check: Network Liveliness / Timeout Monitor
    if (engine->last_timestamp != 0)
    {
        uint32_t dt = packet->timestamp_ms - engine->last_timestamp;
        if (dt > 25)
        { // If delay between consecutive packets > 25ms
            engine->relay_tripped = 1;
            printf("\n[🚨 ALERT @ %u ms] NETWORK INTRUSION/TIMEOUT: Frame delay of %u ms detected! Opening Interlock Relay.\n",
                   packet->timestamp_ms, dt);
            return;
        }
    }
    engine->last_timestamp = packet->timestamp_ms;

    // 2. Electronics Profile Check: Map Digital CAN State to Physical Voltage Profile
    float expected_voltage = 12.0f;
    if (packet->can_state == STATE_IDLE)
        expected_voltage = 9.0f;
    if (packet->can_state == STATE_CHARGING)
        expected_voltage = 6.0f;

    // 3. Compute Innovation Residual (Mathematical deviation)
    float residual = fabs(packet->measured_voltage - expected_voltage);

    // 4. Update Sequential CUSUM Accumulator
    float step_drift = residual - DRIFT_COMPENSATION;
    if (step_drift > 0.0f)
    {
        engine->cumulative_drift += step_drift;
    }
    else
    {
        engine->cumulative_drift *= 0.95f; // Exponential decay to purge minor transient noise
    }

    // Print real-time streaming telemetry status
    printf("[%05u ms] CAN State: %u | Pilot V: %5.2fV | Anomaly Score: %5.2f\n",
           packet->timestamp_ms, packet->can_state, packet->measured_voltage, engine->cumulative_drift);

    // 5. Threshold Validation
    if (engine->cumulative_drift > CUSUM_THRESHOLD)
    {
        engine->relay_tripped = 1;
        printf("\n[🚨 ALERT @ %u ms] STRUCTURAL MISMATCH: Cumulative Drift reached threshold (%.2f)! Opening Interlock Relay.\n",
               packet->timestamp_ms, engine->cumulative_drift);
    }
}

int main()
{
    // Open macOS Pseudo-Terminal port
    int fd = open(VIRTUAL_PORT, O_RDONLY | O_NOCTTY);
    if (fd < 0)
    {
        perror("❌ Error opening serial port. Is socat running in your terminal?");
        return -1;
    }

    // Configure basic serial interface parameters for raw binary read
    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(fd, &tty) != 0)
    {
        perror("❌ Error from tcgetattr");
        close(fd);
        return -1;
    }
    cfmakeraw(&tty);
    tcsetattr(fd, TCSANOW, &tty);

    printf("🛡️ Gateway Safety Monitor Core Initialized. Listening on %s...\n", VIRTUAL_PORT);

    SafetyEngine engine;
    init_engine(&engine);

    TelemetryPacket packet;

    // Read streaming packed structures sequentially from the virtual bus
    while (!engine.relay_tripped)
    {
        ssize_t bytes_read = read(fd, &packet, sizeof(TelemetryPacket));
        if (bytes_read == sizeof(TelemetryPacket))
        {
            evaluate_safety_step(&engine, &packet);
        }
    }

    close(fd);
    printf("🏁 Evaluation Complete. Gateway safely locked down.\n");
    return 0;
}