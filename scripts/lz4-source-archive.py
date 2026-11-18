#!/usr/bin/env python3
"""Create the checksummed raw LZ4 source module supported by ZealBooter."""
import argparse
import ctypes
import ctypes.util
import pathlib
import struct
import zlib

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('source', type=pathlib.Path)
parser.add_argument('output', type=pathlib.Path)
args = parser.parse_args()
data = args.source.read_bytes()
if not 0 < len(data) <= 128 * 1024 * 1024:
    parser.error('source must be between 1 byte and 128 MiB')
name = ctypes.util.find_library('lz4')
if not name:
    parser.error('liblz4 is required')
lib = ctypes.CDLL(name)
lib.LZ4_compressBound.argtypes = [ctypes.c_int]
lib.LZ4_compress_HC.argtypes = [ctypes.c_void_p, ctypes.c_void_p,
                                 ctypes.c_int, ctypes.c_int, ctypes.c_int]
capacity = lib.LZ4_compressBound(len(data))
src = ctypes.create_string_buffer(data)
dst = ctypes.create_string_buffer(capacity)
size = lib.LZ4_compress_HC(src, dst, len(data), capacity, 12)
if size <= 0:
    raise RuntimeError('LZ4 compression failed')
packed = dst.raw[:size]
header = b'ZEALLZ41' + struct.pack('<IIII', len(data), size,
                                      zlib.crc32(data), zlib.crc32(packed))
args.output.write_bytes(header + packed)
print(f'{len(data)} bytes -> {len(header) + size} bytes')
