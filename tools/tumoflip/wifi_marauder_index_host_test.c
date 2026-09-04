#include "wifi_marauder_index.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static void expect_valid(const char* command, const char* prefix, uint32_t expected) {
    uint32_t actual = UINT32_MAX;
    assert(wifi_marauder_parse_device_index_command(command, prefix, &actual));
    assert(actual == expected);
}

static void expect_invalid(const char* command, const char* prefix) {
    uint32_t untouched = 123U;
    assert(!wifi_marauder_parse_device_index_command(command, prefix, &untouched));
    assert(untouched == 123U);
}

int main(void) {
    expect_valid("spoofat -t 0", "spoofat -t", 0U);
    expect_valid("findmy -t 42", "findmy -t", 42U);
    expect_valid("findmy -t   17", "findmy -t", 17U);
    expect_valid("findmy -t 2147483647", "findmy -t", WIFI_MARAUDER_DEVICE_INDEX_MAX);

    expect_invalid(NULL, "findmy -t");
    expect_invalid("findmy -t 1", NULL);
    expect_invalid("findmy -t", "findmy -t");
    expect_invalid("findmy -t   ", "findmy -t");
    expect_invalid("findmy -t -1", "findmy -t");
    expect_invalid("findmy -t +1", "findmy -t");
    expect_invalid("findmy -t 1.0", "findmy -t");
    expect_invalid("findmy -t one", "findmy -t");
    expect_invalid("findmy -t 12 ", "findmy -t");
    expect_invalid("findmy -t 2147483648", "findmy -t");
    expect_invalid("findmy -t 999999999999999999999999", "findmy -t");
    expect_invalid("findmy -tx 1", "findmy -t");
    expect_invalid("spoofat -t 1", "findmy -t");

    puts("wifi_marauder_index_host_test: PASS");
    return 0;
}
