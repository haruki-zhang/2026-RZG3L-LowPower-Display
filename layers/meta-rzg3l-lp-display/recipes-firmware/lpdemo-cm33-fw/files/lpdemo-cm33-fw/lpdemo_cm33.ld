/*
 * RZ/G3L LowPower Display Demo - CM33 firmware (BL22).
 *
 * BL2 (meta-rz-multi-os TF-A layout) loads 192 KiB (RZG3L_M33_FW_SIZE) from boot media
 * offset 0x270000 to BL22_BASE = 0x20000 and sets INITSVTOR = 0x20000. The loaded part
 * (vectors, code, data) must fit in 0x20000-0x4FFFF; .bss and the stack may use the rest
 * of the BL22 area up to 0x59C00; the top 1 KiB up to BL22_LIMIT = 0x5A000 is the
 * interface area of the CM33 fast logo animation (fla_mailbox.h).
 */
MEMORY {
	RAM (rwx) : ORIGIN = 0x00020000, LENGTH = 0x0003A000
}

ENTRY(Reset_Handler)

SECTIONS
{
	.vectors : {
		KEEP(*(.vectors))
	} > RAM

	.text : {
		*(.text*)
		*(.rodata*)
		. = ALIGN(4);
	} > RAM

	.data : {
		*(.data*)
		. = ALIGN(4);
		__load_end = .;
	} > RAM

	.bss (NOLOAD) : {
		__bss_start = .;
		*(.bss*)
		*(COMMON)
		. = ALIGN(4);
		__bss_end = .;
	} > RAM

	.stack (NOLOAD) : {
		. = ALIGN(8);
		. = . + 0x2000;
		__stack_top = .;
	} > RAM

	/DISCARD/ : {
		*(.ARM.exidx*)
		*(.ARM.extab*)
		*(.note*)
		*(.interp)
		*(.dyn*)
		*(.gnu.hash)
		*(.rel.dyn)
		*(.comment)
	}
}

ASSERT(g_vectors == 0x00020000, "vector table is not at BL22_BASE 0x20000")
ASSERT(__load_end <= 0x00050000, "loaded image exceeds the 192 KiB BL2 reads")
/* 0x59C00-0x59FFF: CM33 fast logo animation interface (fla_mailbox.h, FLA_SRAM_BASE) */
ASSERT(__stack_top <= 0x00059C00, "image + stack overlap the fast logo animation area 0x59C00")
