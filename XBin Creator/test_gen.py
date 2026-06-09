import sys
sys.path.insert(0, r'e:\STM32F407_2025\2.BootLoader\XBin Creator')
from gen_magic_header_gui import build_xbin, crc32

data = b'\x00\x01\x02\x03' * 100
config = {
    'magic': 0x4D414749,
    'data_type': 1,
    'data_offset': 4096,
    'data_address': 0x08010000,
    'this_address': 0x0800C000,
    'ver_major': 1,
    'ver_minor': 0,
    'ver_patch': 0,
    'ver_extra': 'alpha',
}

result = build_xbin(data, config)
header_struct = result[:256]

print(f"Total size: {len(result)}")
print(f"Header size: {config['data_offset']}")
print(f"data_crc32 computed: 0x{crc32(data):08X}")
print(f"this_crc32 stored:   0x{int.from_bytes(header_struct[252:256], 'little'):08X}")
print(f"this_crc32 verify:   0x{crc32(header_struct[:252]):08X}")

# Verify: stored should equal verify
stored = int.from_bytes(header_struct[252:256], 'little')
verify = crc32(header_struct[:252])
assert stored == verify, f"CRC mismatch: stored={stored:08X} verify={verify:08X}"
print("All checks passed!")
