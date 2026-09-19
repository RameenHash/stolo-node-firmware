// Deterministic fake, not cryptographic verification. ESP32 links real mbedTLS.
#pragma once
#include "esp_system.h"
#define MBEDTLS_MD_SHA256 6
struct mbedtls_hmac_drbg_context {};
inline void mbedtls_hmac_drbg_init(mbedtls_hmac_drbg_context*) {}
inline const void* mbedtls_md_info_from_type(int) { return nullptr; }
inline int mbedtls_hmac_drbg_seed_buf(mbedtls_hmac_drbg_context*, const void*, const unsigned char*, size_t) { return 0; }
inline int mbedtls_hmac_drbg_random(void*, unsigned char* p, size_t n) { esp_fill_random(p,n); return 0; }
