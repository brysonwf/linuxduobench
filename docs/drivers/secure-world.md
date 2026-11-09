# Workbench — the secure world (TrustZone / QSEE)

Chapter of the Linux workbench notes (hub: [../33-linux-workbench.md](../33-linux-workbench.md)).
the tester 2026-11-08: "build a driver for - secure world" (after: a locked bootloader does not give Hyper-V or the
fingerprint sensor - both live below what our key reaches).

## Research 2026-11-08 night (offline, while the phone was reset to stock): the RPMB listener
Why it matters: saving a finger needs a Gatekeeper token, Gatekeeper keeps its failure records in secure storage, and
even storing an EMPTY template DB failed "before any file I/O" - both point at QSEE's secure storage, whose
anti-rollback counters live in **RPMB**. Our listener 0x2000 (RPMB) registration was refused with -22.
- **The protocol is public now:** Qualcomm's own `qualcomm/minkipc` (BSD-3-Clause-Clear) has
  `listeners/librpmbservice` = the RPMB listener service (for the newer QCOM-TEE/CBO transport, same request format):
  service 0x2000, buffer TZ_CM_MAX_DATA_LEN 20000 + 40 (our 25600 is enough); commands **0x101 init** (-> num_sectors,
  rel_wr_count, v2 adds dev_type), **0x102 read**, **0x103 write** ({cmd_id, num_sectors, req_buff_len,
  req_buff_offset, version, rel_wr_count} -> {cmd_id, status, res_buff_len, res_buff_offset, version}), **0x104
  partition table** (v1 0x100 / v2 0x200). UFS backend (`rpmb_ufs.c`): the RPMB well-known LU through BSG,
  `/dev/bsg/0:0:0:49476` (W-LUN 0xC4), SCSI SECURITY PROTOCOL OUT/IN. TZ builds and MACs every RPMB frame - the normal
  world only carries them (no key in Linux: inside our scope rules).
- **Why registration may fail:** Android's downstream qseecom registers EVERY listener with the SMCInvoke variant
  (svc 2 / cmd **0x06**, `TZ_OS_REGISTER_LISTENER_SMCINVOKE_ID`) and falls back to the legacy cmd 0x01 only on -EIO;
  our patch 0004 uses cmd 0x01 only (fine for 0xa / 0x7000, refused for 0x2000). Downstream also turns the QSEE
  crypto clock (`CLK_QSEE`) on around every RPMB/SSD listener response (TZ uses the crypto engine for the frame MACs).
  Source: android.googlesource.com kernel/msm drivers/misc/qseecom.c + include/soc/qcom/qseecomi.h (read 2026-11-08).
- **Next on the phone (after the stock reset + Debian install):** (1) register 0x2000 with cmd 0x06 (fallback 0x01);
  (2) check `/dev/bsg/0:0:0:49476` exists (CONFIG_BLK_DEV_BSG + the UFS RPMB W-LUN); (3) RPMB service in
  duo-qsee-listenerd.py (4 commands, SG_IO v4 SECURITY PROTOCOL IN/OUT); (4) if responses fail: the crypto-engine
  clock vote; (5) then Gatekeeper enroll/verify -> FPC token -> end-enrol -> store DB. Note: the 2026-11-08 reset
  deleted the old Debian (its /lib/duo qsee test modules) - rebuild duo_qsee.ko + the 0003/0004 kernel from the repo.

## STOPPED HERE (2026-11-08 ~14:45 PC time) - read this first
**Done and verified on the phone (mainline 6.19 + our two pending qcom_scm patches + duo_qsee):**
the phone's own trusted apps load and start (cmnlib, cmnlib64, fpctzrel); the FPC1020 wakes (GPIO 95 reset) and
initialises; finger detection (IRQ GPIO 118) and image capture work; a full enrollment runs (countdown 19 -> 0 with
spoken guidance); QSEECOM listeners work and the storage daemon answers the fingerprint app's file requests
(open -> ENOENT via get-errno, the app then carries on).

