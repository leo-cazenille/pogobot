"""Versioned IR upload frames carried inside the existing SFL serial link."""

import struct
import zlib


VERSION = 1
# The remote prints this before the normal SFL request so make connect can
# select the versioned transfer without changing example Makefiles.
CAPABILITY_BANNER = b"POGOBOT-IR-V2\n"
CHUNK_SIZE = 64
# Full-image passes separate copies in time without extra robot-side buffers.
DEFAULT_COPIES = 3
MAX_COPIES = 5
SLOT_SIZE = 0x20000
SLOT_ADDRESSES = (0x240000, 0x260000)
CMD_START = b"\x10"
CMD_DATA = b"\x11"
CMD_END = b"\x12"
CMD_ABORT = b"\x13"


def image_frames(image, address, transfer_id):
    """Yield (command, payload) pairs for one complete user-slot image."""
    if address not in SLOT_ADDRESSES:
        raise ValueError("IR v2 destination must be 0x240000 or 0x260000")
    if not 0 < len(image) <= SLOT_SIZE:
        raise ValueError("IR v2 image must contain 1 to 131072 bytes")
    if not 0 <= transfer_id <= 0xFFFFFFFF:
        raise ValueError("IR v2 transfer ID must fit in 32 bits")

    image_crc = zlib.crc32(image) & 0xFFFFFFFF
    # Version, reserved flags, transfer ID, mapped address, exact length,
    # 64-byte chunk size, and exact-image CRC use network byte order.
    yield CMD_START, struct.pack(">BBIIIHI", VERSION, 0, transfer_id,
                                 address, len(image), CHUNK_SIZE, image_crc)
    for index, offset in enumerate(range(0, len(image), CHUNK_SIZE)):
        yield CMD_DATA, struct.pack(">IH", transfer_id, index) + image[offset:offset + CHUNK_SIZE]
    yield CMD_END, struct.pack(">I", transfer_id)


def abort_payload(transfer_id):
    return struct.pack(">I", transfer_id)
