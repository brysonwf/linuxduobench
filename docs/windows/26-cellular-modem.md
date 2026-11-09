# Driver 24 — Cellular / modem

Status: ✅/❓ modem present & OK; data/VoLTE untested on our boot.

## What it is
Snapdragon **X24 LTE** modem. `Snapdragon(TM) X24 LTE Modem` (QCOM051E) + the RIL/data stack
(MSRil, Cell* devices, LTE Coex) all enumerate OK in the SB-off baseline. Per prior notes cellular
works on Duo 1 (it's the Duo 2 where modem is a dead-end). VoLTE untested.

## Next
- Insert a SIM; confirm registration + data.
- Test VoLTE (needs the IMS/CAN0 config).

## Virtual SIM / eSIM — feasibility (2026-10-28 discovery)
Goal: cellular without a physical SIM (and stop the no-SIM modem scanning endlessly — a heat/drain
source). Discovery via `netsh mbn`:
- Modem: **Snapdragon X24 LTE, embedded**, IMEI (redacted), State "Not connected", no provider.
- **`Sim class: SIM is removable + Multi SIM`** — the MBB driver exposes only a **removable** SIM slot
  (+ dual-SIM); it does **NOT** report an **eUICC/embedded SIM**. The Duo 1 physically has eSIM
  hardware, but the Windows modem driver isn't surfacing the eUICC.

**Verdict (preliminary): a true virtual/eSIM on Windows is likely NOT feasible as-is:**
- **eSIM** needs Windows to see an eUICC (then Mobile Plans / LPA downloads a profile). The eUICC
  isn't exposed → no download. Would need a modem driver/firmware that surfaces the embedded SIM
  (a port/driver gap — Android can use the Duo's eSIM).
- **SoftSIM / virtual SIM** (Qualcomm software-SIM applet) is carrier/OEM-provisioned, not exposed here.
- **Supported path:** a physical nano-SIM (works, but not "virtual").

If pursued: (1) confirm Android uses the Duo's eSIM (proves HW → Windows gap); (2) try an updated/
different MBB/Qualcomm modem driver that exposes the eUICC; (3) inspect the modem firmware LPA/eUICC
interface. Tie-in: a *connected* modem (or disabling the radio when no SIM) stops the scan loop →
may cut right-side heat (driver 20).
