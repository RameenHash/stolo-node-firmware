#pragma once
inline int boot_entropy_enables=0, boot_entropy_disables=0;
inline void bootloader_random_enable() { ++boot_entropy_enables; }
inline void bootloader_random_disable() { ++boot_entropy_disables; }