**The blocker:** saving a finger. End-enrol returns -1 without a **Gatekeeper-signed auth token**, and storing even an
empty template DB ((2,12) + path) returns -1 before any file I/O. Both are policy/sequence checks inside the TA, not
transport problems - the transport (TA calls, listeners, daemon) is proven.

**Discovery done before stopping (so the blocker is real):** keymaster64 answers the documented version handshake
(0x200): **API 4.0, TA 4.558** - the same keymaster TA version as the Goodix prior-art phone, whose docs/06
describes the Gatekeeper CBOR protocol (handshake 0x200 + 0x207, enroll 0x21001 {3: uid, 8: password}, verify 0x21002
{3: uid, 4: challenge, 6: handle, 9: password} -> a 7-entry map that becomes hw_auth_token_t). Nothing was enrolled.

**Resume here (in order):**
1. Boot the test kernel 6.19.0-qsee-00088 (WSL tree /root/linux-gpufix branch duo/qsee-test = default series + the
   two pending patches; modules already in Debian /lib/duo/6.19.0-qsee-00088-g68cbee31c4db) as a one-shot, then
   `insmod duo_qsee.ko app=fpctzrel listeners=0xa,0x7000` + `duo-qsee-listenerd.py -v`, sensor reset + init.
2. Gatekeeper with a **Linux-owned** credential: handshake 0x200/0x207, enroll a test PIN for an unused uid (watch
   for a call to the RPMB listener 0x2000, whose registration was refused with -22 - find out why if needed).
3. FPC (3,2) challenge -> Gatekeeper verify with that challenge -> token -> FPC (3,3) -> begin -> samples -> end
   (expect a template id) -> store DB (2,12) through the daemon -> reboot -> load DB (2,11) -> identify.
4. Then a fingerprint service: libfprint driver (compare wrobelda's goodix-qsee driver) + fprintd + PAM; package
   the daemon + module; decide kernel side upstream route (our qcom_scm patches vs a TEE-subsystem backend).
**Scope rules for the resume:** no TrustZone key material in the normal world; no deriving/recovering anyone's
credentials; no skipping the enroll authorization (details in "Scope boundaries" below).

**Files:** kernel patches tools/linux/kernel/pending/0003 (image loading) + 0004 (listeners);
tools/linux/drivers/duo-qsee/ (duo_qsee.c module, qsee-cmd.py, fp-step.sh, duo-qsee-listenerd.py, README.md).
Phone left clean: module unloaded, daemon stopped, test files off the ESP, back on the default kernel 00086.

## What mainline 6.19 has (read 2026-11-08)
- `qcom_scm` finds QSEECOM on the Duo at every boot: **"qseecom: found qseecom with version 0x1400000"**, then
  "untested machine, skipping" - the QSEECOM platform device is created only for machines on
  `qcom_scm_qseecom_allowlist` (laptops + Microsoft arcata/blackrock/romulus; matched on the root compatible, which ABL
  replaces with `qcom,sm8150-mtp` on the ABL boot path). "SHM Bridge not supported" (TZ on SM8150 predates it).
- In-kernel QSEECOM clients: only `qcom_qseecom_uefisecapp` (EFI variables). No userspace interface (by design), no
  trusted-app LOADING - only lookups of apps something else already loaded (`qcom_scm_qseecom_app_get_id`, exported).
- `qcomtee` (object-based TEE, /dev/tee, smcinvoke) is in the tree, CONFIG_QCOMTEE off; qcom_scm probes smcinvoke
  support itself when it is on - untested on SM8150.
- Trusted apps on the phone's modem_a (image/*.mdt): cmnlib, cmnlib64 (TA common libraries), **fpctzrel** (fingerprint),
  hdcp1, hdcp2p2, hdcpsrm, spss1p/spss2p (secure processor), voicepri.

## Step 1 - which TAs are loaded (2026-11-08, tools/linux/drivers/duo-qsee, read-only lookups)
On Debian booted through the UEFI (kernel 00086), `insmod duo_qsee.ko`, all answers within 2 ms, module removed:
- **keymaster64 LOADED (app id 1)** - started by the boot chain before Linux: the hardware key store is reachable.
- **qcom.tz.uefisecapp LOADED (app id 2)** - the UEFI's variable store app (loaded by the UEFI we boot through; on the
  ABL path probably absent).
