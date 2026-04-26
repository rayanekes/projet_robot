import serial
import time
import os
import glob

PORT = '/dev/ttyUSB0'
BAUD = 115200

def read_until(ser, expected):
    while True:
        line = ser.readline()
        if not line:
            return False
        try:
            dec = line.decode('utf-8', errors='ignore').strip()
            print("ESP32:", dec)
            if dec == expected:
                return True
            if "FAIL" in dec or "TIMEOUT" in dec:
                return False
        except:
            pass

try:
    print("Connecting to ESP32...")
    ser = serial.Serial(PORT, BAUD, timeout=5)
    ser.dtr = False
    ser.rts = False
    time.sleep(1)
    ser.dtr = True
    ser.rts = True
    time.sleep(2)
    
    ser.write(b"PING\n")
    if not read_until(ser, "PONG") and not read_until(ser, "READY"):
        print("ESP32 not responding correctly. Retrying...")
        ser.write(b"PING\n")
        read_until(ser, "PONG")

    bmp_files = glob.glob("*.bmp")
    for f in bmp_files:
        size = os.path.getsize(f)
        print(f"\n--- Uploading {f} ({size} bytes) ---")
        ser.write(f"WRITE /{f}\n".encode())
        if read_until(ser, "SEND_SIZE"):
            ser.write(f"{size}\n".encode())
            if read_until(ser, "SEND_DATA"):
                with open(f, 'rb') as file:
                    while chunk := file.read(1024):
                        ser.write(chunk)
                if read_until(ser, "DONE"):
                    print(f"{f} uploaded successfully.")
                else:
                    print(f"Failed to finish {f}")
            else:
                print("ESP32 not ready for data")
        else:
            print("ESP32 not ready for size")
            
except Exception as e:
    print(f"Error: {e}")
finally:
    if 'ser' in locals() and ser.is_open:
        ser.close()
