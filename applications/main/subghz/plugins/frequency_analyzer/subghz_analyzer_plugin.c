#include <subghz/helpers/subghz_analyzer_plugin.h>
#include <subghz/helpers/subghz_feature_plugin.h>

static const SubGhzAnalyzerPlugin api = {
    .alloc = subghz_frequency_analyzer_alloc,
    .free = subghz_frequency_analyzer_free,
    .get_view = subghz_frequency_analyzer_get_view,
    .set_callback = subghz_frequency_analyzer_set_callback,
    .get_frequency_to_save = subghz_frequency_analyzer_get_frequency_to_save,
    .get_observation = subghz_frequency_analyzer_get_observation,
    .get_selected_preset = subghz_frequency_analyzer_get_selected_preset,
    .feedback_level = subghz_frequency_analyzer_feedback_level,
    .get_trigger_level = subghz_frequency_analyzer_get_trigger_level,
};

static const FlipperAppPluginDescriptor descriptor = {
    .appid = SUBGHZ_ANALYZER_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_FEATURE_PLUGIN_API_VERSION,
    .entry_point = &api,
};

const FlipperAppPluginDescriptor* subghz_frequency_analyzer_ep(void) {
    return &descriptor;
}
