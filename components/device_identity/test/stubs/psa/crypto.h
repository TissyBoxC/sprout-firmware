#pragma once

#include <stddef.h>
#include <stdint.h>

typedef int32_t psa_status_t;
typedef uint32_t psa_key_id_t;
typedef uint16_t psa_key_type_t;
typedef uint32_t psa_algorithm_t;
typedef uint32_t psa_key_usage_t;

#define PSA_SUCCESS 0

#define PSA_KEY_TYPE_ECC_KEY_PAIR(family) ((psa_key_type_t)(0x7100 + family))
#define PSA_ECC_FAMILY_SECP_R1 0x12
#define PSA_KEY_USAGE_EXPORT 0x0001
#define PSA_KEY_USAGE_SIGN_MESSAGE 0x0002
#define PSA_ALG_SHA_256 0x02000009
#define PSA_ALG_ECDSA(hash_alg) ((psa_algorithm_t)(0x06000600 + hash_alg))

typedef struct {
    psa_key_type_t type;
    size_t bits;
    psa_key_usage_t usage;
    psa_algorithm_t algorithm;
} psa_key_attributes_t;

#define PSA_KEY_ATTRIBUTES_INIT ((psa_key_attributes_t){0})

void psa_set_key_type(psa_key_attributes_t *attributes, psa_key_type_t type);
void psa_set_key_bits(psa_key_attributes_t *attributes, size_t bits);
void psa_set_key_usage_flags(
    psa_key_attributes_t *attributes,
    psa_key_usage_t usage
);
void psa_set_key_algorithm(
    psa_key_attributes_t *attributes,
    psa_algorithm_t algorithm
);

psa_status_t psa_crypto_init(void);
psa_status_t psa_generate_key(
    const psa_key_attributes_t *attributes,
    psa_key_id_t *key
);
psa_status_t psa_export_key(
    psa_key_id_t key,
    uint8_t *data,
    size_t data_size,
    size_t *data_length
);
psa_status_t psa_import_key(
    const psa_key_attributes_t *attributes,
    const uint8_t *data,
    size_t data_size,
    psa_key_id_t *key
);
psa_status_t psa_export_public_key(
    psa_key_id_t key,
    uint8_t *data,
    size_t data_size,
    size_t *data_length
);
psa_status_t psa_hash_compute(
    psa_algorithm_t alg,
    const uint8_t *input,
    size_t input_length,
    uint8_t *hash,
    size_t hash_size,
    size_t *hash_length
);
psa_status_t psa_sign_message(
    psa_key_id_t key,
    psa_algorithm_t alg,
    const uint8_t *input,
    size_t input_length,
    uint8_t *signature,
    size_t signature_size,
    size_t *signature_length
);
psa_status_t psa_destroy_key(psa_key_id_t key);
