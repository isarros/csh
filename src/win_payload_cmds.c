#include <string.h>

#include <csp/csp.h>
#include <slash/slash.h>
#include <apm/csh_api.h>

#include "win_payload_proto.h"
#include "win_payload_client.h"
#include "win_payload_upload.h"

#define WIN_PAYLOAD_APP_INDALOS "indalos"

static char g_nsr_config_path[PAYLOAD_MAX_ARG_LEN];

slash_command_group(win_payload, "Payload control");
slash_command_group(set, "Set payload options");
slash_command_group(get, "Get payload values");

static int win_payload_get_state_cmd(struct slash *slash) {
    return win_payload_send_request(slash, slash_dfl_node, slash_dfl_timeout, CMD_GET_STATE, NULL);
}
slash_command_sub(win_payload, get_state, win_payload_get_state_cmd, "", "Get win_payload state");

static int win_payload_send_start_app(struct slash *slash,
                                      const char *app,
                                      const char *config_path,
                                      int verbose) {
    uint8_t arg_buf[PAYLOAD_MAX_ARG_LEN];
    size_t app_len;
    size_t config_len;
    size_t arg_len;

    app_len = strlen(app);
    config_len = strlen(config_path);
    arg_len = app_len + 1 + config_len + 1;

    if (arg_len > sizeof(arg_buf)) {
        slash_printf(slash, "Arguments too long\n");
        return SLASH_EINVAL;
    }

    memcpy(arg_buf, app, app_len + 1);
    memcpy(arg_buf + app_len + 1, config_path, config_len + 1);

    return win_payload_send_request_raw_ex(slash,
                                           slash_dfl_node,
                                           slash_dfl_timeout,
                                           CMD_START_APP,
                                           arg_buf,
                                           arg_len,
                                           1,
                                           verbose,
                                           NULL,
                                           NULL,
                                           0);
}

static int win_payload_send_stop_app(struct slash *slash,
                                     const char *app,
                                     int verbose) {
    return win_payload_send_request_raw_ex(slash,
                                           slash_dfl_node,
                                           slash_dfl_timeout,
                                           CMD_STOP_APP,
                                           app,
                                           strlen(app),
                                           1,
                                           verbose,
                                           NULL,
                                           NULL,
                                           0);
}

static int win_payload_start_cmd(struct slash *slash) {
    if (slash->argc < 3 || slash->argv[1][0] == '\0' || slash->argv[2][0] == '\0') {
        slash_printf(slash, "Usage: win_payload start <app> <nsr_path>\n");
        return SLASH_EINVAL;
    }

    return win_payload_send_start_app(slash, slash->argv[1], slash->argv[2], 1);
}
slash_command_sub(win_payload, start, win_payload_start_cmd, "<app> <nsr_path>", "Start win_payload app");

static int win_payload_stop_cmd(struct slash *slash) {
    if (slash->argc < 2 || slash->argv[1][0] == '\0') {
        slash_printf(slash, "Usage: win_payload stop <app>\n");
        return SLASH_EINVAL;
    }
    return win_payload_send_stop_app(slash, slash->argv[1], 1);
}
slash_command_sub(win_payload, stop, win_payload_stop_cmd, "<app>", "Stop win_payload app");

static int set_nsr_config_path_cmd(struct slash *slash) {
    size_t path_len;
    size_t max_path_len;

    if (slash->argc < 2 || slash->argv[1][0] == '\0') {
        slash_printf(slash, "Usage: set nsr_config_path <Windows_nsr_config_path>\n");
        return SLASH_EINVAL;
    }

    path_len = strlen(slash->argv[1]);
    max_path_len = PAYLOAD_MAX_ARG_LEN - strlen(WIN_PAYLOAD_APP_INDALOS) - 2;
    if (path_len > max_path_len || path_len >= sizeof(g_nsr_config_path)) {
        slash_printf(slash, "NSR config path too long\n");
        return SLASH_EINVAL;
    }

    memcpy(g_nsr_config_path, slash->argv[1], path_len + 1);
    slash_printf(slash, "nsr_config_path=%s\n", g_nsr_config_path);
    return SLASH_SUCCESS;
}
slash_command_sub(set, nsr_config_path, set_nsr_config_path_cmd, "<Windows_nsr_config_path>", "Set Windows NSR config path");

