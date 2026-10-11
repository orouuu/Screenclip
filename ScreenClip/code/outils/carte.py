import re

from serial.tools import list_ports

ESPRESSIF_VID = 0x303A

def is_programming(port):
    text = f"{port.product or ''} {port.description or ''}".lower()
    if "jtag" in text:
        return True
    return re.fullmatch(r"[0-9A-Fa-f]{12}", port.serial_number or "") is None

def find(programming):
    for port in list_ports.comports():
        if port.vid == ESPRESSIF_VID and is_programming(port) == programming:
            return port.device
    return None
