#include "win_payload_client.h"
#include "win_payload_proto.h"

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>

#include <csp/csp.h>

#define WIN_PAYLOAD_RETRYABLE_FAILURE 2

static int win_payload_parse_numeric_reply(struct slash *slash,
                                       const char *reply,
                                       const char *prefix,
                                       uint32_t *value_out) {
    unsigned long parsed = 0;

    if (sscanf(reply, prefix, &parsed) != 1) {
        slash_printf(slash, "Failed to parse reply\n");
        return -1;
    }

    *value_out = (uint32_t) parsed;
    return 0;
}

static int win_payload_send_request_raw_once(struct slash *slash,
                                unsigned int node,
                                unsigned int timeout,
                                uint8_t cmd,
                                const void *arg,
                                size_t arg_len,
                                int expect_reply,
                                int verbose,
                                uint8_t *resp_status_out,
                                char *resp_text_out,
                                size_t resp_text_sz,
                                int quiet_transient) {
    csp_conn_t *conn;
    csp_packet_t *pkt;
    csp_packet_t *reply;
    win_payload_req_t *req;
    win_payload_resp_t *resp;
    size_t pkt_len;

    if (arg_len > sizeof(((win_payload_req_t *) 0)->arg)) {
        slash_printf(slash, "Argument too long (max %u bytes)\n",
                     (unsigned) sizeof(((win_payload_req_t *) 0)->arg));
        return -1;
    }

    if (resp_text_out != NULL && resp_text_sz > 0)
        resp_text_out[0] = '\0';

    conn = csp_connect(CSP_PRIO_NORM, (uint16_t) node, PAYLOAD_CMD_PORT, timeout, CSP_O_NONE);
    if (!conn) {
        if (!quiet_transient)
            slash_printf(slash, "Failed to connect to node %u port %u\n", node, PAYLOAD_CMD_PORT);
        return WIN_PAYLOAD_RETRYABLE_FAILURE;
    }

    pkt = csp_buffer_get(sizeof(win_payload_req_t));
    if (!pkt) {
        slash_printf(slash, "Failed to allocate CSP packet\n");
        csp_close(conn);
        return -1;
    }

    req = (win_payload_req_t *) pkt->data;
    memset(req, 0, sizeof(*req));
    req->cmd = cmd;
    req->reserved = 0;
    req->arg_len = (uint16_t) arg_len;

    if (arg != NULL && arg_len > 0)
        memcpy(req->arg, arg, arg_len);

    pkt_len = offsetof(win_payload_req_t, arg) + arg_len;
    pkt->length = (uint16_t) pkt_len;

    csp_send(conn, pkt);

    if (!expect_reply) {
        csp_close(conn);
        if (resp_status_out)
            *resp_status_out = 0;
        return 0;
    }

    reply = csp_read(conn, timeout);
    if (!reply) {
        if (!quiet_transient)
            slash_printf(slash, "No reply from node %u\n", node);
        csp_close(conn);
        return WIN_PAYLOAD_RETRYABLE_FAILURE;
    }

    if (reply->length < offsetof(win_payload_resp_t, data)) {
        if (!quiet_transient)
            slash_printf(slash, "Short reply from node %u\n", node);
        csp_buffer_free(reply);
        csp_close(conn);
        return WIN_PAYLOAD_RETRYABLE_FAILURE;
    }

    resp = (win_payload_resp_t *) reply->data;

    if (resp->data_len >= sizeof(resp->data)) {
        if (!quiet_transient)
            slash_printf(slash, "Malformed reply from node %u\n", node);
        csp_buffer_free(reply);
        csp_close(conn);
        return WIN_PAYLOAD_RETRYABLE_FAILURE;
    }

    if (reply->length < offsetof(win_payload_resp_t, data) + resp->data_len + 1) {
        if (!quiet_transient)
            slash_printf(slash, "Truncated reply from node %u\n", node);
        csp_buffer_free(reply);
        csp_close(conn);
        return WIN_PAYLOAD_RETRYABLE_FAILURE;
    }

    resp->data[resp->data_len] = '\0';

    if (resp_status_out != NULL)
        *resp_status_out = resp->status;

    if (resp_text_out != NULL && resp_text_sz > 0)
        snprintf(resp_text_out, resp_text_sz, "%s", resp->data);

    if (verbose) {
        slash_printf(slash, "status=%u\n", resp->status);
        slash_printf(slash, "%s\n", resp->data);
    }

    csp_buffer_free(reply);
    csp_close(conn);
    return 0;
}

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
                                size_t resp_text_sz) {
    int rc;

    rc = win_payload_send_request_raw_once(slash,
                                           node,
                                           timeout,
                                           cmd,
                                           arg,
                                           arg_len,
                                           expect_reply,
                                           verbose,
                                           resp_status_out,
                                           resp_text_out,
                                           resp_text_sz,
                                           0);
    if (rc == WIN_PAYLOAD_RETRYABLE_FAILURE)
        return SLASH_EIO;

    return rc;
}

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
                                size_t resp_text_sz) {
    unsigned int attempt;
    unsigned int max_attempts = expect_reply ? WIN_PAYLOAD_CMD_MAX_RETRIES : 1;
    int rc = -1;

    for (attempt = 1; attempt <= max_attempts; attempt++) {
        rc = win_payload_send_request_raw_once(slash,
                                               node,
                                               timeout,
                                               cmd,
                                               arg,
                                               arg_len,
                                               expect_reply,
                                               verbose,
                                               resp_status_out,
                                               resp_text_out,
                                               resp_text_sz,
                                               1);
        if (rc == 0 || rc < 0)
            return rc;

        if (attempt < max_attempts)
            slash_printf(slash, "retrying same command (attempt %u/%u)\n",
                         attempt + 1, max_attempts);
    }

    slash_printf(slash,
                 "win_payload cmd %u failed after %u attempts\n",
                 (unsigned int) cmd,
                 max_attempts);
    if (rc == WIN_PAYLOAD_RETRYABLE_FAILURE)
        return SLASH_EIO;

    return rc;
}

