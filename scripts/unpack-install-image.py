#!/usr/bin/env python3
"""Reconstruct a disposable raw template from a packed installer payload."""
import argparse
import pathlib
import struct

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('payload', type=pathlib.Path)
parser.add_argument('output', type=pathlib.Path)
args = parser.parse_args()
header = struct.Struct('<8sIIQIIQ')
extent = struct.Struct('<QII')
with args.payload.open('rb') as src, args.output.open('wb') as dst:
    magic, version, sector, sectors, count, entry_size, payload_size = header.unpack(src.read(header.size))
    if magic != b'ZINST001' or version != 1 or sector != 512 or entry_size != extent.size or payload_size != args.payload.stat().st_size:
        raise ValueError('invalid installer header')
    entries = [extent.unpack(src.read(extent.size)) for _ in range(count)]
    dst.truncate(sectors * sector)
    end = 0
    for first, length, reserved in entries:
        if reserved or not length or first < end or first + length > sectors:
            raise ValueError('invalid installer extent')
        end = first + length
        dst.seek(first * sector)
        data = src.read(length * sector)
        if len(data) != length * sector:
            raise ValueError('truncated installer payload')
        dst.write(data)
    if src.read(1):
        raise ValueError('unexpected trailing data')
