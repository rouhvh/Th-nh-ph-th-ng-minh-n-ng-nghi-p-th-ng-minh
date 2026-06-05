"""Simple Arduino serial helper using pyserial.

Usage:
  from arduino_interface import ArduinoInterface
  a = ArduinoInterface(port=None, baud=9600, autoconnect=True)
  a.send_command('ALARM')
  a.close()

The module will try to auto-detect a serial port that looks like an Arduino
if `port` is None and `autoconnect` is True.
"""
from typing import Optional
import time

try:
    import serial
    import serial.tools.list_ports
except Exception as exc:
    raise ImportError("pyserial is required for Arduino support. Install with 'pip install pyserial'.") from exc


def find_arduino_port() -> Optional[str]:
    ports = list(serial.tools.list_ports.comports())
    for p in ports:
        desc = (p.description or "").lower()
        if 'arduino' in desc or 'arduino' in (p.manufacturer or '').lower():
            return p.device
    # fallback: return first available port
    if ports:
        return ports[0].device
    return None


class ArduinoInterface:
    def __init__(self, port: Optional[str] = None, baud: int = 9600, timeout: float = 1.0, autoconnect: bool = True):
        self.port = port
        self.baud = int(baud)
        self.timeout = float(timeout)
        self.ser = None
        if autoconnect:
            self.open()

    def open(self):
        if self.ser and self.ser.is_open:
            return
        port_to_try = self.port
        if not port_to_try:
            port_to_try = find_arduino_port()
        if not port_to_try:
            raise RuntimeError("No serial ports found for Arduino. Set ARDUINO_PORT env or connect a device.")
        self.ser = serial.Serial(port_to_try, self.baud, timeout=self.timeout)
        # small delay for Arduino reset-on-open
        time.sleep(0.2)
        self.port = port_to_try

    def send_command(self, payload: str):
        if self.ser is None or not getattr(self.ser, 'is_open', False):
            self.open()
        data = (str(payload) + "\n").encode('utf-8')
        self.ser.write(data)

    def close(self):
        try:
            if self.ser and getattr(self.ser, 'is_open', False):
                self.ser.close()
        except Exception:
            pass


if __name__ == '__main__':
    print('ArduinoInterface test')
    try:
        a = ArduinoInterface(autoconnect=True)
        print('Connected to', a.port)
        a.send_command('PING')
        a.close()
    except Exception as e:
        print('Arduino test failed:', e)