- not loaded: fpctzrel / fpctzapp / fingerpr, keymaster(32), km4, gatekeeper, cmnlib, cmnlib64, spss, hdcp*,
  widevine, voicepri, smplap*, gptest, secotaap.

## Step 2 - the loader WORKS (2026-11-08 ~12:55 PC time)
Kernel patch `firmware: qcom: scm: add QSEECOM image loading` (tools/linux/kernel/pending/0003-...; test kernel
6.19.0-qsee-00087): qcom_scm_qseecom_services_load() / _app_start() / _app_shutdown() = QSEE OS app manager commands
7 / 1 / 2 (owner 50, svc 1; args mdt_len, img_len, phys of a dma_alloc_coherent buffer = .mdt + .b00..bNN
concatenated, freed after the call). No region notification needed: the boot chain did it (keymaster64 runs).
duo_qsee.ko (services= / app=, firmware from the phone's own modem_a):
- `services=cmnlib,cmnlib64`: cmnlib (6 segments, mdt 20228 + 411058 bytes) **loaded**, cmnlib64 (525238) **loaded**.
- `app=fpctzrel` (8 segments, 744860 bytes): **STARTED, app id 3** (a lookup by the file name finds nothing - the
  TA registers under its own internal name). No WARN (the qseecom call path warns on INCOMPLETE/BLOCKED results),
  duo-health 0 FAIL afterwards.
=> Mainline Linux loads and starts the phone's own trusted applications. Next for fingerprint: FPC's command
protocol - the Android side is vendor_a lib64/com.fingerprints.extension@1.0.so + vendor.qti.hardware.fingerprint@1.0.so
(+ the HAL in lib64/hw, to find): read the request/response layouts they send through qseecom (QSEECOM_IOCTL_SEND_CMD),
then send them with qcom_scm_qseecom_app_send(app id 3, ...). Also needed: the sensor's reset/IRQ GPIOs (TLMM 95/118)
and its SPI being owned by TZ - the TA drives the sensor, Linux only toggles reset and waits for the IRQ.

## Step 3 - the fingerprint TA ANSWERS (2026-11-08 ~13:05 PC time)
The command protocol, read from the Android HAL's code (vendor bin/hw/android.hardware.biometrics.fingerprint@2.1-service,
stripped; the TAC = trusted-application client is inside it; copies only in the job folder):
- every command goes through fpc_tac_transfer -> QSEECom_send_modified_cmd: a **64-byte request {u32 shared_size
  (page-rounded), u32 shared_phys (patched in at offset 4)}**, a 64-byte response whose first u32 = transfer status.
- the TA command lives in the **shared buffer** (Android: ION heap 27 = QSECOM, page-sized): `u32 group, u32 cmd,
  i32 status (out), ...`; build info = group **0x0c**, cmd **0x01**, u32 size 1024 at +16, text out at +20 (buffer
  1044 bytes, fpc_tee_get_build_info at 0x7444, the 8-byte header constant at .rodata 0x3560).
