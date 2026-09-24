#include <gui/modules/loading.h>

#include "../test.h" // IWYU pragma: keep

MU_TEST(loading_public_label_progress_api_test) {
    Loading* loading = loading_alloc();
    mu_check(loading != NULL);

    loading_set_text(loading, "Loading test");
    loading_set_progress(loading, 0.5f);
    loading_reset_progress(loading);

    loading_free(loading);
}

MU_TEST_SUITE(loading_api) {
    MU_RUN_TEST(loading_public_label_progress_api_test);
}

int run_minunit_test_loading_api(void) {
    MU_RUN_SUITE(loading_api);
    return MU_EXIT_CODE;
}

TEST_API_DEFINE(run_minunit_test_loading_api)
