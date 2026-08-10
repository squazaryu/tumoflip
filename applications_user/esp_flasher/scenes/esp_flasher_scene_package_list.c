#include "../esp_flasher_app_i.h"
#include "../esp_flasher_worker.h"

static void package_list_callback(void* context, uint32_t index) {
    EspFlasherApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void package_show_error(EspFlasherApp* app, const char* text) {
    DialogMessage* message = dialog_message_alloc();
    dialog_message_set_header(message, "Package rejected", 64, 3, AlignCenter, AlignTop);
    dialog_message_set_text(message, text, 64, 30, AlignCenter, AlignCenter);
    dialog_message_show(app->dialogs, message);
    dialog_message_free(message);
}

static bool package_join_segment_path(
    char* output,
    size_t output_size,
    const char* package_path,
    const char* file_name) {
    const int written = snprintf(output, output_size, "%s/%s", package_path, file_name);
    return written > 0 && (size_t)written < output_size;
}

static bool package_configure_flash(EspFlasherApp* app, char* error, size_t error_size) {
    memset(app->selected_flash_options, 0, sizeof(app->selected_flash_options));
    app->bin_file_path_boot[0] = '\0';
    app->bin_file_path_part[0] = '\0';
    app->bin_file_path_boot_app0[0] = '\0';
    app->bin_file_path_app_a[0] = '\0';

    for(size_t i = 0; i < app->package_plan.segment_count; ++i) {
        const EspFlashPackageSegment* segment = &app->package_plan.segments[i];
        SelectedFlashOptions slot;
        char* destination;
        size_t destination_size;
        switch(segment->role) {
        case EspFlashPackageRoleBootloader:
            slot = SelectedFlashBoot;
            destination = app->bin_file_path_boot;
            destination_size = sizeof(app->bin_file_path_boot);
            break;
        case EspFlashPackageRolePartitionTable:
            slot = SelectedFlashPart;
            destination = app->bin_file_path_part;
            destination_size = sizeof(app->bin_file_path_part);
            break;
        case EspFlashPackageRoleOtaData:
            slot = SelectedFlashBootApp0;
            destination = app->bin_file_path_boot_app0;
            destination_size = sizeof(app->bin_file_path_boot_app0);
            break;
        case EspFlashPackageRoleApplication:
            slot = SelectedFlashAppA;
            destination = app->bin_file_path_app_a;
            destination_size = sizeof(app->bin_file_path_app_a);
            break;
        default:
            snprintf(error, error_size, "Unknown package role");
            return false;
        }
        if(!package_join_segment_path(
               destination, destination_size, app->package_path, segment->file_name)) {
            snprintf(error, error_size, "Segment path is too long");
            return false;
        }
        app->selected_flash_options[slot] = true;
        app->custom_slot_addrs[slot] = segment->offset;
    }

    app->selected_flash_options[SelectedFlashC5Mode] =
        app->package_plan.target == EspFlashPackageTargetEsp32C5;
    app->selected_flash_options[SelectedFlashS3Mode] = false;
    app->num_selected_flash_options = (int)app->package_plan.segment_count;
    return true;
}

static void package_build_summary(EspFlasherApp* app) {
    size_t position = 0U;
    position += snprintf(
        app->package_summary + position,
        sizeof(app->package_summary) - position,
        "%s\nFW %s\n%u files, %lu KiB\n",
        app->package_plan.display_name,
        app->package_plan.firmware_version,
        (unsigned)app->package_plan.segment_count,
        (unsigned long)((app->package_plan.total_size + 1023U) / 1024U));
    for(size_t i = 0; i < app->package_plan.segment_count &&
                    position < sizeof(app->package_summary);
        ++i) {
        const EspFlashPackageSegment* segment = &app->package_plan.segments[i];
        position += snprintf(
            app->package_summary + position,
            sizeof(app->package_summary) - position,
            "%s  0x%lX  %lu B\n",
            esp_flash_package_role_name(segment->role),
            (unsigned long)segment->offset,
            (unsigned long)segment->size);
    }
    if(position < sizeof(app->package_summary)) {
        snprintf(
            app->package_summary + position,
            sizeof(app->package_summary) - position,
            "\nErase: selected regions\nTarget is checked before write.");
    }
}

void esp_flasher_scene_package_list_on_enter(void* context) {
    EspFlasherApp* app = context;
    app->package_mode = false;
    app->package_directory_count = esp_flash_package_list_directories(
        app->storage, app->package_directories, ESP_FLASH_PACKAGE_MAX_DIRECTORIES);

    if(app->package_directory_count == 0U) {
        widget_add_text_box_element(
            app->widget,
            4,
            8,
            120,
            48,
            AlignCenter,
            AlignCenter,
            "No compatible packages.\nDownload one in\nTumoCompanion first.",
            false);
        view_dispatcher_switch_to_view(app->view_dispatcher, EspFlasherAppViewWidget);
        return;
    }

    submenu_set_header(app->submenu, "Verified package folder:");
    for(size_t i = 0; i < app->package_directory_count; ++i) {
        submenu_add_item(
            app->submenu, app->package_directories[i], (uint32_t)i, package_list_callback, app);
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, EspFlasherAppViewSubmenu);
}

bool esp_flasher_scene_package_list_on_event(void* context, SceneManagerEvent event) {
    EspFlasherApp* app = context;
    if(event.type != SceneManagerEventTypeCustom ||
       event.event >= app->package_directory_count) {
        return false;
    }

    char error[128] = {0};
    if(!esp_flash_package_load_verified(
           app->storage,
           app->package_directories[event.event],
           &app->package_plan,
           app->package_path,
           sizeof(app->package_path),
           error,
           sizeof(error)) ||
       !package_configure_flash(app, error, sizeof(error))) {
        package_show_error(app, error[0] ? error : "Validation failed");
        return true;
    }

    app->package_mode = true;
    package_build_summary(app);
    scene_manager_next_scene(app->scene_manager, EspFlasherScenePackageConfirm);
    return true;
}

void esp_flasher_scene_package_list_on_exit(void* context) {
    EspFlasherApp* app = context;
    submenu_reset(app->submenu);
    widget_reset(app->widget);
}
