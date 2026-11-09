# Driver 23 — Sensors & interface (hinge / posture / haptics)

Status: ❓ present (OK) in SB-off; functional/calibration untested. (Tier 3: the "feels like a Duo"
interface layer — fold awareness, haptics, ambient sensors. Thermal lives in driver 20.)

## What it is
- **Hinge / posture / ambient:** `Qualcomm All-Ways Aware Hub` (QCOM0521) + `Sensor Platform Device -
  Display / - Keyboard` (QCOM059F/05A0) — the fold angle + which-screen posture that drives the
  dual-screen UX, plus ambient light/proximity.
- **Haptics:** `SurfaceHaptics`.
- **Panel/OEM glue:** `SurfaceOemPanel`, orientation (`Audio Device Orientation` QCOM05D2).

## Next (polish)
1. Confirm hinge/posture sensing actually drives dual-screen behavior (rotation, active screen).
2. Haptics functional check (vibration on input).
3. Ambient light / proximity → auto-brightness / in-call screen-off.

(Charge-adjacent nodes — QCOM055B current-limit monitor, CAD Charge Arbitration — are under driver 18;
thermal sensors are under driver 20.)
