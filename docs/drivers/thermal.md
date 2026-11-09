# Workbench — Thermal

Chapter of the Linux workbench notes (hub: [../33-linux-workbench.md](../33-linux-workbench.md)); newest log entries first.

## What Microsoft does (Android `vendor/etc/thermal-engine.conf`, read 2026-11-07; the file stays local)
A **virtual skin sensor** = weighted mix of the board thermistors skin-therm (553), msm-therm (435) and the fuel
gauge's battery temperature (115), sampled every 7 s. On it:
- 43.5 C trip (clear 42.5) for the virtual sensor itself; a low-temperature twin at 15.5 C.
- CPU silver + gold and GPU: step-wise set point 47.5 C (clear 44.5 C).
- Charging (VTS_THERMAL_CHARGING): levels 1 / 2 / 3 at 46.5 / 49.5 / 53.5 C (clear 44.5 / 47.5 / 51.5) = the DT's
  thermal-mitigation currents 1.85 / 0.74 / 0.185 A.
- Both panels' backlight capped at 49 / 51.5 / 52.5 C (and raised when cold: 6.5 / 11.5 C).
- Modem PA mitigation on a second virtual sensor (mbhb-therm + lbpa-therm) at 48.5 / 50.5 / 56.5 C.
- **Shutdown at 56 C** (VTS_THERMAL_SHUTDOWN).

## Board thermistors (the phone's own DT, dtbo overlay "Surface Duo DV_A")
PM8150 ADC-TM: xo-therm 0x4c, **skin-therm 0x4d** (AMUX_THM1), lbpa-therm 0x4e. PM8150L ADC-TM: mbhb-therm 0x4d,
**msm-therm 0x4e** (AMUX_THM2), ufs-therm 0x4f. All 100k pull-up, ratiometric, 200 us settle.

## Log
- 2026-11-07 15:00: **Skin limits DONE (DTB 04d8eed1 default).** `sm8150-microsoft-surface-duo-thermal.dtsi`
  (series commit "skin thermistors and skin temperature limits"): skin-therm + msm-therm on the ADC-TM5 blocks, each a
  thermal zone with Microsoft's limits - passive 47.5 C (3 C hysteresis) -> cpufreq cooling of all three clusters +
  the GPU (step_wise, 1 s polling only while passive), critical 56 C -> orderly shutdown. Trips on BOTH thermistors =
  at or before Microsoft's weighted mix. Verified step by step (first with a 105 C critical trip, so a mis-scaled
  sensor could not power the phone off): idle 29.6 / 30.8 C between the gauges (26-28) and the SoC (31-34); under an
  8-core load msm-therm 30.6 -> 42.7 C, skin 29.4 -> 37.8 C, both back down after; with a temporary 38 C trip the
  cooling devices stepped to 6-9 (CPU) / 4 (GPU) and held msm-therm at 39 C, released after the load.
  Charging: `duo-charger-guard.sh` steps Microsoft's thermal charging levels on the hotter thermistor (hysteresis
  logic unit-tested with a sweep) and caps the charge current in both the android and the default profile.
  Not done: backlight caps (needs the panels' brightness path in the guard), modem PA mitigation (no mobile data yet),
  the low-temperature rules.
