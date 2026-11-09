# Workbench — IPA v4.1 (SM8150 modem data path) port

## SHIPPED (2026-11-07 evening): default kernel 6.19.0-00065-ge5f1531dd893
Four series commits (driver v4.1 data + registers incl. ENDP_INIT_CTRL, the qcom,sm8150-ipa binding, the board DT
node, QCOM_IPA=m). pmOS `duo-modem` links the GSI firmware from vendor_a and loads ipa.ko BEFORE starting the modem;
the veth stand-in is only used without IPA; ModemManager is started with retries + a scan once the modem is in
low-power. Result on a plain pmOS boot: "IPA driver setup completed successfully", modem starting/running events,
`rmnet_ipa0`, ModemManager modem with ports `qrtr0 (qmi), rmnet_ipa0 (net)`, state failed/sim-missing (no SIM yet).
duo-health checks both (0 FAIL). Next: insert the SIM -> `mmcli -m 0 --simple-connect="apn=<carrier apn>"` (or the
Phosh mobile-data toggle), then calls + SMS. The RAM workbench does not load IPA (pmOS only).

## FIRST LOAD (2026-11-07 08:54, the tester at the desk): IPA PROBES, rmnet_ipa0 EXISTS
- Test kernel #87 (QCOM_IPA=m) + test-dtb-ipa-default + ipa.ko via duo-ipa-try.sh / duo-ipa-load.sh (from the RAM
  workbench: pmOS has no scp). Firmware = the phone's own ipa_fws from vendor_a. Result: "IPA driver initialized",
  "IPA driver setup completed successfully", netdev **rmnet_ipa0**. Kernel log: (local file, not published)
- WARNING reg.h:87 in ipa_endpoint_init_ctrl (x2): v4.2 dropped ENDP_INIT_CTRL, v4.1 has it (ENDP_DELAY, 0x800 stride
  0x70 like v3.5.1) -> ipa-v4.1.py now adds it to reg/ipa_reg-v4.1.c (rebuilt ipa.ko, not yet loaded).
- "unexpected init_completed response": the modem was up (low-power) before IPA; Android brings IPA up first.
- ~90 s after the load the phone needed a reset (the tester: "rebooted"); the log stream had stopped 3 s after insmod and
  pstore was too decayed (688 unrecoverable ECC blocks) -> cause unknown. NEXT RUN: keep /proc/kmsg streaming for
  minutes, load the fixed ipa.ko, and try IPA before the modem (duo.nolpm / modem start after insmod).
