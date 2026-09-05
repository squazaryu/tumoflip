#include "quac_picopass.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(condition)                                                                     \
    do {                                                                                       \
        if(!(condition)) {                                                                     \
            fprintf(stderr, "%s:%d requirement failed: %s\n", __FILE__, __LINE__, #condition); \
            return 1;                                                                          \
        }                                                                                      \
    } while(false)

static const char valid_file[] = "Filetype: Flipper Picopass device\n"
                                 "Version: 1\n"
                                 "Facility Code: 42\n"
                                 "Card Number: 4242\n"
                                 "Credential: 00 00 00 00 00 00 00 00\n"
                                 "# Picopass blocks\n"
                                 "Block 0: 01 02 03 04 05 06 07 08\n"
                                 "Block 1: 0C FF FF FF 7F 1F FF 2C\n"
                                 "Block 2: FE FF FF FF FF FF FF FF\n"
                                 "Block 3: FF FF FF FF FF FF FF FF\n"
                                 "Block 4: FF FF FF FF FF FF FF FF\n"
                                 "Block 5: FF FF FF FF FF FF FF FF\n"
                                 "Block 6: 06 10 20 30 40 50 60 14\n"
                                 "Block 7: 07 11 21 31 41 51 61 71\n"
                                 "Block 8: 08 12 22 32 42 52 62 72\n"
                                 "Block 9: 09 13 23 33 43 53 63 73\n"
                                 "Block 10: 0A 14 24 34 44 54 64 74\n"
                                 "Block 11: 0B 15 25 35 45 55 65 75\n";

static uint16_t test_crc(const uint8_t* data, size_t size) {
    uint16_t crc = 0xE012U;
    for(size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for(size_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) ? (uint16_t)((crc >> 1) ^ 0x8408U) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

static void add_read_crc(uint8_t frame[4]) {
    const uint16_t crc = test_crc(frame + 1, 1);
    frame[2] = (uint8_t)(crc & 0xFFU);
    frame[3] = (uint8_t)(crc >> 8);
}

static bool response_crc_ok(const uint8_t* response, size_t size) {
    if(size < 3) return false;
    const uint16_t expected = test_crc(response, size - 2);
    const uint16_t actual =
        (uint16_t)((uint16_t)response[size - 2] | (uint16_t)((uint16_t)response[size - 1] << 8));
    return actual == expected;
}

static char* replace_once(const char* source, const char* from, const char* to) {
    const char* match = strstr(source, from);
    if(!match) return NULL;
    const size_t prefix = (size_t)(match - source);
    const size_t total = strlen(source) - strlen(from) + strlen(to) + 1;
    char* result = malloc(total);
    if(!result) return NULL;
    memcpy(result, source, prefix);
    memcpy(result + prefix, to, strlen(to));
    strcpy(result + prefix + strlen(to), match + strlen(from));
    return result;
}

static bool all_zero(const void* value, size_t size) {
    const uint8_t* bytes = value;
    for(size_t i = 0; i < size; ++i) {
        if(bytes[i] != 0) return false;
    }
    return true;
}

static int malformed_case(const char* mode) {
    const char* from = NULL;
    const char* to = NULL;
    if(!strcmp(mode, "bad-header")) {
        from = "Flipper Picopass device";
        to = "Other device";
    } else if(!strcmp(mode, "bad-version")) {
        from = "Version: 1";
        to = "Version: 2";
    } else if(!strcmp(mode, "truncated")) {
        from = "Block 11: 0B 15 25 35 45 55 65 75\n";
        to = "";
    } else if(!strcmp(mode, "duplicate-block")) {
        from = "Block 11: 0B 15 25 35 45 55 65 75\n";
        to = "Block 10: 0B 15 25 35 45 55 65 75\n";
    } else if(!strcmp(mode, "unknown-field")) {
        from = "Facility Code: 42";
        to = "Private Key: 42";
    } else if(!strcmp(mode, "invalid-hex")) {
        from = "Block 6: 06";
        to = "Block 6: GG";
    } else if(!strcmp(mode, "extra-hex")) {
        from = "Block 6: 06 10 20 30 40 50 60 14";
        to = "Block 6: 06 10 20 30 40 50 60 14 80";
    } else if(!strcmp(mode, "missing-block")) {
        from = "Block 6: 06 10 20 30 40 50 60 14\n";
        to = "";
    } else if(!strcmp(mode, "small-limit")) {
        from = "Block 1: 0C";
        to = "Block 1: 05";
    } else if(!strcmp(mode, "large-limit")) {
        from = "Block 1: 0C";
        to = "Block 1: 21";
    } else if(!strcmp(mode, "block-over-limit")) {
        from = "Block 11: 0B";
        to = "Block 12: 0B";
    } else if(!strcmp(mode, "oversized-line")) {
        from = "Facility Code: 42";
        to =
            "Facility Code: 42222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222222";
    }
    REQUIRE(from && to);
    char* modified = replace_once(valid_file, from, to);
    REQUIRE(modified);
    QuacPicopassCredential credential;
    memset(&credential, 0xA5, sizeof(credential));
    const QuacPicopassParseResult result =
        quac_picopass_parse(modified, strlen(modified), &credential);
    free(modified);
    REQUIRE(result == QuacPicopassParseInvalid);
    REQUIRE(all_zero(&credential, sizeof(credential)));
    return 0;
}

static int embedded_nul_case(void) {
    char data[sizeof(valid_file)];
    memcpy(data, valid_file, sizeof(valid_file));
    data[24] = '\0';
    QuacPicopassCredential credential;
    REQUIRE(quac_picopass_parse(data, sizeof(data) - 1, &credential) == QuacPicopassParseInvalid);
    REQUIRE(all_zero(&credential, sizeof(credential)));
    return 0;
}

static int secure_case(const char* mode) {
    const char* from = NULL;
    const char* to = NULL;
    if(!strcmp(mode, "secure-fuses")) {
        from = "Block 1: 0C FF FF FF 7F 1F FF 2C";
        to = "Block 1: 0C FF FF FF 7F 1F FF 3C";
    } else if(!strcmp(mode, "encryption-unknown")) {
        from = "Block 6: 06 10 20 30 40 50 60 14";
        to = "Block 6: 06 10 20 30 40 50 60 00";
    } else if(!strcmp(mode, "encryption-unrecognized")) {
        from = "Block 6: 06 10 20 30 40 50 60 14";
        to = "Block 6: 06 10 20 30 40 50 60 70";
    } else if(!strcmp(mode, "encryption-des")) {
        from = "Block 6: 06 10 20 30 40 50 60 14";
        to = "Block 6: 06 10 20 30 40 50 60 15";
    } else if(!strcmp(mode, "encryption-3des")) {
        from = "Block 6: 06 10 20 30 40 50 60 14";
        to = "Block 6: 06 10 20 30 40 50 60 17";
    } else if(!strcmp(mode, "key-material")) {
        from = "Block 3: FF FF FF FF FF FF FF FF";
        to = "Block 3: E0 33 CA 41 9A EE 43 F9";
    }
    REQUIRE(from && to);
    char* modified = replace_once(valid_file, from, to);
    REQUIRE(modified);
    QuacPicopassCredential credential;
    memset(&credential, 0xA5, sizeof(credential));
    const QuacPicopassParseResult result =
        quac_picopass_parse(modified, strlen(modified), &credential);
    free(modified);
    REQUIRE(result == QuacPicopassParseUnsupportedSecure);
    REQUIRE(all_zero(&credential, sizeof(credential)));
    return 0;
}

static void init_session(QuacPicopassCredential* credential, QuacPicopassSession* session) {
    if(quac_picopass_parse(valid_file, strlen(valid_file), credential) != QuacPicopassParseOk)
        abort();
    quac_picopass_session_init(session, credential);
}

static QuacPicopassFrameResult frame(
    QuacPicopassSession* session,
    const uint8_t* request,
    size_t request_size,
    uint8_t* response,
    size_t response_capacity,
    size_t* response_size) {
    bool sof_only = false;
    return quac_picopass_process_frame(
        session, request, request_size, response, response_capacity, response_size, &sof_only);
}

static int select_session(QuacPicopassCredential* credential, QuacPicopassSession* session) {
    init_session(credential, session);
    uint8_t response[40] = {0};
    size_t response_size = 0;
    bool sof = false;
    const uint8_t actall[] = {0x0A};
    REQUIRE(
        quac_picopass_process_frame(
            session, actall, sizeof(actall), response, sizeof(response), &response_size, &sof) ==
        QuacPicopassFrameSof);
    REQUIRE(sof && session->state == QuacPicopassStateActivated);
    const uint8_t identify[] = {0x0C};
    REQUIRE(
        frame(session, identify, sizeof(identify), response, sizeof(response), &response_size) ==
        QuacPicopassFrameResponse);
    REQUIRE(response_size == 10 && response_crc_ok(response, response_size));
    REQUIRE(!memcmp(response, (uint8_t[]){0x40, 0x60, 0x80, 0xA0, 0xC0, 0xE0, 0x00, 0x21}, 8));
    uint8_t select[9] = {0x81};
    memcpy(select + 1, response, 8);
    REQUIRE(
        frame(session, select, sizeof(select), response, sizeof(response), &response_size) ==
        QuacPicopassFrameResponse);
    REQUIRE(response_size == 10 && response_crc_ok(response, response_size));
    REQUIRE(!memcmp(response, credential->blocks[0], 8));
    REQUIRE(session->state == QuacPicopassStateSelected);
    return 0;
}

static int duration_case(void) {
    uint32_t value = 777;
    REQUIRE(quac_picopass_duration_parse("500", &value) && value == 500);
    REQUIRE(quac_picopass_duration_parse("10000", &value) && value == 10000);
    const char* invalid[] = {
        "", "0", "99", "10001", "-1", "+500", " 500", "500 ", "500ms", "01", "4294967296"};
    for(size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        value = 777;
        REQUIRE(!quac_picopass_duration_parse(invalid[i], &value));
        REQUIRE(value == 777);
    }
    return 0;
}

static int activation_case(void) {
    QuacPicopassCredential credential;
    QuacPicopassSession session;
    REQUIRE(select_session(&credential, &session) == 0);
    uint8_t response[40] = {0};
    size_t response_size = 0;
    uint8_t read[4] = {0x0C, 1, 0, 0};
    add_read_crc(read);
    REQUIRE(read[2] == 0xFA && read[3] == 0x22);
    REQUIRE(
        frame(&session, read, 4, response, sizeof(response), &response_size) ==
        QuacPicopassFrameResponse);
    REQUIRE(response_size == 10 && !memcmp(response, credential.blocks[1], 8));
    read[1] = 3;
    add_read_crc(read);
    REQUIRE(
        frame(&session, read, 4, response, sizeof(response), &response_size) ==
        QuacPicopassFrameResponse);
    REQUIRE(response_size == 10);
    REQUIRE(!memcmp(response, (uint8_t[]){0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, 8));
    quac_picopass_session_clear(&session);
    quac_picopass_credential_clear(&credential);
    return 0;
}

static int read_policy_case(void) {
    QuacPicopassCredential credential;
    QuacPicopassSession session;
    REQUIRE(select_session(&credential, &session) == 0);
    uint8_t response[40] = {0};
    size_t response_size = 0;
    uint8_t read[4] = {0x0C, 6, 0, 0};
    add_read_crc(read);
    REQUIRE(
        frame(&session, read, 4, response, sizeof(response), &response_size) ==
        QuacPicopassFrameResponse);
    REQUIRE(response_size == 10 && !memcmp(response, credential.blocks[6], 8));
    read[1] = credential.app_limit;
    add_read_crc(read);
    REQUIRE(
        frame(&session, read, 4, response, sizeof(response), &response_size) ==
        QuacPicopassFrameRejected);
    quac_picopass_session_clear(&session);
    quac_picopass_credential_clear(&credential);
    return 0;
}

static int read4_case(void) {
    QuacPicopassCredential credential;
    QuacPicopassSession session;
    REQUIRE(select_session(&credential, &session) == 0);
    uint8_t response[40] = {0};
    size_t response_size = 0;
    uint8_t read4[4] = {0x06, 6, 0, 0};
    add_read_crc(read4);
    REQUIRE(
        frame(&session, read4, 4, response, sizeof(response), &response_size) ==
        QuacPicopassFrameResponse);
    REQUIRE(response_size == 34 && response_crc_ok(response, response_size));
    REQUIRE(!memcmp(response, credential.blocks[6], 8));
    REQUIRE(!memcmp(response + 24, credential.blocks[9], 8));
    read4[1] = 9;
    add_read_crc(read4);
    REQUIRE(
        frame(&session, read4, 4, response, sizeof(response), &response_size) ==
        QuacPicopassFrameRejected);
    quac_picopass_session_clear(&session);
    quac_picopass_credential_clear(&credential);
    return 0;
}

static int halt_reselect_case(void) {
    QuacPicopassCredential credential;
    QuacPicopassSession session;
    REQUIRE(select_session(&credential, &session) == 0);
    uint8_t response[40] = {0};
    size_t response_size = 0;
    bool sof = false;
    const uint8_t halt[] = {0};
    REQUIRE(
        quac_picopass_process_frame(
            &session, halt, 1, response, sizeof(response), &response_size, &sof) ==
        QuacPicopassFrameSof);
    REQUIRE(sof && session.state == QuacPicopassStateHalted);
    uint8_t select[9] = {0x81};
    memcpy(select + 1, credential.blocks[0], 8);
    REQUIRE(
        frame(&session, select, 9, response, sizeof(response), &response_size) ==
        QuacPicopassFrameResponse);
    REQUIRE(session.state == QuacPicopassStateSelected);
    quac_picopass_session_clear(&session);
    quac_picopass_credential_clear(&credential);
    return 0;
}

static int unknown_case(void) {
    QuacPicopassCredential credential;
    QuacPicopassSession session;
    init_session(&credential, &session);
    uint8_t response[40] = {0xA5};
    size_t response_size = 37;
    const uint8_t frames[][9] = {
        {0x88, 0x02},
        {0x18, 0x02},
        {0x05, 0, 0, 0, 0, 0, 0, 0, 0},
        {0x87, 1, 2, 3},
    };
    const size_t sizes[] = {2, 2, 9, 4};
    for(size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        const QuacPicopassFrameResult result =
            frame(&session, frames[i], sizes[i], response, sizeof(response), &response_size);
        REQUIRE(result == QuacPicopassFrameIgnored || result == QuacPicopassFrameRejected);
        REQUIRE(response_size == 0);
    }
    quac_picopass_session_clear(&session);
    quac_picopass_credential_clear(&credential);
    return 0;
}

typedef struct {
    unsigned load_count;
    unsigned start_count;
    unsigned wait_count;
    unsigned stop_count;
    QuacPicopassParseResult load_result;
    bool start_ok;
    QuacPicopassWaitResult wait_result;
} FakeRuntime;

static QuacPicopassParseResult
    fake_load(void* context, const char* path, QuacPicopassCredential* credential) {
    FakeRuntime* fake = context;
    fake->load_count++;
    if(!path || strcmp(path, "/ext/test.picopass")) abort();
    if(fake->load_result == QuacPicopassParseInvalid) return fake->load_result;
    if(fake->load_result == QuacPicopassParseUnsupportedSecure) {
        char* secure = replace_once(
            valid_file, "Block 1: 0C FF FF FF 7F 1F FF 2C", "Block 1: 0C FF FF FF 7F 1F FF 3C");
        if(!secure) abort();
        const QuacPicopassParseResult result =
            quac_picopass_parse(secure, strlen(secure), credential);
        free(secure);
        return result;
    }
    return quac_picopass_parse(valid_file, strlen(valid_file), credential);
}

static bool fake_start(void* context, QuacPicopassSession* session) {
    FakeRuntime* fake = context;
    fake->start_count++;
    if(session->state != QuacPicopassStateIdle) abort();
    return fake->start_ok;
}

static QuacPicopassWaitResult fake_wait(void* context, uint32_t duration_ms) {
    FakeRuntime* fake = context;
    fake->wait_count++;
    if(duration_ms != 500) abort();
    return fake->wait_result;
}

static void fake_stop(void* context) {
    FakeRuntime* fake = context;
    fake->stop_count++;
}

static int lifecycle_case(const char* mode) {
    FakeRuntime fake = {
        .load_result = !strcmp(mode, "lifecycle-load-fail") ? QuacPicopassParseInvalid :
                       !strcmp(mode, "lifecycle-secure")    ? QuacPicopassParseUnsupportedSecure :
                                                              QuacPicopassParseOk,
        .start_ok = strcmp(mode, "lifecycle-start-fail") != 0,
        .wait_result = !strcmp(mode, "lifecycle-cancel")    ? QuacPicopassWaitCancelled :
                       !strcmp(mode, "lifecycle-wait-fail") ? QuacPicopassWaitError :
                                                              QuacPicopassWaitComplete,
    };
    const QuacPicopassRuntimeOps ops = {
        .context = &fake,
        .load = fake_load,
        .start = fake_start,
        .wait = fake_wait,
        .stop = fake_stop,
    };
    const QuacPicopassRunResult result = quac_picopass_run(&ops, "/ext/test.picopass", 500);
    REQUIRE(fake.load_count == 1);
    if(fake.load_result == QuacPicopassParseInvalid) {
        REQUIRE(result == QuacPicopassRunLoadError);
        REQUIRE(fake.start_count == 0 && fake.wait_count == 0 && fake.stop_count == 0);
    } else if(fake.load_result == QuacPicopassParseUnsupportedSecure) {
        REQUIRE(result == QuacPicopassRunUnsupportedSecure);
        REQUIRE(fake.start_count == 0 && fake.wait_count == 0 && fake.stop_count == 0);
    } else if(!fake.start_ok) {
        REQUIRE(result == QuacPicopassRunStartError);
        REQUIRE(fake.start_count == 1 && fake.wait_count == 0 && fake.stop_count == 1);
    } else {
        REQUIRE(fake.start_count == 1 && fake.wait_count == 1 && fake.stop_count == 1);
        if(fake.wait_result == QuacPicopassWaitCancelled) {
            REQUIRE(result == QuacPicopassRunCancelled);
        } else if(fake.wait_result == QuacPicopassWaitError) {
            REQUIRE(result == QuacPicopassRunWaitError);
        } else {
            REQUIRE(result == QuacPicopassRunComplete);
        }
    }
    return 0;
}

static int zeroize_case(void) {
    QuacPicopassCredential credential;
    QuacPicopassSession session;
    init_session(&credential, &session);
    quac_picopass_session_clear(&session);
    quac_picopass_credential_clear(&credential);
    REQUIRE(all_zero(&credential, sizeof(credential)));
    REQUIRE(all_zero(&session, sizeof(session)));
    return 0;
}

int main(int argc, char** argv) {
    if(argc != 2) return 2;
    const char* mode = argv[1];
    if(!strcmp(mode, "parse-valid")) {
        QuacPicopassCredential credential;
        memset(&credential, 0xA5, sizeof(credential));
        REQUIRE(
            quac_picopass_parse(valid_file, strlen(valid_file), &credential) ==
            QuacPicopassParseOk);
        REQUIRE(credential.app_limit == 12);
        REQUIRE(!memcmp(credential.blocks[0], (uint8_t[]){1, 2, 3, 4, 5, 6, 7, 8}, 8));
        REQUIRE(all_zero(credential.blocks[3], 8));
        REQUIRE(all_zero(credential.blocks[4], 8));
        quac_picopass_credential_clear(&credential);
        return 0;
    }
    if(!strcmp(mode, "embedded-nul")) return embedded_nul_case();
    if(!strcmp(mode, "duration")) return duration_case();
    if(!strcmp(mode, "activation")) return activation_case();
    if(!strcmp(mode, "secure-fuses") || !strncmp(mode, "encryption-", 11) ||
       !strcmp(mode, "key-material"))
        return secure_case(mode);
    if(!strcmp(mode, "read-policy")) return read_policy_case();
    if(!strcmp(mode, "read4")) return read4_case();
    if(!strcmp(mode, "halt-reselect")) return halt_reselect_case();
    if(!strcmp(mode, "unknown")) return unknown_case();
    if(!strncmp(mode, "lifecycle-", 10)) return lifecycle_case(mode);
    if(!strcmp(mode, "zeroize")) return zeroize_case();
    return malformed_case(mode);
}
