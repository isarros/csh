#pragma once

#include <stddef.h>
#include <stdint.h>

#include <slash/slash.h>

#include "win_payload_proto.h"

int win_payload_send_request_raw_ex(struct slash *slash,
                                unsigned int node,
                                unsigned int timeout,
                                uint8_t cmd,
                                const void *arg,
                                size_t arg_len,
                                int expect_reply,
                                int verbose,
                                uint8_t *resp_status_out,
                                char *resp_text_out,
                                size_t resp_text_sz);

int win_payload_send_request_raw_retry_ex(struct slash *slash,
                                unsigned int node,
                                unsigned int timeout,
                                uint8_t cmd,
                                const void *arg,
                                size_t arg_len,
                                int expect_reply,
                                int verbose,
                                uint8_t *resp_status_out,
                                char *resp_text_out,
                                size_t resp_text_sz);

int win_payload_send_request(struct slash *slash,
                         unsigned int node,
                         unsigned int timeout,
                         uint8_t cmd,
                         const char *arg);

int win_payload_send_request_retry(struct slash *slash,
                         unsigned int node,
                         unsigned int timeout,
                         uint8_t cmd,
                         const char *arg);

int win_payload_query_remote_offset(struct slash *slash,
                                unsigned int node,
                                unsigned int timeout,
                                const char *remote_name,
                                uint32_t *offset_out,
                                int verbose);

int win_payload_query_remote_size(struct slash *slash,
                              unsigned int node,
                              unsigned int timeout,
                              const char *remote_name,
                              uint32_t *size_out,
                              int verbose);

int win_payload_get_file_chunk(struct slash *slash,
                           unsigned int node,
                           unsigned int timeout,
                           const char *remote_name,
                           uint32_t offset,
                           win_payload_file_read_resp_t *chunk_out);

int win_payload_list_files_page(struct slash *slash,
                            unsigned int node,
                            unsigned int timeout,
                            const char *remote_dir,
                            uint32_t start_index,
                            uint32_t *next_index_out,
                            int *is_end_out,
                            char *items_out,
                            size_t items_out_sz);
