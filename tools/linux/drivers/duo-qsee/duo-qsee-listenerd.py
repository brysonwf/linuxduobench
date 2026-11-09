#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# duo-qsee-listenerd - the normal-world storage services TrustZone's trusted applications call back into (QSEECOM
# listeners), served through duo_qsee's /dev/duo_qsee_listener. Replaces what Android's qseecomd does for:
#   0x000a  file service: POSIX-shaped open/read/write/close/... on plain files
#   0x7000  GP file service: whole-object read/write/delete/rename (the sealed objects; opaque to us)
# Message layouts and opcodes follow Qualcomm's published listener definitions (qualcomm/minkipc,
# listeners/libfsservice/fs/fs_msg.h and gpfs/gpfs_msg.h, BSD-3-Clause-Clear); notes:
# docs/research/workbench/secure-world.md. Every path a TA sends is rooted under ROOT (no escaping it).
# usage: duo-qsee-listenerd.py [--root /var/lib/duo/qsee] [-v]
import errno, os, struct, sys

ROOT = sys.argv[sys.argv.index("--root") + 1] if "--root" in sys.argv else "/var/lib/duo/qsee"
VERBOSE = "-v" in sys.argv
DEV = "/dev/duo_qsee_listener"
NAME = 256            # TZ_CM_MAX_NAME_LEN
DATA = 20000          # TZ_CM_MAX_DATA_LEN (fs read/write chunk)
GP_DATA = 512000      # GPFS read data limit
fds = {}              # TA-visible fd -> our os fd
last_errno = 0
next_fd = 3

def log(*a):
    print(*a, flush=True)

def path_of(b, off):
    """a NUL-terminated path from the request, rooted under ROOT (.. cannot escape)"""
    raw = b[off:off + NAME].split(b"\0")[0].decode(errors="replace")
    p = os.path.normpath("/" + raw.lstrip("/"))
    return os.path.join(ROOT, p.lstrip("/")), raw

def ok(cmd, ret):
    return struct.pack("<Ii", cmd, ret)

def fail(cmd, e):
    global last_errno
    last_errno = e
    return struct.pack("<Ii", cmd, -1)

# ---- listener 0xa: file service (opcodes from fs_msg.h: FILE_START = 0x201, then sequential)
def fs(b):
    global next_fd, last_errno
    cmd = struct.unpack_from("<I", b, 0)[0]
    try:
        if cmd in (0x202, 0x206):                       # open(path, flags) / creat(path, mode)
            p, raw = path_of(b, 4)
            v = struct.unpack_from("<i", b, 4 + NAME)[0]
            if cmd == 0x202:
                flags = v & (os.O_ACCMODE | os.O_CREAT | os.O_EXCL | os.O_TRUNC | os.O_APPEND)
                mode = 0o600
            else:
                flags, mode = os.O_WRONLY | os.O_CREAT | os.O_TRUNC, v & 0o777
            if flags & os.O_CREAT:
                os.makedirs(os.path.dirname(p), mode=0o700, exist_ok=True)
            fd = os.open(p, flags, mode)
            tfd = next_fd; next_fd += 1; fds[tfd] = fd
            if VERBOSE: log(f"  open {raw!r} flags {v:#o} -> fd {tfd}")
            return ok(cmd, tfd)
        if cmd == 0x207:                                # read(fd, count): data at +4, count at +4+DATA
            tfd, count = struct.unpack_from("<iI", b, 4)
            d = os.read(fds[tfd], min(count, DATA))
            return struct.pack("<I", cmd) + d.ljust(DATA, b"\0") + struct.pack("<i", len(d))
        if cmd == 0x208:                                # write(fd, buf[DATA], count)
            tfd = struct.unpack_from("<i", b, 4)[0]
            count = struct.unpack_from("<I", b, 8 + DATA)[0]
            return ok(cmd, os.write(fds[tfd], b[8:8 + min(count, DATA)]))
        if cmd == 0x209:                                # close(fd)
            tfd = struct.unpack_from("<i", b, 4)[0]
            os.close(fds.pop(tfd))
            return ok(cmd, 0)
        if cmd == 0x20a:                                # lseek(fd, offset, whence)
            tfd, off, wh = struct.unpack_from("<iii", b, 4)
            return ok(cmd, os.lseek(fds[tfd], off, wh))
        if cmd in (0x20c, 0x213):                       # unlink / remove
            p, raw = path_of(b, 4)
            os.remove(p)
            return ok(cmd, 0)
        if cmd == 0x210:                                # mkdir(path, mode)
            p, raw = path_of(b, 4)
            os.makedirs(p, mode=0o700, exist_ok=False)
            return ok(cmd, 0)
        if cmd == 0x211:                                # testdir(path): 0 = is a directory
            p, raw = path_of(b, 4)
            return ok(cmd, 0) if os.path.isdir(p) else fail(cmd, errno.ENOENT)
        if cmd == 0x216:                                # fsync(fd)
            tfd = struct.unpack_from("<i", b, 4)[0]
            os.fsync(fds[tfd])
            return ok(cmd, 0)
        if cmd == 0x217:                                # rename(old, new)
            p, raw = path_of(b, 4)
            q, rawq = path_of(b, 4 + NAME)
            os.replace(p, q)
            return ok(cmd, 0)
        if cmd == 0x21c:                                # get last errno
            return ok(cmd, last_errno)
        log(f"  fs op {cmd:#x}: not implemented")
        return fail(cmd, errno.ENOSYS)
    except OSError as e:
        return fail(cmd, e.errno or errno.EIO)
    except KeyError:
        return fail(cmd, errno.EBADF)