duo_qsee `app=fpctzrel fpc_info=1` (tzmem pool buffers, below 4 GiB): **send 0, transfer status 0, TA status 0** and
the TA's build string (version hashes of fpc_bio, prodtestlib, fpc_c_core, fpc_lib - not reproduced here). App
shutdown (id 3 -> 0) and restart (id 4) work. Normal kernel memory is fine for the shared buffer (no QSECOM carve-out).
Next: the sensor side - the TA drives the FPC1020 over TZ-owned SPI; Linux owns reset (TLMM 95) + IRQ (TLMM 118) +
vio (downstream fpc1020 sysfs: hw_reset, irq, wakeup_enable - the HAL's fpc_irq_wait/fpc_sysfs_node_write). Then the
TA's init / capture / enroll / identify commands (fpc_tac open, fpc_tee_* in the HAL) and the template DB storage
(fpc_tee_load/store_template_db) - all read from the same HAL code.

### Command catalogue so far (8-byte header constants in the HAL, job script fpc-cmds.py)
| group | cmd | sent from (nearest named function) | meaning (so far) |
|---|---|---|---|
| 0x0c | 0x01 | fpc_tee_get_build_info | build info - VERIFIED on the phone |
| 0x03 | 0x01, 0x02, 0x04, 0x05 | fpc_tac_open (+ the next unnamed one) | TA session setup / init |
| 0x0b | 0x00-0x04, 0x06-0x0a | small unlogged functions before fpc_tee_store_template_db | template DB + sensor operations |
| 0x205 | 0x02 | the 2nd app path (QSEECom_start_app + plain send_cmd) | keymaster64: KEYMASTER_GET_AUTH_TOKEN_KEY |
Capture / enroll / identify headers are not 8-byte constants (built with immediates) - next round of reading.

### Live probing (2026-11-08 ~13:20, duo_qsee debugfs `cmd` + tools/linux/drivers/duo-qsee/qsee-cmd.py)
All 20 fpc_tac_transfer call sites mapped (job script fpc-calls.py). Results from the TA, no init done:
- (0x0c,1) build info: status 0 (+12 = 2, +16 = text length 666).
- (3,2): status 0, a random u64 at +8 = a **challenge** (pre-enroll).
- (0x0b,7) list templates, max 5 at +12: status 0, **count 0** (max 5 fingers; ids follow at +16).
- (0x0b,0x0a): status 0, a random **u64 authenticator id** at +16.
- (0x0b,1): status **-11 = FPC_ERROR_NOT_INITIALIZED** -> sensor/library init comes first.
Group 0x0a = KPI (performance counters: cmds 0,1,3,4,5 from the code around clock_gettime / fpc_tee_kpi_*),
group 2 = string commands (len at +12, text at +16; dispatcher 0x9860), group 0x0b = the biometric/template API
(0 = no-arg action, 1-4 = u32 getters, 7 = list, 8/9 = u32 setters, 0x0a = authenticator id).
FPC error codes (HAL table at 0xb168): -1 INPUT, -2 TIMEDOUT, -3 ALLOC, -4 COMM, -5 NOSPACE, -6 IO, -7 CANCELLED,
-8 NOENTITY, -9 HARDWARE, -10 CONFIG, -11 NOT_INITIALIZED, -12 RESET_HARDWARE, -13 PN, -16 CRYPTO, -17 DB,
-18 TEMPLATE_CORRUPTED.
Sensor GPIOs from Microsoft's dtbo (fpc1020 node): reset TLMM 95 (assert low / deassert high), IRQ TLMM 118 (input,
pull-down, active high, wakeup), vio = pm8150_s4 (1.8 V, always on in mainline). Both lines free on gpiochip3
(only 126-129 are reserved) -> userspace (libgpiod) can drive them, no kernel driver needed.
Correction: group 9 = KPI (fpc_tee_kpi_ctrl, variable cmd + blob); **group 0x0a = `sensor_command`** (dispatcher
0x865c, its log name; status at +8; on -12 RESET_HARDWARE it waits 10 ms and retries once, then 20 ms -> -9). Callers:
cmd 0 right after a mutex init (= **sensor init**), 1, 3 (right after clock_gettime = capture?), 4, 5 (cleanup paths =
deinit/sleep). The HAL also writes the downstream fpc1020 sysfs nodes `enable` / `wakeup_enable` (power + reset).

