import esp
import uctypes


def main():

    ROM = uctypes.bytearray_at(0x40200000, 16)
    fid = esp.flash_id()

    print("Flash ID: %x (Vendor: %x Device: %x)" % (fid, fid & 0xff, fid & 0xff00 | fid >> 16))

    print("Flash bootloader data:")
    SZ_MAP = {0: "512KB", 1: "256KB", 2: "1MB", 3: "2MB", 4: "4MB"}
    FREQ_MAP = {0: "40MHZ", 1: "26MHZ", 2: "20MHz", 0xf: "80MHz"}
    print("Byte @2: %02x" % ROM[2])
    print("Byte @3: %02x (Flash size: %s Flash freq: %s)" % (ROM[3], SZ_MAP.get(ROM[3] >> 4, "?"), FREQ_MAP.get(ROM[3] & 0xf)))


main()
