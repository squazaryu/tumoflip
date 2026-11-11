"""Named production-body coverage for the selected 095 adaptations, not RF coverage."""
import os
from tools.tumoflip import test_unleashed095_ir as ir
from tools.tumoflip import test_unleashed095_save as save
from tools.tumoflip import test_unleashed095_browser as browser
from tools.tumoflip import test_unleashed095_rename as rename
from tools.tumoflip import test_proteco as proteco
from tools.tumoflip.measure_upstream_1435_tests import measure

FUNCTIONS={
    'infrared_signal_read_raw','infrared_cli_brute_force_signals','infrared_brute_force_reset',
    'subghz_scene_save_name_on_event','subghz_scene_save_name_on_exit',
    'arf_subghz_scene_save_name_on_event','arf_subghz_scene_save_name_on_exit',
    'browser_folder_init','browser_folder_load_full','browser_folder_load_chunked',
    'browser_dir_read_complete','browser_load_aborted','browser_load_failed','browser_folder_error_cb',
    'storage_common_is_case_only_rename','storage_common_rename_case_only','storage_common_rename',
    'storage_common_copy','validator_is_file_callback','validator_is_file_alloc_init',
    'archive_rename_copy_file_or_dir','subghz_protocol_encoder_proteco_get_upload',
    'subghz_protocol_decoder_proteco_reset','subghz_protocol_decoder_proteco_feed',
    'subghz_protocol_proteco_read_key','subghz_protocol_encoder_proteco_deserialize',
}


def main():
    os.environ['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
    modules=(ir,save,browser,rename,proteco)
    for module in modules:
        module.native=lambda body,selected=module:selected.run_c(body)
    return measure(modules,FUNCTIONS)


if __name__=='__main__':raise SystemExit(main())
