#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Add your SSH public key to a Linux Duo Bench RAM boot image (Android boot image, header v2).

The released image ships WITHOUT a key. This appends a tiny uncompressed cpio with root/.ssh/authorized_keys to the
image's ramdisk (Linux unpacks concatenated archives in order), fixes the ramdisk size and the image id, and writes a
new image. Nothing is sent to a phone; the result is RAM-booted with `fastboot boot`.

usage: python3 add-ssh-key.py <duo-ram.img> <your key .pub> <out.img>
"""
import hashlib, struct, sys


def cpio_entry(name, mode, data=b''):
    nb = name.encode() + b'\0'
    fields = [1, mode, 0, 0, 1, 0, len(data), 0, 0, 0, 0, len(nb), 0]
    h = b'070701' + ''.join('%08x' % f for f in fields).encode()
    return h + nb + b'\0' * ((4 - (len(h) + len(nb)) % 4) % 4) + data + b'\0' * ((4 - len(data) % 4) % 4)


def main(src, pub, out):
    b = open(src, 'rb').read()
    if b[:8] != b'ANDROID!' or struct.unpack_from('<I', b, 40)[0] != 2:
        sys.exit('%s is not a header v2 boot image' % src)
    ks, = struct.unpack_from('<I', b, 8); rs, = struct.unpack_from('<I', b, 16)
    page, = struct.unpack_from('<I', b, 36); ds, = struct.unpack_from('<I', b, 1648)
    up = lambda n: -(-n // page) * page
    K = b[page:page + ks]; ro = page + up(ks); R = b[ro:ro + rs]; do = ro + up(rs); D = b[do:do + ds]
    key = open(pub, 'rb').read().strip()
    if not key.startswith((b'ssh-', b'ecdsa-', b'sk-')):
        sys.exit('%s does not look like an SSH public key (.pub)' % pub)
    cpio = (cpio_entry('root', 0o40755) + cpio_entry('root/.ssh', 0o40700) +
            cpio_entry('root/.ssh/authorized_keys', 0o100600, key + b'\n') + cpio_entry('TRAILER!!!', 0))
    R2 = R + b'\0' * ((4 - rs % 4) % 4) + cpio
    sha = hashlib.sha1()
    for blob in (K, R2, b'', b'', D):   # kernel, ramdisk, second, recovery_dtbo, dtb (+ sizes), like mkbootimg
        sha.update(blob); sha.update(struct.pack('<I', len(blob)))
    hdr = bytearray(b[:page])
    struct.pack_into('<I', hdr, 16, len(R2))
    hdr[576:608] = sha.digest().ljust(32, b'\0')
    pad = lambda x: x + b'\0' * (up(len(x)) - len(x))
    with open(out, 'wb') as f:
        f.write(bytes(hdr)); f.write(pad(K)); f.write(pad(R2)); f.write(pad(D))
    print('wrote %s with your key (ramdisk %d -> %d bytes)' % (out, rs, len(R2)))


if __name__ == '__main__':
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:4])
