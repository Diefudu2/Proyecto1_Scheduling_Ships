# serial_client.py
# Cliente serial SIN hilos para Scheduling Ships ESP32-C6.
# Lectura no bloqueante desde tkinter.after().

import time

try:
    import serial
    import serial.tools.list_ports
except ImportError as exc:
    raise RuntimeError("Falta pyserial. Instale con: pip3 install pyserial") from exc


class SerialClient:
    def __init__(self, port="/dev/ttyACM0", baud=115200):
        self.port = port
        self.baud = baud
        self.ser = None
        self._rx_buffer = b""

    @staticmethod
    def list_ports():
        return [p.device for p in serial.tools.list_ports.comports()]

    def is_connected(self):
        return self.ser is not None and self.ser.is_open

    def connect(self, port=None, baud=None):
        if port:
            self.port = port
        if baud:
            self.baud = baud

        if self.is_connected():
            return True

        self.ser = serial.Serial(self.port, self.baud, timeout=0)
        time.sleep(0.25)
        self._rx_buffer = b""
        return True

    def disconnect(self):
        if self.ser:
            try:
                self.ser.close()
            finally:
                self.ser = None
                self._rx_buffer = b""

    def send(self, command):
        if not self.is_connected():
            raise RuntimeError("Serial no conectado")

        command = command.strip()
        if command:
            self.ser.write((command + "\n").encode("utf-8"))

    def poll_lines(self, max_bytes=2048):
        lines = []

        if not self.is_connected():
            return lines

        try:
            waiting = self.ser.in_waiting
            if waiting <= 0:
                return lines
            data = self.ser.read(min(waiting, max_bytes))
        except serial.SerialException as exc:
            lines.append(f"ERR SERIAL_READ {exc}")
            self.disconnect()
            return lines

        if not data:
            return lines

        self._rx_buffer += data

        while b"\n" in self._rx_buffer:
            raw, self._rx_buffer = self._rx_buffer.split(b"\n", 1)
            text = raw.decode("utf-8", errors="replace").strip()
            if text:
                lines.append(text)

        if len(self._rx_buffer) > 4096:
            self._rx_buffer = b""
            lines.append("ERR SERIAL_BUFFER_OVERFLOW")

        return lines
