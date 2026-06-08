import serial
import time
import struct
import random

# CONNECT TO VIRTUAL PORT 1
# We use /dev/ttys002 as the Vehicle pipe
VIRTUAL_PORT = '/dev/ttys002'

try:
    ser = serial.Serial(VIRTUAL_PORT, 115200, timeout=1)
    print(f"📦 EV Emulator Active. Streaming telemetry to {VIRTUAL_PORT}...")
except Exception as e:
    print(f"❌ Connection Error: Ensure socat is running in your background terminal!\nError details: {e}")
    exit(1)

state = 1          # 0: Idle, 1: Active Charging
expected_v = 6.0   # Baseline steady state voltage for active charging (6 Volts)
current_ms = 0

try:
    while True:
        # 1. Simulate mild, high-frequency electrical white noise on the line
        noise = random.normalvariate(0, 0.04)
        measured_v = expected_v + noise
        
        # 2. INJECT FAULT TIMING: At exactly 5000ms (5 seconds), simulate a gun/latch snap
        # The analog voltage jumps instantly to 9.0V (State B) before the CAN bus drops or alerts
        if current_ms >= 5000:
            measured_v = 9.0 + noise
            
        # 3. Pack data structure into precise binary frames for fast C parsing
        # Frame Format: < (Little Endian), I (uint32 Timestamp), B (uint8 CAN State), f (float32 Voltage)
        packet = struct.pack('<IBf', current_ms, state, measured_v)
        
        # 4. Transmit frame across virtual wire
        ser.write(packet)
        ser.flush()
        
        time.sleep(0.01)  # Broadcast telemetry strictly every 10 milliseconds
        current_ms += 10

except KeyboardInterrupt:
    ser.close()
    print("\n🛑 Emulator stopped cleanly.")