int win_payload_send_request(struct slash *slash,
                         unsigned int node,
                         unsigned int timeout,
                         uint8_t cmd,
                         const char *arg) {
    size_t arg_len = 0;

    if (arg != NULL)
        arg_len = strlen(arg);

    return win_payload_send_request_raw_ex(slash, node, timeout, cmd, arg, arg_len,
                                       1, 1, NULL, NULL, 0);
}

int win_payload_send_request_retry(struct slash *slash,
                         unsigned int node,
                         unsigned int timeout,
                         uint8_t cmd,
                         const char *arg) {
    size_t arg_len = 0;

    if (arg != NULL)
        arg_len = strlen(arg);

    return win_payload_send_request_raw_retry_ex(slash, node, timeout, cmd, arg, arg_len,
                                       1, 1, NULL, NULL, 0);
}

int win_payload_query_remote_offset(struct slash *slash,
                                unsigned int node,
                                unsigned int timeout,
                                const char *remote_name,
                                uint32_t *offset_out,
                                int verbose) {
    uint8_t status = 0;
    char reply[256];
    int rc;

    rc = win_payload_send_request_raw_retry_ex(slash, node, timeout,
                                     CMD_PUT_FILE_QUERY,
                                     remote_name, strlen(remote_name),
                                     1, verbose, &status, reply, sizeof(reply));
    if (rc != 0 || status != 0)
        return -1;

    return win_payload_parse_numeric_reply(slash, reply, "offset=%lu", offset_out);
}

int win_payload_query_remote_size(struct slash *slash,
                              unsigned int node,
                              unsigned int timeout,
                              const char *remote_name,
                              uint32_t *size_out,
                              int verbose) {
    uint8_t status = 0;
    char reply[256];
    int rc;

    rc = win_payload_send_request_raw_retry_ex(slash, node, timeout,
                                     CMD_GET_FILE_INFO,
                                     remote_name, strlen(remote_name),
                                     1, verbose, &status, reply, sizeof(reply));
    if (rc != 0 || status != 0)
        return -1;

    return win_payload_parse_numeric_reply(slash, reply, "size=%lu", size_out);
}

