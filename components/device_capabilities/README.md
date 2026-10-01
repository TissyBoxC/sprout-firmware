# device_capabilities

Owns the compile-time capability set advertised by the device. Capability
names and their order are aligned with the platform `1.0.0` contract so a
product build cannot silently invent a hardware name.

The component exposes a bitmap, membership checks, and canonical name
conversion. Individual `CAPABILITY_*` Kconfig options let a product profile
advertise only the hardware and software services it actually contains.