### THE SENSOR IS ALIVE (2026-11-08 ~13:30)
- control: sensor init (0x0a,0) with GPIO 95 untouched -> **-12 RESET_HARDWARE** (sensor held in reset).
- reset pulse (gpioset -c gpiochip3 95=0, 10 ms, then `gpioset -z 95=1` held in the background, like the fpc1020
  driver's enable): the sensor raised its IRQ (GPIO 118 active), sensor init -> **status 0**, IRQ cleared afterwards.
=> Linux GPIO reset -> TrustZone SPI -> FPC1020 -> TA init -> IRQ back to Linux: the whole hardware chain works.
- (0x0b,1) still -11 NOT_INITIALIZED: the biometric library above the sensor needs its template DB loaded
  (HAL: "/user.db", "%s/%s/%s", "failed to load db, corrupt?, load empty db." -> fpc_tee_load_template_db).
Next: the load-DB / load-empty-DB command (group 3 blob cmds 3/4 or group 2 strings), then enroll + identify.

### Template DB + FINGER DETECTION (2026-11-08 ~13:40, the tester touching the pad)
- DB flow (HAL 0x9900): stat(<dir>/user.db); missing/empty -> **(0x0b,6) load empty DB** (status 0 on the phone);
  present -> group-2 string cmd 11 with the PATH: the TA reads the file itself through a QSEECOM **listener** (Android's
  qseecomd file service) - mainline has no listener support yet -> persistent templates need one (later).
- After the empty DB: (0x0b,1) -11, (0x0b,2) -3, (0x0b,3) -6, (0x0b,4) 0 - consistent with source order begin/enrol/
  end/identify needing a session or a captured image.
- **Finger detection works:** sensor_command 3 = ARM (0), the finger raises IRQ 118 ~0.2 s later; 39 detections in 45 s.
  sensor_command 4 = **finger present?** -> 0 while touching, 1 after lift / with no finger. sensor_command 1 left the
  sensor out of detect mode in the first test (returns 1 without a finger) - capture candidate.
- Pad = the separate fingerprint pad on the right edge of the Duo 1 (not the power button).

### ENROLLMENT PROGRESSES (2026-11-08 ~13:55, the tester touching)
HAL capture routine (0x873c) corrected the map: **sensor 3 = arm / wait for finger** (the HAL also sets a flag byte
at +12 = 1 and sends sensor 1 first via 0x85ec - with only the flag set the IRQ never fired: keep it 0 for now),
**sensor 4 = CAPTURE** (0 = image taken, 3 = retry, up to 4x; +16 = a quality/coverage value), **sensor 1 = finger
present?** (0 finger / 1 none), sensor 5 = cleanup on error. Enroll loop (do_enroll 0x66c0): (3,3) send blob (= the
Gatekeeper hardware auth token) -> (0x0b,0) begin -> per touch capture + **(0x0b,1) enrol** -> (0x0b,2) end ->
store DB / authenticator id.
Run 3 (empty DB, gid 0, begin, then per touch arm 3 -> IRQ -> capture 4 -> enrol 1, lift = sensor 1): every capture
0; enrol returns **status + a countdown at +12**: 19 -> 18 -> 17 -> 16 -> 15 -> 14 -> 13 ... -> 11 after 18 touches.
Status **1 = accepted**, **5 = accepted** (counts down too), **6 = rejected, same area** (countdown held at 13 for six
touches). -> a full enrollment needs ~19 accepted samples from different spots of the finger (no auth token was
needed for the samples; whether end-enrol needs it shows when the countdown reaches 0).

### FULL ENROLLMENT RUN (2026-11-08 ~13:50 PC time, spoken guidance: tools/linux/drivers/duo-qsee/fp-step.sh +
a PC loop that only speaks the phone's `SAY` line)
Countdown **19 -> 0** in 25 touches (status 1/5 accepted, 6 = same spot, final status **0 = complete**). Then
**end enrol (0x0b,2) -> -1 INPUT**, list = 0, identify -> -8 NOENTITY / -6 -> the template is not committed.
Cause (most likely): no **hardware auth token**. Android's do_enroll first sends (3,3) = the HAT blob (Gatekeeper's
proof of the PIN for the (3,2) challenge). The HAL also fetches the HAT HMAC key from keymaster64
(KEYMASTER_GET_AUTH_TOKEN_KEY, its 2nd app path, a034-a154) and hands it to the FPC TA (likely (3,5)).
Next: (1) read the keymaster request (group 0x205 cmd 2) + the (3,5) layout from the HAL; (2) fetch the key from the
already-running keymaster64 (app id 1); (3) build hw_auth_token_t {version 0, challenge (from (3,2)), user_id,
authenticator_id, authenticator_type = FINGERPRINT|PASSWORD, timestamp, HMAC-SHA256} with it; (4) (3,3) before begin
enrol; then end enrol -> template id, identify. Then: template DB persistence (needs a QSEECOM listener or the TA's
encrypted-blob export) and a fingerprint daemon (fprintd driver).
Earlier enrol attempts also showed: never call enrol again after status 0 - end enrol at once.

