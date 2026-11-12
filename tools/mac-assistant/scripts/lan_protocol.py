"""XiaoZhi-compatible Opus/AES-CTR envelope, with Mac turn fencing."""
import struct
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes


def crypt(key, iv, payload):
    cipher = Cipher(algorithms.AES(key), modes.CTR(iv)).encryptor()
    return cipher.update(payload) + cipher.finalize()


def encode(key, nonce, sequence, turn, payload, direction=0):
    if len(nonce) != 16 or not 0 < sequence <= 2**32-128 or sequence % 128 or not 0 < len(payload) <= 1468:
        raise ValueError('Invalid envelope')
    header = bytearray(nonce)
    header[0] = 1
    header[1] = direction
    struct.pack_into('>H', header, 2, len(payload))
    struct.pack_into('>II', header, 8, turn, sequence)
    return bytes(header) + crypt(key, bytes(header), payload)


def decode(key, nonce, packet, last_sequence):
    if not 16 < len(packet) <= 1484 or packet[0] != 1:
        raise ValueError('Invalid datagram')
    length = struct.unpack_from('>H', packet, 2)[0]
    turn, sequence = struct.unpack_from('>II', packet, 8)
    if length != len(packet) - 16 or packet[4:8] != nonce[4:8] or sequence <= last_sequence:
        raise ValueError('Stale or malformed datagram')
    return sequence, turn, crypt(key, packet[:16], packet[16:])
