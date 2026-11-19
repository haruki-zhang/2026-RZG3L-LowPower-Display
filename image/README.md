# Prebuilt image

Prebuilt `lpdemo-image` for `MACHINE = "smarc-rzg3l"`, built from the layers
in this package (`meta-rzg3l-seamless-logo-still` + `meta-rzg3l-lp-display`,
with the Multi-OS package and `USE_OPENAMP = "0"`). CM33 firmware 1.3.6,
without the CM33 boot animation.

```plaintext
image/
├── lpdemo-image-smarc-rzg3l.rootfs.wic.gz
└── lpdemo-image-smarc-rzg3l.rootfs.wic.bmap
```

The WIC image is complete for eSD boot (microSD card in the SMARC module
slot): it contains `bl2`, `fip`, the CM33 firmware (at offset `0x270000`) and
the rootfs. For QSPI and eMMC boot, build `bl2`, `fip` and the CM33 firmware
from the same layers and write them as the guide describes (Section 4.1).

To write the image to a microSD card on the host PC:

```sh
# replace sdX with the actual microSD card device
sudo bmaptool copy lpdemo-image-smarc-rzg3l.rootfs.wic.gz /dev/sdX
```

Then set eSD boot mode and power on (guide, Section 4, Step 5).