### Auth token: the gatekeeper route (2026-11-08 ~14:05)
- keymaster64 (app 1) answers KEYMASTER_GET_AUTH_TOKEN_KEY ({0x205, 2}) with status 0 and a 152-byte blob - the
  Android HAL hands that blob unchanged to the FPC TA ((3,5): len +12, data +16). **Decision: our code never
  extracts, inspects or signs with that key** (a probe of it was stopped; an HMAC-signing script was deleted).
  Tokens must come from TrustZone itself, like on Android: Gatekeeper.
- Gatekeeper = vendor lib64/hw/android.hardware.gatekeeper@1.0-impl-qti.so (symbols kept): no own TA - it talks to
  **keymaster64** via KeymasterUtils::send_cmd (libkeymasterdeviceutils.so), 40 KiB request buffer. Two protocols:
  legacy fixed structs (isLegacy) and QCBOR-serialized (serializeClientEnroll/Verify, deserializeClientVerify ->
  hw_auth_token_t).
  - legacy **enroll** cmd **0x1001** (0x1501 when a version field == 2): {cmd, uid, (off,len) current handle,
    (off,len) current password, (off,len) desired password}; response = new password handle.
  - legacy **verify** cmd **0x1002** (0x1502): {cmd, uid, u64 challenge @+8, (off,len) handle @+16, (off,len)
    password @+24}; response = the HAT (copied out by km_memscpy) - signed inside TrustZone.
- Chain: GK enroll (PIN) -> handle; FPC (3,2) challenge -> GK verify(challenge) -> HAT -> FPC (3,3) -> begin ->
  samples -> end -> template id.
- **Caution:** Gatekeeper keeps throttling/failure records in secure storage (RPMB/SFS) through a QSEECOM
  **listener** - mainline has none (it WARNs on BLOCKED_ON_LISTENER). A GK call could stall in keymaster64 (the app
  the boot chain uses). Order: (1) QSEECOM listener support in our qcom_scm patch (also needed to persist the FPC
  template DB: its load/store go through file paths = a file listener), (2) gatekeeper enroll/verify, (3) full
  enrollment + identify, (4) an fprintd driver / PAM login.

