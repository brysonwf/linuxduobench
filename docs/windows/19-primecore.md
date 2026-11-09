# Driver 19 — PrimeCore (CPU prime-core frequency)

Status: ❌ **loads but does NOT boost (proven ineffective, 2026-10-28).** Per-core frequency benchmark
(`scratchpad/primecore-bench.ps1`, peak per-core MHz under all-core load):
- PrimeCore **ON**:  big cores (0,4–0,7) = **2419 MHz**, Silver (0,0–0,3) = 1785 MHz, MAX **2419**.
- PrimeCore **OFF**: big cores = **2419 MHz**, MAX **2419** — **identical.**
So the prime A76 core is pinned at the Gold-cluster freq (2419) and never reaches its rated ~2840 MHz,
with or without PrimeCore. The driver's register write isn't unlocking the prime-core OPP. (DVFS per
cluster does work: Gold 2419 vs Silver 1785.)

## LUT confirmed: the 2.84 GHz OPP EXISTS (2026-10-28)
PrimeCore's `Diag` readback of the OSM LUTs: gold = 17 entries, max **2,419,200 kHz**; prime = 20
entries, **825,600 → 2,841,600 kHz**. So the prime domain's hardware LUT **does** have the 2.84 GHz
OPP — this is NOT a firmware/LUT cap. The driver maps both domains fine (ENABLE bit set) and writes a
scaled index to the prime `PERF_STATE` (0x920), but cpu7 still runs at 2419 under load → **the
PERF_STATE write isn't taking effect.**

## Why it's not working (refined next steps)
0. **(NEW, top suspect) Windows CPPC fights us / wrong write semantics.** The OPP exists but our write
   to prime `PERF_STATE` doesn't raise cpu7. Likely either Windows' processor power mgmt (CPPC, which
   believes max=2419 via `_CPC`) re-writes/clamps the prime domain every cycle, or `PERF_STATE` is the
   wrong register for *forcing* freq on SM8150's prime OSM (may need a max-perf-limit reg, or the write
   is a no-op). **Verify the write lands:** read back the prime domain's current-perf/state register
   after writing (add to `Diag`); and try writing the **top index directly** (bypass the gold-mirror)
   to isolate the mirror logic from the write mechanism.
1. **Confirm the write lands:** add a `Diag` registry readback to PrimeCore (as DuoCharge has) — read
   the cpufreq-hw/OSM/EPSS domain register before/after to see if our write takes effect at all.
2. **Right domain + OPP:** SM8150 has separate frequency domains; the **prime core is its own domain**
   (not the Gold cluster). Verify the domain index + the LUT/OPP entry for 2.84 GHz against
   `qcom-cpufreq-hw.c` / the EPSS LUT — we may be writing the wrong domain or a capped OPP.
3. **OS side:** Windows CPPC/`_CPC` may only expose up to 2419 (the OS won't *request* an OPP it
   doesn't know). Check whether the 2.84 GHz OPP is exposed to Windows at all (ACPI `_CPC` / EPSS LUT);
   if not, the fix is firmware/ACPI, not just a register poke.

Build/deploy: `tools\Build-Driver.ps1 -Dir PrimeCore -Name PrimeCore` → scp → `sc start PrimeCore`.
Benchmark: push `primecore-bench.ps1`, run with/without the service, compare MAX MHz.

## What it is
SM8150 is 1+3+4: one **prime** Cortex-A76 "Gold+" core (up to ~2.84 GHz), 3 Gold, 4 Silver. Windows
on the Duo leaves the prime core capped (a missing cpufreq/clock register write), so peak single-core
performance is lower than the hardware allows. `PrimeCore/PrimeCore.c` is our small driver that
programs the prime-core frequency domain (values taken from Linux `drivers/cpufreq/qcom-cpufreq-hw.c`).

## Status / evidence
- Driver built + signed (OEMA0-KMCI). Historically blocked only by 4551 (a244370e didn't trust OEMA0).
- In the SB-off baseline, our drivers load with no CI gate → PrimeCore should now load like DuoCharge.

## Next
1. `scp` + `sc create/start PrimeCore` on the SB-off Windows; confirm it loads.
2. Verify the prime core reaches its rated frequency (perf counter / `Get-CimInstance Win32_Processor
   CurrentClockSpeed` under load, or a quick single-core benchmark).
3. Add a `Diag` register-readback (as DuoCharge has) to confirm the clock registers took.

## Build / deploy
`tools\Build-Driver.ps1 -Dir PrimeCore -Name PrimeCore` → scp → `sc start PrimeCore`.
