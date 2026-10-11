#pragma once

// The host test exercises the additional-suffix path, so the compiled Kconfig
// value names a second trusted domain in addition to clarkhub.cn.
#define CONFIG_TRANSPORT_SECURITY_ALLOWED_HOST "example.com"
