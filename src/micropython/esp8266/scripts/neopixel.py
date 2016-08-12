# NeoPixel driver for MicroPython on ESP8266
# MIT license; Copyright (c) 2016 Damien P. George

from esp import neopixel_write

class NeoPixel:
    def __init__(self, pin, n):
        self.pin = pin
        self.n = n
        self.buf = bytearray(n * 3)
        self.pin.init(pin.OUT, pin.PULL_NONE)

    def __setitem__(self, index, val):
        r, g, b = val
        self.buf[index * 3] = g
        self.buf[index * 3 + 1] = r
        self.buf[index * 3 + 2] = b

    def __getitem__(self, index):
        i = index * 3
        return self.buf[i + 1], self.buf[i], self.buf[i + 2]

    def write(self):
        neopixel_write(self.pin, self.buf, True)