### QSEECOM listeners - protocol + the phone's table (2026-11-08 ~14:10)
Protocol (GPL downstream qseecomi.h): register = owner 50 (QSEE OS), svc 2 (TZ_SVC_LISTENER), cmd 1, args
(listener_id, shared-buffer phys [BUF_RW], length); deregister cmd 2. A TA call that needs a listener returns
result INCOMPLETE (1) with resp_type QSEOS_LISTENER_ID (0xEE02) and data = listener id; the request is then in that
listener's shared buffer -> service it -> LISTENER_RESPONSE_HANDLER cmd 3 (listener_id, status) -> which may return
INCOMPLETE again (loop) or SUCCESS (the TA's real answer). BLOCKED_ON_LISTENER (2) = that listener is busy ->
CONTINUE_BLOCKED_REQUEST cmd 4 (app id) later.
Android registers them all from `qseecomd` (vendor bin; table in its .data, job script qseecomd-table.py):
| id | service | library | shared buffer |
|---|---|---|---|
| 0x2000 | RPMB (secure storage: gatekeeper/keymaster counters) | librpmb.so | 25600 |
| 0x3000 | SSD | libssd.so | 20480 |
| 0x4000 | secure UI | libsecureui.so | |
| 0x000b | time | libdrmtime.so | 20480 |
| 0x000a | file system (TA files - the FPC user.db) | libdrmfs.so (fs_start) | 20480 |
| 0x7000 | GP file system | libdrmfs.so (gpfs_start) | 516096 |
| 0x9000 | interrupts | libqisl.so | |
| 0xb000 | secure processor | libspl.so | |
| 0x1100 | GP request cancellation | libGPreqcancel.so | |
| 0xc000 | output protection | libops.so | |
Plan: kernel side = listener registration + the INCOMPLETE/response loop in qcom_scm (handlers registered by a
client driver), duo_qsee exposes each registered listener to userspace (char device: read request / write
response); userspace = a small daemon that implements 0x000a (files under /var/lib/duo/qsee/) and 0x2000 (RPMB via
the UFS RPMB well-known LUN, SCSI SECURITY PROTOCOL IN/OUT). First a LOGGING daemon (answers "failure") to learn
which listener each TA call hits and the request formats, before implementing any storage.

### Listeners WORK (2026-11-08 ~14:15, test kernel 6.19.0-qsee-00088)
Kernel patch `firmware: qcom: scm: add QSEECOM listener support` (pending/0004): register/unregister + the
INCOMPLETE -> handler -> listener-response loop inside the QSEECOM call. duo_qsee `listeners=0xa,0x2000,0x7000,0xb`
(logging handler, answers failure):
- registered: **0xa files OK, 0x7000 GP files OK, 0xb time OK; 0x2000 RPMB refused (-22)** (maybe the boot chain
  owns RPMB, or it needs another setup step - gatekeeper will show whether it is needed).
- FPC (2,11) "load DB from path /data/vendor/fpc/0/user.db": the TA called **listener 0xa** with
  `02 02 00 00` + the path (fs command 0x0202) -> our failure answer -> the TA finished cleanly (status 8). No WARN.
=> Linux can serve TrustZone's storage callbacks. Next: the fs-service command set (0x02xx: open/read/write/close/
...) from vendor libdrmfs.so, implemented in a userspace daemon behind a duo_qsee char device (files under
/var/lib/duo/qsee/), then the template DB persists and gatekeeper can be tried.
Next: per touch arm(3) -> IRQ -> cmd 1 (capture?) -> (0x0b,1) enrol; end with (0x0b,2) -> template id; then identify
(arm -> IRQ -> capture -> (0x0b,3)).

### Prior art + official definitions (2026-11-08 ~14:30)
- **wrobelda/goodix-fp-spi-linux** (GPL-2.0-only, github.com/wrobelda/goodix-fp-spi-linux): the same problem for a
  Goodix QSEE fingerprint TA on mainline 7.1 - a TEE-subsystem QSEECOM backend (drivers/tee/qseecom), a userspace
  listener daemon (wrobelda/qsee-supplicant: file service 10 + GP files 28672) and a libfprint driver (enroll /
  identify / delete working). Its docs/03-listener-services.md documents both file protocols; docs/06 the Qualcomm
  Gatekeeper-in-keymaster64 CBOR protocol (version handshake 0x200/0x207, enroll 0x21001, verify 0x21002).
  Reusing its kernel backend (TEE subsystem, mainline-style) instead of growing our qcom_scm patches is worth a look
  before upstreaming anything.
- **Qualcomm's own open-source listener services**: github.com/qualcomm/minkipc listeners/ (BSD-3-Clause-Clear):
  libfsservice/fs/fs_msg.h (FILE_SERVICE_ID 0xa, 20 KiB buffer, TZ_FS_MSG_CMD_* from 0x201: open 0x202, read 0x207,
  write 0x208, close 0x209, lseek 0x20a, mkdir 0x210, rename 0x217, get-errno 0x21c ...), gpfs/gpfs_msg.h
  (GPFILE_SERVICE_ID 0x7000, op%4 = read/write/delete/rename, op 12 = version), plus time 11 and RPMB 8192.