- **Run 2 (09:05):** IPA_ARGS=duo.wifi=0 (init leaves the modem off), MODEM_AFTER=1: no reg.h warning (ENDP_INIT_CTRL fix
  works), IPA got "modem starting" + "modem running" events in order - but the modem crashed 6 s later:
  WCSS_WLAN1_GDSCR.PWR_ON = the 11-04 WLAN board-data crash (duo.wifi=0 also skipped init's board-2.bin removal). Not IPA.
- **Run 3 (09:07): CLEAN** - same + board-2.bin removed before the modem start: IPA setup, modem starting/running events,
  modem up, low-power OK, rmnet_ipa0 + wlan0, no warnings, no crash. Log kmsg-090754.log. **STABLE: soak 30..227 s
  uptime, modem running, 0 crashes** (run 1's reset ~90 s after insmod is gone with the two fixes).
- No SIM in the phone (UIM slot 1 'no-atr-received'): an actual data session needs a SIM (or a usable eSIM profile).
- TO SHIP: default kernel with IPA (=m, the v4.1 port + ENDP_INIT_CTRL fix) + the IPA DT node in the default DTB, and
  the modem service loading ipa.ko BEFORE the modem start (+ ModemManager on the real rmnet_ipa0 instead of the veth
  stand-in). One-shot pmOS test boot first.

Why: ModemManager drops the modem without an IPA net port ("Failed to find a net port in the QMI modem"; GPS works
around it with a veth stand-in) and mobile data needs IPA. The SM8150's IPA is **v4.1** (downstream DT
`qcom,ipa-hw-ver = <0x0f>`, IPA_HW_v4_1 = 15). Mainline (6.19) has no v4.1 data file or compatible.

## What mainline already has (2026-11-06, /root/linux/drivers/net/ipa)
- `enum ipa_version` includes IPA_VERSION_4_0 / 4_1 (GSI 2.0); ~34 version checks already treat 4.0/4.1.
- GSI registers for GSI 2.0: `gsi_reg-v4.0.c` (used by 4.2 today) -> map IPA_VERSION_4_1 to it in `gsi_regs()`.
- IPA registers: no v4.1 table; nearest = `ipa_reg-v4.2.c` (v4.2 = sc7180, a reduced v4.1 derivative). Check the
  places that special-case 4.2 (ipa_reg.c lines ~20/23/57) before mapping 4.1 to it.
- Missing: `data/ipa_data-v4.1.c`, `ipa_data_v4_1` in ipa_data.h, the compatible in ipa_main.c, a DT node.

## Downstream tables (Pixel 4 "coral" = SM8150, msm-4.14 ipa_v3/ipa_utils.c, IPA_4_1)
GSI tuple = {IPA endpoint, GSI channel, TLV, AOS}, owner EE: AP / Q6 (modem) / UC (microcontroller).

| Client | ep | ch | tlv | aos | EE | seq |
|---|---|---|---|---|---|---|
| APPS_CMD_PROD (AP command TX) | 5 | 4 | 20 | 24 | AP | DMA_ONLY |
| APPS_LAN_CONS (AP LAN RX) | 10 | 5 | 9 | 9 | AP | - |
| APPS_WAN_PROD (AP -> modem TX) | 2 | 3 | 16 | 32 | AP | REP_2PKT_PROC_PASS_NO_DEC_UCP_DMAP |
| APPS_WAN_CONS (modem -> AP RX) | 11 | 6 | 9 | 9 | AP | - |
| Q6_CMD_PROD (modem command TX) | 4 | 1 | 20 | 24 | Q6 | PKT_PROCESS_NO_DEC_UCP |
| Q6_LAN_CONS (modem LAN RX) | 14 | 4 | 9 | 9 | Q6 | - |
| Q6_WAN_PROD (modem -> AP TX) | 3 | 0 | 16 | 32 | Q6 | 2ND_PKT_PROCESS_PASS_NO_DEC_UCP |
| Q6_WAN_CONS (AP -> modem RX) | 13 | 3 | 9 | 9 | Q6 | - |
| Q6_LTE_WIFI_AGGR_CONS | 16 | 5 | 9 | 9 | Q6 | - |
| APPS_LAN_PROD | 8 | 10 | 8 | 16 | AP | 2ND_PKT_PROCESS_PASS_NO_DEC_UCP |
(USB/WLAN/ODU/ETHERNET/test clients exist too; not needed for the modem data path.)

Resource groups (source: LWA_DL, UL_DL, unused, UC_RX_Q): PKT_CONTEXTS {1,63}{1,63}{0,0}{1,63};
SRS_DESCRIPTOR_LISTS {10,10}{10,10}{0,0}{8,8}; SRC_DESCRIPTOR_BUFF {12,12}{14,14}{0,0}{8,8};
SRC_HPS_DMARS {0,63}x4; SRC_ACK_ENTRIES {14,14}{20,20}{0,0}{14,14}.
Destination (LWA_DL, UL/DL/DPL, uC, ...): DATA_SECTORS {4,4}{4,4}{3,3}{2,2}; DPS_DMARS {2,63}{1,63}{1,2}{0,2}.
RX_HPS_CMDQ {3,3}{7,7}{0,0}{2,2}; HPS weights {1,1,1,1}. QMB outstanding: DDR reads 12 / writes 8, PCIE 12 / 4.

## Downstream DT (droidian.dtb, SM8150 v2) node `qcom,ipa@1e00000`
reg 0x1e00000/0x34000 (ipa-base) + 0x1e04000/0x28000 (gsi-base); interrupts SPI 0x137 (ipa) + 0x1b0 (gsi);
ipa-hw-mode 0, ee 0, arm-smmu, 64-bit DMA, ipa-wdi2, modem-cfg-emb-pipe-flt, msm-bus "ipa" votes (5 cases).

## The port (2026-11-06 evening) - drafted, compile-tested, NOT booted
Files: `tools/linux/patches/ipa-v4.1/` (`ipa_data-v4.1.c` + idempotent `ipa-v4.1.py`), `tools/linux/dts/
sm8150-microsoft-surface-duo-ipa.dtsi` (test DTB only), `build-kernel.sh` opt-in `DUO_IPA=1` (applies the patch, makes
QCOM_IPA a module, builds `drivers/net/ipa/ipa.ko`). Compile check: W=1 clean; second run of the script = "already
patched" for all 6 files. Test DTB (`build-test-dtb.sh ... sm8150-microsoft-surface-duo-ipa.dtsi`) resolves every phandle.

How the downstream values map onto mainline:
- **Registers:** v4.1 = mainline `ipa_reg-v4.2.c` (v4.0-family offsets: HASH_FLUSH 0x14c, STATE_AGGR_ACTIVE 0xb4,
  IDLE_INDICATION 0x240, the v4.x COMP_CFG/CLKON/TX_CFG fields) **plus** what v4.2 dropped: hashed tables
  (`ENDP_FILTER_ROUTER_HSH_CFG` 0x85c/0x70; `FILT_ROUT_HASH_EN` is v4.2-only, left out) and 4 resource groups
  (`ENDP_RSRC_GRP` = GENMASK(1,0), not BIT(0)). `ENDP_INIT_CTRL` is only used below v4.0 (left out, as in v4.2).
  GSI = `gsi_reg-v4.0.c` (GSI 2.0, as v4.2 uses).
- **Memory:** `ipa_4_1_mem_part` = mainline v3.5.1's layout + the v4.0 stats regions; a canary pair in every 8-byte gap
  (PDN 0xbd8, quota 0xc30), one canary before the uC descriptor RAM (= `UC_EVENT_RING` 0x2400/0x400); it ends exactly
  at 0x2800, so no END_MARKER (optional; v3.5.1 has none). `ipa-shared` mapped 0x3000 (layout ends at 0x280 + 0x2800;
  sdm845 maps only 0x2000). IMEM 0x146bd000/0x2000 (downstream ipa_smmu_ap additional mapping), SMEM 8 KB.
- **BCR** 0x39 (downstream IPA_BCR_REG_VAL_v4_0) = CMDQ_L_LACK_ONE_ENTRY | SUSPEND_L2_IRQ | HOLB_DROP_L2_IRQ | DUAL_TX.
- **Clock/bus:** core 125 MHz (IPA_V4_0 SVS); interconnects from the DT's SVS msm-bus case: IPA->EBI 80/640 MB/s,
  IPA->OCIMEM 640, APPS->IPA_CFG 80.
- **DT:** reg 0x1e40000 (ipa-base 0x1e00000 + v4.0 reg offset 0x40000), SRAM 0x1e47000, GSI 0x1e04000; SPI 311/432;
  SMMU 0x520 (AP) + 0x522 (uC); new smp2p entries "ipa" on `smp2p-mpss`; `qcom,gsi-loader = "self"` with the
  existing mainline `ipa_fw_mem` (0x98700000, = downstream pil_ipa_fw_region, PAS 15) and
  `firmware-name = "qcom/sm8150/ipa_fws.mdt"`. Firmware = the phone's own vendor_a `/firmware/ipa_fws.{mdt,b00..b04}`
  (pmOS mounts it at /mnt/vendor-fw), linked into /lib/firmware at boot, never stored.

Open questions the first boot answers: SHARED_MEM_SIZE register value (dmesg "IPA SRAM size" / mem errors), whether
the modem accepts the AP's QMI init (`ipa_qmi`, modem-side tables) and whether the clock-query smp2p bits match.

Built 2026-11-06 20:37 (local, `(local file, not published)`): kernel-ipa.efi 9c0d09b0 (QCOM_IPA=m, otherwise = #86
config), ipa.ko e97d397e (alias `of:...qcom,sm8150-ipa`), panel/touch .ko from the same build. TODO before booting:
rebuild the test DTB from the DEFAULT DTB's extras (cam-af-flash: camera/flash dtsi) + the ipa dtsi - the saved
test-dtb-ipa.dtb (19de4c2e) is the base Duo DTS + ipa only (no camera).

## Test plan (ONLY with the tester around: modem/IPA bus faults can freeze the SoC past the watchdog)
1. Test kernel = `DUO_IPA=1 build-kernel.sh` in a COPY of the tree (`/root/linux-ipa`; the default #86 tree stays as is).
   One-shot: its vmlinuz.efi + test DTB (default DTB content + ipa dtsi) + a 2nd initrd with `lib/duo/ipa.ko` (+ the
   panel/touch .ko from the same build).
2. On the phone, BEFORE loading: stream the kernel log to the PC (`cat /proc/kmsg` as its own ssh task), link
   `/mnt/vendor-fw/firmware/ipa_fws.*` into `/lib/firmware/qcom/sm8150/`, modem already up in low-power.
3. `insmod ipa.ko` -> expect "IPA driver initialized" + an `rmnet_ipa0` netdev; then `rc-service modemmanager restart`
   without the veth stand-in and check `mmcli -m 0` lists a net port.
4. Any hang -> PS_HOLD reset by the watchdog / the tester; read /duo/pstore.
