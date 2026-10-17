# ed25519_verify

Small RFC 8032 Ed25519 verifier used by the OTA signed-manifest path.

The verification backend is Monocypher 3.1.3, vendored from commit
`c4f3d97262d090ff71781818cb5f32eafd676bf1` under
`BSD-2-Clause OR CC0-1.0`. Only the core and optional Ed25519 sources are
compiled; the public key and signature formats are raw 32-byte and 64-byte
values. The license text is kept in `MONOCYPHER_LICENCE.md`.

The wrapper is intentionally limited to signature verification. It does not
generate keys, sign, or expose secret-key material.
