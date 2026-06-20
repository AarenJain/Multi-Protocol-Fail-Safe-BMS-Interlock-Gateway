#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <math.h>

#define VIRTUAL_PORT "/dev/ttys003"
#define CUSUM_THRESHOLD 8.0f    
#define DRIFT_COMPENSATION 0.2f 
#define STATE_IDLE 0
#define STATE_CHARGING 1

// struct for binary packet read
typedef struct __attribute__((packed))
{
    uint32_t timestamp_ms;
    uint8_t  can_state;
    float    measured_voltage;
} TelemetryPacket;

typedef struct
{
    float    cumulative_drift;
    uint32_t last_timestamp;
    uint8_t  relay_tripped;
} SafetyEngine;

// init filter variables
void init_engine(SafetyEngine *engine)
{
    engine->cumulative_drift = 0.0f;
    engine->last_timestamp = 0;
    engine->relay_tripped = 0;
}

// core check logic
void evaluate_safety_step(SafetyEngine *engine, TelemetryPacket *packet)
{
    if (engine->relay_tripped)
        return;

    // network check for timing and packt timeouts
    if (engine->last_timestamp != 0)
    {
        uint32_t dt = packet->timestamp_ms - engine->last_timestamp;
        if (dt > 25)
        { 
            engine->relay_tripped = 1;
            printf("\n[ALERT @ %u ms] NETWORK INTRUSION/TIMEOUT: Frame delay of %u ms detected! Opening Interlock Relay.\n",
                   packet->timestamp_ms, dt);
            return;
        }
    }
    engine->last_timestamp = packet->timestamp_ms;

    // map can state to voltage
    float expected_voltage = 12.0f;
    if (packet->can_state == STATE_IDLE)
        expected_voltage = 9.0f;
    if (packet->can_state == STATE_CHARGING)
        expected_voltage = 6.0f;

    // calculate deviation
    float residual = fabs(packet->measured_voltage - expected_voltage);

    // update cusum score
    float step_drift = residual - DRIFT_COMPENSATION;
    if (step_drift > 0.0f)
    {
        engine->cumulative_drift += step_drift;
    }
    else
    {
        engine->cumulative_drift *= 0.95f; 
    }

    // print active stats
    printf("[%05u ms] CAN State: %u | Pilot V: %5.2fV | Anomaly Score: %5.2f\n",
           packet->timestamp_ms, packet->can_state, packet->measured_voltage, engine->cumulative_drift);

    // threshold trip check
    if (engine->cumulative_drift > CUSUM_THRESHOLD)
    {
        engine->relay_tripped = 1;
        printf("\n[ALERT @ %u ms] STRUCTURAL MISMATCH: Cumulative Drift reached threshold (%.2f)! Opening Interlock Relay.\n",
               packet->timestamp_ms, engine->cumulative_drift);
    }
}

int main()
{
    // open pty port
    int fd = open(VIRTUAL_PORT, O_RDONLY | O_NOCTTY);
    if (fd < 0)
    {
        perror("Error opening serial port. Is socat running in your terminal?");
        return -1;
    }

    // raw terminal config
    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(fd, &tty) != 0)
    {
        perror("Error from tcgetattr");
        close(fd);
        return -1;
    }
    cfmakeraw(&tty);
    tcsetattr(fd, TCSANOW, &tty);

    printf("Gateway Safety Monitor Core Initialized. Listening on %s...\n", VIRTUAL_PORT);

    SafetyEngine engine;
    init_engine(&engine);

    TelemetryPacket packet;

    // processing stream loop
    while (!engine.relay_tripped)
    {
        ssize_t bytes_read = read(fd, &packet, sizeof(TelemetryPacket));
        if (bytes_read == sizeof(TelemetryPacket))
        {
            evaluate_safety_step(&engine, &packet);
        }
    }

    close(fd);
    printf("Evaluation Complete. Gateway safely locked down.\n");
    return 0;
}
