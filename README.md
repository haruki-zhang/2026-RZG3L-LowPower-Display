# RZ/G3L LowPower Display

Display on in system suspend for the RZ/G3L SMARC EVK, Renesas RZ Linux BSP
Plus for RZ/G3L (Yocto scarthgap). The CA55 (Cortex-A55) sleeps and the DDR is
in self-refresh, while the CM33 (Cortex-M33) keeps a low power picture on
screen from on-chip SRAM and updates the measured module power on it. USER_SW3
returns to the Linux GUI. The guide in `docs/` describes how it works and how
to build, write and use it.

| Layer | What it does |
|---|---|
| `meta-rzg3l-lp-display` | CM33 firmware, TF-A suspend mode `disp`, kernel display hand-over, GUI and `lpdemo-ctl`, image `lpdemo-image` |
| `meta-rzg3l-seamless-logo-still` | Boot logo in U-Boot; the kernel takes over the running display. Required |

The Multi-OS package (`meta-rz-multi-os`) is also required: BL2 loads and
starts the CM33 firmware. It is not included; get it as described in Renesas
RZ Linux BSP Plus (How To Build, Step 3).

| Directory | Content |
|---|---|
| `docs/` | `RZG3L-LowPower-Display-Guide.md` (and `.pdf`) |
| `layers/` | Yocto layers, each with a `README` |
| `image/` | Prebuilt eSD WIC image |
| `tools/` | `md2pdf.py`: renders the guide as PDF |
