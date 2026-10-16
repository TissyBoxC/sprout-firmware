#pragma once

/* Only the partition-label default is needed by factory_reset; individual NVS
   key access goes through config_store. */
#define NVS_DEFAULT_PART_NAME "nvs"
