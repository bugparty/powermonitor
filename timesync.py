import serial
import struct
import time
import datetime # Import datetime module

# === Configuration ===
PORT = '/dev/tty.usbmodem1101'   # Change to your serial port name, e.g., COM3 on Windows
BAUDRATE = 1000000
TIMEOUT = 1  # seconds
LOG_FILE_PREFIX = "serial_log_" # Log file name prefix
MAX_CONSECUTIVE_ERRORS = 3 # Threshold for consecutive errors
ERROR_SLEEP_DURATION = 1 # Sleep duration after an error (seconds)

# === Packet Structure ===
# header(2) + seq(2) + T1(8) + T2(8) + T3(8) + crc(2) = 30 bytes
PACKET_FORMAT = '<HHQQQH'  # Little-endian: header, seq, T1, T2, T3, crc
PACKET_SIZE = 30

def calc_crc(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc & 0xFFFF

# Record a baseline at the start of the script
base_py = time.monotonic()          # seconds
base_us = int(base_py * 1_000_000)   # Corresponding microsecond baseline

def now_us():
    return int((time.monotonic() * 1_000_000) - base_us)

# === Logging Function ===
def log_message(log_file, message):
    timestamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
    log_entry = f"[{timestamp}] {message}\n"
    print(message) # Also print to the console
    log_file.write(log_entry)
    log_file.flush() # Ensure immediate write

# === Main Program ===
def main():
    # Create a log file name with a timestamp
    log_filename = f"{LOG_FILE_PREFIX}{datetime.datetime.now().strftime('%Y%m%d_%H%M%S')}.log"
    log_f = open(log_filename, "w") # Open the log file in write mode

    try:
        ser = serial.Serial(PORT, BAUDRATE, timeout=TIMEOUT)
        log_message(log_f, f"Serial port {PORT} opened.")
        time.sleep(0.5)
        initial_line = ser.readline()
        if initial_line:
             log_message(log_f, f"Initial line from serial: {initial_line.decode(errors='ignore').strip()}")
        else:
             log_message(log_f, "No initial line received from serial.")

        seq = 0
        consecutive_errors = 0 # Initialize consecutive error counter

        while True:
            seq += 1
            error_occurred = False # Flag to indicate if an error occurred in this iteration
            # Generate sync request
            T1 = now_us()
            packet_data = struct.pack('<HHQ', 0xAA55, seq, T1)
            crc = calc_crc(packet_data)
            packet = packet_data + struct.pack('<H', crc)

            # Pad to 30 bytes (if needed, based on your protocol)
            # packet += b'\x00' * (PACKET_SIZE - len(packet)) # If the protocol requires sending a fixed 30 bytes

            # Send the complete packet
            log_message(log_f, f"--> SEND seq={seq}, T1={T1}, Data: {packet.hex()}")
            ser.write(packet)
            ser.flush()

            # Receive reply
            # Read the header magic first
            state = 0 # 0: first byte, 1: second byte
            magic = b'\x55\xaa'
            while state < 2:
                ch = ord(ser.read(1))
                if state == 0:
                    if ch == magic[0]:
                        state += 1
                        continue
                    else:
                        continue
                elif state == 1:
                    if ch == magic[1]:
                        state += 1
                        break
                    else:
                        state = 0
                        continue
                
            reply = ser.read(PACKET_SIZE - 2)
            reply = magic + reply
            if len(reply) == PACKET_SIZE:
                log_message(log_f, f"<-- RECV Data: {reply.hex()}")
                # Validation
                if reply[0:2] != b'\x55\xaa':
                    log_message(log_f, "Error: Invalid header")
                    error_occurred = True
                    # continue # No longer directly continuing, handle error count first

                else: # Only proceed with CRC validation and parsing if the header is correct
                    recv_crc = struct.unpack_from('<H', reply, PACKET_SIZE - 2)[0]
                    calc = calc_crc(reply[:-2])
                    if recv_crc != calc:
                        log_message(log_f, f"Error: CRC mismatch. Received={recv_crc}, Calculated={calc}")
                        error_occurred = True
                        # continue # No longer directly continuing

                    else:
                        # Parse
                        header, seq_recv, T1_recv, T2, T3, _ = struct.unpack(PACKET_FORMAT, reply)
                        T4 = now_us() # Current time (us)

                        # Calculate offset and delay
                        delay = (T4 - T1_recv) - (T3 - T2)
                        offset = ((T2 - T1_recv) + (T3 - T4)) // 2
                        # Offset indicates how late the slave received the message
                        log_message(log_f, f"    Parsed: seq={seq_recv}, T1={T1_recv}, T2={T2}, T3={T3}, T4={T4}")
                        log_message(log_f, f"    Result: Delay = {delay} us, Offset = {offset} us\n")
                        # header(2) + seq(2) + T1(8) + T2(8) + T3(8) + crc(2) = 30 bytes
                        CMD_PACKET_FORMAT = '<HHq'  # Little-endian: header, seq, offset
                        packet_data = struct.pack(CMD_PACKET_FORMAT, 0xAA56, seq, -offset)
                        crc = calc_crc(packet_data)
                        packet = packet_data + struct.pack('<H', crc)
                        ser.write(packet)
                        ser.flush()
                        consecutive_errors = 0 # Reset error counter on success

            elif len(reply) > 0:
                 log_message(log_f, f"<-- RECV Incomplete Data ({len(reply)} bytes): {reply.hex()}")
                 log_message(log_f, "Error: Incomplete reply")
                 error_occurred = True
            else:
                log_message(log_f, "Error: Timeout waiting for reply")
                error_occurred = True

            # Check if an error occurred and handle the counter
            if error_occurred:
                consecutive_errors += 1
                log_message(log_f, f"Consecutive errors: {consecutive_errors}")
                if consecutive_errors >= MAX_CONSECUTIVE_ERRORS:
                    log_message(log_f, f"Reached {MAX_CONSECUTIVE_ERRORS} consecutive errors. Sleeping for {ERROR_SLEEP_DURATION} second(s)...")
                    time.sleep(ERROR_SLEEP_DURATION)
                    consecutive_errors = 0 # Reset counter after sleeping

            # If no error occurred, sleep normally
            if not error_occurred:
                 time.sleep(1)  # Sync every 1 second (only if successful or error count is below threshold)
            # If an error occurred but the counter is not full, do not sleep additionally, immediately try again

    except serial.SerialException as e:
        log_message(log_f, f"Serial Error: {e}")
    except Exception as e:
        log_message(log_f, f"An unexpected error occurred: {e}")
    finally:
        if 'ser' in locals() and ser.is_open:
            ser.close()
            log_message(log_f, f"Serial port {PORT} closed.")
        log_f.close() # Close the log file
        print(f"Log saved to {log_filename}")


if __name__ == "__main__":
    main()