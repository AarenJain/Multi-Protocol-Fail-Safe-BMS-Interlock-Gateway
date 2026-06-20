import serial
import time
import struct
import random

virtual_port = '/dev/ttys002'

try:
    ser = serial.Serial(virtual_port, 115200, timeout=1)
    print(f"EV Emulator Active. Streaming telemetry to {virtual_port}...")
except Exception as e:
    print(f"Connection Error: Ensure socat is running!\nError details: {e}")
    exit(1)

state = 1          
expected_v = 6.0   
current_ms = 0

try:
    while True:
        # add mild white noise to simulate real line distortion
        noise = random.normalvariate(0, 0.04)
        measured_v = expected_v + noise
        
        # inject latch fault at 5 seconds to simulate gun unlatch
        if current_ms >= 5000:
            measured_v = 9.0 + noise
            
        # write parameters into binary packt frame for fast c read
        packet = struct.pack('<IBf', current_ms, state, measured_v)
        
        # transmit data over virtual link
        ser.write(packet)
        ser.flush()
        
        time.sleep(0.01)  
        current_ms += 10

except KeyboardInterrupt:
    ser.close()
    print("\nEmulator stopped cleanly.")
