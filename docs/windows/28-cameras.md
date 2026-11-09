# Driver 26 — Cameras

Status (corrected 2026-11-06): **Linux: ✅ works** (mainline workbench + postmarketOS: photos, live preview,
Snapshot). **Windows: missing driver stack** (a driver gap like audio, NOT a crypto wall).

## What it is
One Sony IMX351 module (16 MP, front-facing when the Duo is open; Android also used it as the rear camera by folding),
with a Dongwoon DW9800L autofocus actuator, an EEPROM and a High CRI LED flash on the PM8150L flash module.
Hardware path on mainline: IMX351 on CCI0 master 1 (0x1a) → CSIPHY1 → CSID0 → VFE0 RDI → memory (raw SBGGR10),
then libcamera's software ISP. Details, log and tools: [workbench/camera.md](workbench/camera.md).

## Correction (2026-11-06)
This chapter used to say "hard dead end: the camera needs a secure (signed) path on cdsp + npu". **That is wrong.**
The raw capture path needs no DSP and no signed firmware: our IMX351 driver + the mainline CAMSS driver (with the
SM8150 GCC camera AXI clock fix) stream real frames, and libcamera's software ISP turns them into pictures.
The secure path only matters for Qualcomm's own ISP pipeline (Spectra 380 with its signed firmware), which a raw
camera does not use. The old conclusion came from Windows-side evidence only: the Windows device nodes exist but no
Windows camera driver stack exists for this phone.

## Windows
Still open, and now a normal driver problem: a Windows camera driver would need the same pieces we used on Linux
(CCI/I2C sensor control, CSIPHY/CSID/VFE raw capture, a software ISP or Media Foundation transform). Our Linux driver
(tools/linux/drivers/imx351) and its register tables are the spec. Not started; Linux comes first.

## Open on Linux
Colour/exposure (the sensor sees far too little light in our captures - see workbench/camera.md), autofocus and
flash in the default boot, a calibrated tuning file.