static int set_mode_cmd(struct slash *slash) {
    if (slash->argc < 2 || slash->argv[1][0] == '\0') {
        slash_printf(slash, "Usage: set mode <0|1>\n");
        return SLASH_EINVAL;
    }

    if (strcmp(slash->argv[1], "0") == 0) {
        return win_payload_send_stop_app(slash, WIN_PAYLOAD_APP_INDALOS, 1);
    }

    if (strcmp(slash->argv[1], "1") == 0) {
        if (g_nsr_config_path[0] == '\0') {
            slash_printf(slash, "Set nsr_config_path first\n");
            return SLASH_EINVAL;
        }

        return win_payload_send_start_app(slash, WIN_PAYLOAD_APP_INDALOS, g_nsr_config_path, 1);
    }

    slash_printf(slash, "Usage: set mode <0|1>\n");
    return SLASH_EINVAL;
}
slash_command_sub(set, mode, set_mode_cmd, "<0|1>", "Set payload mode");

static int get_state_cmd(struct slash *slash) {
    uint8_t status = 0;
    char reply[256];
    int rc;

    rc = win_payload_send_request_raw_ex(slash,
                                         slash_dfl_node,
                                         slash_dfl_timeout,
                                         CMD_GET_STATE,
                                         NULL,
                                         0,
                                         1,
                                         0,
                                         &status,
                                         reply,
                                         sizeof(reply));
    if (rc != 0)
        return SLASH_SUCCESS;

    if (status != 0) {
        slash_printf(slash, "status=%u\n", status);
        slash_printf(slash, "%s\n", reply);
        return SLASH_SUCCESS;
    }

    if (strstr(reply, "indalos=RUNNING") != NULL) {
        slash_printf(slash, "1\n");
        return SLASH_SUCCESS;
    }

    if (strstr(reply, "indalos=STOPPED") != NULL) {
        slash_printf(slash, "0\n");
        return SLASH_SUCCESS;
    }

    slash_printf(slash, "Failed to parse state reply: %s\n", reply);
    return SLASH_SUCCESS;
}
slash_command_sub(get, state, get_state_cmd, "", "Get payload mode state");

static int win_payload_reboot_os_cmd(struct slash *slash) {
    return win_payload_send_request(slash, slash_dfl_node, slash_dfl_timeout, CMD_REBOOT_OS, NULL);
}
slash_command_sub(win_payload, reboot_os, win_payload_reboot_os_cmd, "", "Reboot remote OS");

static int win_payload_shutdown_os_cmd(struct slash *slash) {
    return win_payload_send_request(slash, slash_dfl_node, slash_dfl_timeout, CMD_SHUTDOWN_OS, NULL);
}
slash_command_sub(win_payload, shutdown_os, win_payload_shutdown_os_cmd, "", "Shutdown remote OS");

static int win_payload_clear_testfolder_cmd(struct slash *slash) {
    if (slash->argc < 2 || strcmp(slash->argv[1], "CONFIRM") != 0) {
        slash_printf(slash, "Usage: win_payload clear_testfolder CONFIRM\n");
        return SLASH_EINVAL;
    }
    return win_payload_send_request(slash, slash_dfl_node, slash_dfl_timeout, CMD_CLEAR_TESTFOLDER, slash->argv[1]);
}
slash_command_sub(win_payload, clear_testfolder, win_payload_clear_testfolder_cmd, "CONFIRM", "Clear contents of the Windows test folder");

static int win_payload_query_file_cmd(struct slash *slash) {
    uint32_t offset = 0;

    if (slash->argc < 2 || win_payload_validate_remote_get_path(slash->argv[1]) != 0) {
        slash_printf(slash, "Usage: win_payload query_file <remote_name_or_full_path>\n");
        return SLASH_EINVAL;
    }

    if (win_payload_query_remote_offset(slash, slash_dfl_node, slash_dfl_timeout, slash->argv[1], &offset, 1) != 0)
        return SLASH_SUCCESS;

    slash_printf(slash, "remote resumable offset: %lu\n", (unsigned long) offset);
    return SLASH_SUCCESS;
}
slash_command_sub(win_payload, query_file, win_payload_query_file_cmd, "<remote_name_or_full_path>", "Query resumable upload offset");

static int win_payload_put_file_cmd(struct slash *slash) {
    return win_payload_put_file_cmd_impl(slash);
}
slash_command_sub(win_payload, put_file, win_payload_put_file_cmd, "<local_path> [remote_name_or_full_path]", "Upload a file to Windows with resume");

static int win_payload_get_file_cmd(struct slash *slash) {
    return win_payload_get_file_cmd_impl(slash);
}
slash_command_sub(win_payload, get_file, win_payload_get_file_cmd, "<remote_name_or_full_path> [local_path]", "Download a file from Windows with resume");

static int win_payload_delete_file_cmd(struct slash *slash) {
    return win_payload_delete_file_cmd_impl(slash);
}
slash_command_sub(win_payload, delete_file, win_payload_delete_file_cmd, "<remote_name_or_full_path>", "Delete a file on Windows");

