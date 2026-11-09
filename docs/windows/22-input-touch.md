# Driver 22 — Input (touchscreen, buttons)

Status: ✅ working.

## What it is
Capacitive touch on both panels + power/volume buttons. Touch is responsive under Windows (confirmed
earlier: "touchscreen works only in Windows"). HID/I2C touch devices enumerate (Duo I2C buses QCOM0511
present + OK). Pen/hover may be partial.

## Next (polish only)
- Confirm both panels' touch + correct coordinate mapping across the fold.
- Check Surface Pen hover (known WOA issue: hover can show on the opposite screen).
