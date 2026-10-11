#include "temperature_monitor_core.h"

#include <cassert>
#include <cstring>

static const temperature_core_thresholds_t thresholds = {
    .warm_tenths = 450,
    .hot_tenths = 600,
    .critical_tenths = 750,
};

static void test_bands_partition_the_range(void) {
    assert(temperature_core_classify(250, &thresholds) == TEMPERATURE_CORE_NORMAL);
    assert(temperature_core_classify(449, &thresholds) == TEMPERATURE_CORE_NORMAL);
    assert(temperature_core_classify(450, &thresholds) == TEMPERATURE_CORE_WARM);
    assert(temperature_core_classify(599, &thresholds) == TEMPERATURE_CORE_WARM);
    assert(temperature_core_classify(600, &thresholds) == TEMPERATURE_CORE_HOT);
    assert(temperature_core_classify(749, &thresholds) == TEMPERATURE_CORE_HOT);
    assert(temperature_core_classify(750, &thresholds) == TEMPERATURE_CORE_CRITICAL);
    assert(temperature_core_classify(1250, &thresholds) == TEMPERATURE_CORE_CRITICAL);
}

static void test_out_of_range_is_unknown(void) {
    assert(temperature_core_classify(-401, &thresholds) == TEMPERATURE_CORE_UNKNOWN);
    assert(temperature_core_classify(1251, &thresholds) == TEMPERATURE_CORE_UNKNOWN);
    assert(temperature_core_classify(200, nullptr) == TEMPERATURE_CORE_UNKNOWN);
}

static void test_names_match_the_contract(void) {
    assert(strcmp(temperature_core_state_name(TEMPERATURE_CORE_UNKNOWN), "unknown") == 0);
    assert(strcmp(temperature_core_state_name(TEMPERATURE_CORE_NORMAL), "normal") == 0);
    assert(strcmp(temperature_core_state_name(TEMPERATURE_CORE_WARM), "warm") == 0);
    assert(strcmp(temperature_core_state_name(TEMPERATURE_CORE_HOT), "hot") == 0);
    assert(strcmp(temperature_core_state_name(TEMPERATURE_CORE_CRITICAL), "critical") == 0);
}

int main(void) {
    test_bands_partition_the_range();
    test_out_of_range_is_unknown();
    test_names_match_the_contract();
    return 0;
}