static int win_payload_list_files_cmd(struct slash *slash) {
    unsigned int node = slash_dfl_node;
    unsigned int timeout = slash_dfl_timeout;
    unsigned int list_timeout = (timeout < 5000) ? 5000 : timeout;
    const char *remote_dir = NULL;
    uint32_t next_index = 0;
    int is_end = 0;
    char items[256];

    if (slash->argc >= 2) {
        remote_dir = slash->argv[1];
        if (win_payload_validate_remote_get_path(remote_dir) != 0) {
            slash_printf(slash, "Invalid remote directory path\n");
            return SLASH_EINVAL;
        }
    }

    do {
        if (win_payload_list_files_page(slash,
                                        node,
                                        list_timeout,
                                        remote_dir,
                                        next_index,
                                        &next_index,
                                        &is_end,
                                        items,
                                        sizeof(items)) != 0) {
            return SLASH_SUCCESS;
        }

        if (items[0] != '\0')
            slash_printf(slash, "%s", items);
    } while (!is_end);

    return SLASH_SUCCESS;
}
slash_command_sub(win_payload, list_files, win_payload_list_files_cmd, "[remote_dir_full_path]", "List files on Windows");

static int win_payload_move_file_cmd(struct slash *slash) {
    const char *from;
    const char *to;
    uint8_t arg_buf[PAYLOAD_MAX_ARG_LEN];
    size_t from_len;
    size_t to_len;
    size_t arg_len;

    if (slash->argc < 3 || slash->argv[1][0] == '\0' || slash->argv[2][0] == '\0') {
        slash_printf(slash, "Usage: win_payload move_file <from> <to>\n");
        return SLASH_EINVAL;
    }

    from = slash->argv[1];
    to = slash->argv[2];

    if (win_payload_validate_remote_get_path(from) != 0) {
        slash_printf(slash, "Invalid source path\n");
        return SLASH_EINVAL;
    }

    if (win_payload_validate_remote_get_path(to) != 0) {
        slash_printf(slash, "Invalid destination path\n");
        return SLASH_EINVAL;
    }

    from_len = strlen(from);
    to_len = strlen(to);
    arg_len = from_len + 1 + to_len + 1;

    if (arg_len > sizeof(arg_buf)) {
        slash_printf(slash, "Paths too long\n");
        return SLASH_EINVAL;
    }

    memcpy(arg_buf, from, from_len + 1);
    memcpy(arg_buf + from_len + 1, to, to_len + 1);

    if (win_payload_send_request_raw_ex(slash,
                                        slash_dfl_node,
                                        slash_dfl_timeout,
                                        CMD_MOVE_FILE,
                                        arg_buf,
                                        arg_len,
                                        1,
                                        1,
                                        NULL,
                                        NULL,
                                        0) != 0) {
        return SLASH_SUCCESS;
    }

    return SLASH_SUCCESS;
}
slash_command_sub(win_payload, move_file, win_payload_move_file_cmd, "<from> <to>", "Move a file on Windows");

static int win_payload_create_folder_cmd(struct slash *slash) {
    if (slash->argc < 2 || slash->argv[1][0] == '\0') {
        slash_printf(slash, "Usage: win_payload create_folder <remote_dir>\n");
        return SLASH_EINVAL;
    }

    if (win_payload_validate_remote_get_path(slash->argv[1]) != 0) {
        slash_printf(slash, "Invalid remote directory path\n");
        return SLASH_EINVAL;
    }

    if (win_payload_send_request(slash,
                                 slash_dfl_node,
                                 slash_dfl_timeout,
                                 CMD_CREATE_FOLDER,
                                 slash->argv[1]) != 0) {
        return SLASH_SUCCESS;
    }

    return SLASH_SUCCESS;
}
slash_command_sub(win_payload, create_folder, win_payload_create_folder_cmd, "<remote_dir>", "Create a folder on Windows");

static int win_payload_delete_folder_cmd(struct slash *slash) {
    if (slash->argc < 2 || slash->argv[1][0] == '\0') {
        slash_printf(slash, "Usage: win_payload delete_folder <remote_dir_or_full_path>\n");
        return SLASH_EINVAL;
    }

    if (win_payload_validate_remote_get_path(slash->argv[1]) != 0) {
        slash_printf(slash, "Invalid remote directory path\n");
        return SLASH_EINVAL;
    }

    if (win_payload_send_request(slash,
                                 slash_dfl_node,
                                 slash_dfl_timeout,
                                 CMD_DELETE_FOLDER,
                                 slash->argv[1]) != 0) {
        return SLASH_SUCCESS;
    }

    return SLASH_SUCCESS;
}
slash_command_sub(win_payload, delete_folder, win_payload_delete_folder_cmd, "<remote_dir_or_full_path>", "Delete a folder on Windows recursively");