int win_payload_get_file_chunk(struct slash *slash,
                           unsigned int node,
                           unsigned int timeout,
                           const char *remote_name,
                           uint32_t offset,
                           win_payload_file_read_resp_t *chunk_out) {
    csp_conn_t *conn;
    csp_packet_t *pkt;
    csp_packet_t *reply;
    win_payload_req_t *req;
    win_payload_resp_t *resp;
    uint8_t arg_buf[PAYLOAD_MAX_ARG_LEN];
    size_t remote_len;
    size_t arg_len;
    size_t raw_len;

    if (chunk_out == NULL)
        return -1;

    memset(chunk_out, 0, sizeof(*chunk_out));

    remote_len = strlen(remote_name);
    arg_len = sizeof(uint32_t) + remote_len + 1;
    if (arg_len > sizeof(arg_buf)) {
        slash_printf(slash, "Argument too long for get_file\n");
        return -1;
    }

    memcpy(arg_buf, &offset, sizeof(uint32_t));
    memcpy(arg_buf + sizeof(uint32_t), remote_name, remote_len + 1);

    conn = csp_connect(CSP_PRIO_NORM, (uint16_t) node, PAYLOAD_CMD_PORT, timeout, CSP_O_NONE);
    if (!conn) {
        slash_printf(slash, "Failed to connect to node %u port %u\n", node, PAYLOAD_CMD_PORT);
        return -1;
    }

    pkt = csp_buffer_get(sizeof(win_payload_req_t));
    if (!pkt) {
        slash_printf(slash, "Failed to allocate CSP packet\n");
        csp_close(conn);
        return -1;
    }

    req = (win_payload_req_t *) pkt->data;
    memset(req, 0, sizeof(*req));
    req->cmd = CMD_GET_FILE_DATA;
    req->arg_len = (uint16_t) arg_len;
    memcpy(req->arg, arg_buf, arg_len);
    pkt->length = (uint16_t) (offsetof(win_payload_req_t, arg) + arg_len);

    csp_send(conn, pkt);

    reply = csp_read(conn, timeout);
    if (!reply) {
        slash_printf(slash, "No reply from node %u\n", node);
        csp_close(conn);
        return -1;
    }

    if (reply->length < offsetof(win_payload_resp_t, data)) {
        slash_printf(slash, "Short reply from node %u\n", node);
        csp_buffer_free(reply);
        csp_close(conn);
        return -1;
    }

    resp = (win_payload_resp_t *) reply->data;

    if (resp->status != 0) {
        if (resp->data_len >= sizeof(resp->data))
            resp->data_len = sizeof(resp->data) - 1;
        resp->data[resp->data_len] = '\0';
        slash_printf(slash, "status=%u\n", resp->status);
        slash_printf(slash, "%s\n", resp->data);
        csp_buffer_free(reply);
        csp_close(conn);
        return -1;
    }

    if (resp->data_len < offsetof(win_payload_file_read_resp_t, data) ||
        resp->data_len > sizeof(resp->data)) {
        slash_printf(slash, "Malformed binary reply from node %u\n", node);
        csp_buffer_free(reply);
        csp_close(conn);
        return -1;
    }

    if (reply->length != offsetof(win_payload_resp_t, data) + resp->data_len) {
        slash_printf(slash, "Truncated binary reply from node %u\n", node);
        csp_buffer_free(reply);
        csp_close(conn);
        return -1;
    }

    raw_len = resp->data_len;
    memcpy(chunk_out, resp->data, raw_len);

    if (chunk_out->data_len > DOWNLOAD_CHUNK_SIZE ||
        raw_len != offsetof(win_payload_file_read_resp_t, data) + chunk_out->data_len) {
        slash_printf(slash, "Bad binary win_payload length from node %u\n", node);
        csp_buffer_free(reply);
        csp_close(conn);
        return -1;
    }

    csp_buffer_free(reply);
    csp_close(conn);
    return 0;
}

int win_payload_list_files_page(struct slash *slash,
                            unsigned int node,
                            unsigned int timeout,
                            const char *remote_dir,
                            uint32_t start_index,
                            uint32_t *next_index_out,
                            int *is_end_out,
                            char *items_out,
                            size_t items_out_sz) {
    uint8_t status = 0;
    char reply[256];
    uint8_t arg_buf[PAYLOAD_MAX_ARG_LEN];
    size_t arg_len = sizeof(uint32_t);
    const char *line_start;
    const char *newline;
    int rc;
    unsigned long next_val = 0;

    if (next_index_out == NULL || is_end_out == NULL || items_out == NULL || items_out_sz == 0)
        return -1;

    items_out[0] = '\0';
    memcpy(arg_buf, &start_index, sizeof(uint32_t));

    if (remote_dir != NULL && remote_dir[0] != '\0') {
        size_t dir_len = strlen(remote_dir) + 1;
        if (arg_len + dir_len > sizeof(arg_buf)) {
            slash_printf(slash, "List directory path too long\n");
            return -1;
        }
        memcpy(arg_buf + arg_len, remote_dir, dir_len);
        arg_len += dir_len;
    }

    rc = win_payload_send_request_raw_retry_ex(slash, node, timeout,
                                     CMD_LIST_FILES,
                                     arg_buf, arg_len,
                                     1, 0, &status, reply, sizeof(reply));
    if (rc != 0 || status != 0)
        return -1;

    line_start = reply;
    newline = strchr(line_start, '\n');
    if (newline == NULL) {
        slash_printf(slash, "Malformed list_files reply\n");
        return -1;
    }

    if (strncmp(line_start, "next=END", 8) == 0) {
        *is_end_out = 1;
        *next_index_out = 0;
    } else if (sscanf(line_start, "next=%lu", &next_val) == 1) {
        *is_end_out = 0;
        *next_index_out = (uint32_t) next_val;
    } else {
        slash_printf(slash, "Failed to parse list_files reply header\n");
        return -1;
    }

    snprintf(items_out, items_out_sz, "%s", newline + 1);
    return 0;
}