- Our listener-0xa call (`02 02 00 00` + "/data/vendor/fpc/0/user.db") = **FILE_OPEN** of the FPC template DB.

### Listener daemon (built 2026-11-08 ~14:35, NOT yet run on the phone)
- duo_qsee: `/dev/duo_qsee_listener` (misc, root, one daemon): read() = {u32 listener id, u32 buffer size, buffer},
  write() = {u32 id, s32 status, reply}; the handler waits <= 10 s, answers failure without a daemon -> a TA call
  can never hang on us.
- tools/linux/drivers/duo-qsee/duo-qsee-listenerd.py: file service 0xa (open/creat/read/write/close/lseek/unlink/
  remove/mkdir/testdir/fsync/rename/get-errno; real fds, raw -1 + separate errno like the TA expects) and GP files
  0x7000 (read/write(+.bak)/delete/rename/version 2), every path rooted under /var/lib/duo/qsee (no escaping).
- Test plan: kernel -qsee-00088 + duo_qsee listeners=0xa,0x7000 + the daemon -> FPC (2,11) load DB (expect open ->
  ENOENT via get-errno -> the TA falls back to an empty DB), a full enrollment, then the DB written by the TA under
  /var/lib/duo/qsee/data/vendor/fpc/0/.

### Scope boundaries (decided 2026-11-08)
- Never extract, inspect or use TrustZone key material in the normal world (the HAT key blob from keymaster stays
  opaque; an HMAC-signing script was deleted).
- No deriving or recovering anyone's credentials, and no skipping the enroll authorization: a signed token must come
  from Gatekeeper with a credential the Linux user sets up (a Linux-owned PIN enrolled through Gatekeeper's normal
  enroll/verify). Until then fingerprints can be detected/captured/enrolled-in-RAM but not committed.

### Status summary (where to pick up)
| piece | state |
|---|---|
| TA loading (qcom_scm patch pending/0003) | WORKS: cmnlib, cmnlib64, fpctzrel start; build info answered |
| sensor (GPIO 95 reset, IRQ 118) | WORKS: init, finger detect (39/45 s), capture |
| enrollment samples | WORKS: countdown 19 -> 0 with spoken guidance (fp-step.sh) |
| commit template (end enrol) | BLOCKED: needs a Gatekeeper-signed token (-1 without) |
| listeners (qcom_scm patch pending/0004) | WORKS: register + serve loop; FPC's DB open reached our handler |
| listener daemon (file + GP files) | WORKS for the read path (open -> ENOENT -> the TA carries on); write path untested (store needs a committed template) |
| RPMB listener 0x2000 | registration refused (-22) - check whether Gatekeeper needs it |
| Gatekeeper (in keymaster64) | keymaster64 answers the version handshake: API 4.0, TA 4.558 (= the Goodix prior-art phone's) -> its CBOR Gatekeeper protocol applies; nothing enrolled yet |
| fprintd / PAM | not started |

## Plan
1. DONE: lookup probe.
2. **TA loader** (the missing piece for fingerprint and anything Android loads at runtime): load cmnlib/cmnlib64 then a
   TA from the phone's own modem_a (QSEOS_LOAD_SERV_IMAGE_COMMAND, QSEOS_APP_START_COMMAND with the split .mdt/.bNN
   ELF in TZ-reachable memory) - port the minimal part of the GPL downstream msm-4.14 qseecom.c onto mainline's
   qcom_scm/qcom_tzmem; a char device for userspace send/receive (like downstream /dev/qseecom, but only for TAs
   from the phone's own firmware).
3. keymaster64: talk to the already-loaded app (its command protocol = the Android keymaster TA ABI) - hardware-bound
   keys for LUKS (the installer's encryption) / verified-boot state.
4. Fingerprint: fpctzrel's command protocol is FPC's (proprietary, undocumented) - needs reverse engineering of the
   Android HAL; step 2 is the prerequisite either way. Hyper-V/EL2 stays a dead end (hyp is fused-key firmware).
