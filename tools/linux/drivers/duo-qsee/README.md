# duo-qsee - Surface Duo secure-world (QSEE / TrustZone) tools

Work in progress. Full notes, protocol tables and the current stopping point:
`secure-world.md` (in the public release: `docs/drivers/secure-world.md`; read "STOPPED HERE" first).

Needs a kernel with the two pending `qcom_scm` patches (`tools/linux/kernel/pending/0003-*` image loading,
`0004-*` listeners). Everything the trusted applications need comes from the phone's own firmware partitions
(modem_a, linked under `/lib/firmware/qcom/sm8150/microsoft/` at boot) - nothing proprietary is shipped here.

| file | what |
|---|---|
| `duo_qsee.c` + `Makefile` | kernel module: lists loaded TAs; `services=cmnlib,cmnlib64` loads the TA libraries, `app=fpctzrel` starts a TA, `fpc_info=1` asks the fingerprint TA for its build info, `listeners=0xa,0x7000` registers QSEECOM listeners; debugfs `duo_qsee/cmd` (shared-buffer commands to the started TA) and `duo_qsee/raw` (plain request/response to any loaded TA); `/dev/duo_qsee_listener` for the listener daemon |
| `qsee-cmd.py` | send one shared-buffer command to the started TA (`qsee-cmd.py 0x0c 0x01 0 0 1024`) |
| `fp-step.sh` | one fingerprint step (`begin` / `touch` / `end` / `identify`) for a PC loop that speaks the phone's `SAY` lines |
| `duo-qsee-listenerd.py` | userspace listener daemon: file service 0xa + GP file service 0x7000, rooted under `/var/lib/duo/qsee` |

Build (WSL): `make -C <kernel build dir> ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- M=$PWD modules`

Typical session (root, on the phone):

    insmod duo_qsee.ko app=fpctzrel listeners=0xa,0x7000
    python3 duo-qsee-listenerd.py -v &
    gpioset -c gpiochip3 -t 0 95=0; gpioset -c gpiochip3 -z 95=1     # sensor out of reset (pulse, then hold)
    python3 qsee-cmd.py 0x0a 0                                        # FPC sensor init -> status 0
    python3 qsee-cmd.py 0x0c 0x01 0 0 1024                            # build info

Safety: the module's listener handler answers "failure" after 10 s without a daemon, so a TA call cannot hang;
`rmmod duo_qsee` shuts the started TA down and unregisters the listeners. TrustZone key material never leaves the
secure world through these tools, and they do not derive or bypass credentials.

Prior art: wrobelda/goodix-fp-spi-linux + wrobelda/qsee-supplicant (Goodix QSEE fingerprint on mainline);
Qualcomm's listener definitions in qualcomm/minkipc (listeners/libfsservice).