# ---- listener 0x7000: GP file service (op % 4: 0 read, 1 write, 2 delete, 3 rename; op 12 = version)
def gpfs(b):
    cmd = struct.unpack_from("<I", b, 0)[0]
    if cmd == 12:
        return struct.pack("<Iii", cmd, 0, 2)
    p, raw = path_of(b, 4)
    kind = cmd % 4
    try:
        if kind == 0:                                   # read(name, offset, length)
            off, ln = struct.unpack_from("<iI", b, 4 + NAME)
            with open(p, "rb") as f:
                f.seek(off)
                d = f.read(min(ln, GP_DATA))
            return struct.pack("<IiI", cmd, 0, len(d)) + d
        if kind == 1:                                   # write(name, offset, length, backup, data)
            off, ln, bak = struct.unpack_from("<iII", b, 4 + NAME)
            os.makedirs(os.path.dirname(p), mode=0o700, exist_ok=True)
            if bak and os.path.exists(p):
                with open(p, "rb") as s, open(p + ".bak", "wb") as d:
                    d.write(s.read())
            with open(p, "r+b" if os.path.exists(p) else "wb") as f:
                f.seek(off)
                f.write(b[NAME + 16:NAME + 16 + ln])
                f.flush(); os.fsync(f.fileno())
            return struct.pack("<IiI", cmd, 0, ln)
        if kind == 2:                                   # delete(name)
            if os.path.exists(p):
                os.remove(p)
            return struct.pack("<Ii", cmd, 0)
        if kind == 3:                                   # rename(old, new)
            q, rawq = path_of(b, 4 + NAME)
            os.replace(p, q)
            return struct.pack("<Ii", cmd, 0)
    except OSError as e:
        return struct.pack("<Ii", cmd, e.errno or errno.EIO)

SERVICES = {0x000a: fs, 0x7000: gpfs}

def main():
    os.makedirs(ROOT, mode=0o700, exist_ok=True)
    dev = os.open(DEV, os.O_RDWR)
    log(f"duo-qsee-listenerd: serving {', '.join(hex(i) for i in SERVICES)} under {ROOT}")
    while True:
        req = os.read(dev, 8 + 516096)
        lid, size = struct.unpack_from("<II", req, 0)
        b = req[8:8 + size]
        svc = SERVICES.get(lid)
        if svc is None:
            log(f"listener {lid:#x}: no service"); os.write(dev, struct.pack("<Ii", lid, -5)); continue
        rsp = svc(b)
        if VERBOSE or lid != 0xa:
            log(f"listener {lid:#x} op {struct.unpack_from('<I', b, 0)[0]:#x} -> {rsp[4:8].hex()}")
        os.write(dev, struct.pack("<Ii", lid, 0) + rsp)

if __name__ == "__main__":
    main()
