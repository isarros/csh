#pragma once

#include <slash/slash.h>

const char *win_payload_basename(const char *path);
int win_payload_validate_remote_name(const char *name);
int win_payload_validate_remote_get_path(const char *path);
int win_payload_put_file_cmd_impl(struct slash *slash);
int win_payload_get_file_cmd_impl(struct slash *slash);
int win_payload_delete_file_cmd_impl(struct slash *slash);