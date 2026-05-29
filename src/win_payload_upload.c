#include "win_payload_upload.h"
#include "win_payload_client.h"
#include "win_payload_proto.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>
#include <stdlib.h>
#include <stddef.h>

#include <apm/csh_api.h>

const char *win_payload_basename(const char *path) {
    const char *p1 = strrchr(path, '/');
    const char *p2 = strrchr(path, '\\');
    const char *base = path;

    if (p1 && p2)
        base = (p1 > p2) ? (p1 + 1) : (p2 + 1);
    else if (p1)
        base = p1 + 1;
    else if (p2)
        base = p2 + 1;

    return base;
}

int win_payload_validate_remote_name(const char *name) {
    size_t i;

    if (name == NULL || name[0] == '\0')
        return -1;

    if (strlen(name) > MAX_REMOTE_FILENAME)
        return -1;

    for (i = 0; name[i] != '\0'; i++) {
        unsigned char c = (unsigned char) name[i];

        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' ||
            c == '"'  || c == '<' || c == '>' || c == '|')
            return -1;

        if (c == '.' && name[i + 1] == '.')
            return -1;

        if (!(isalnum(c) || c == '_' || c == '-' || c == '.'))
            return -1;
    }

    return 0;
}

static int win_payload_is_absolute_windows_path(const char *path) {
    unsigned char c0;

    if (path == NULL || path[0] == '\0')
        return 0;

    if ((path[0] == '\\' || path[0] == '/') &&
        (path[1] == '\\' || path[1] == '/')) {
        return 1;
    }

    c0 = (unsigned char) path[0];
    if (isalpha(c0) && path[1] == ':' && (path[2] == '\\' || path[2] == '/'))
        return 1;

    return 0;
}

int win_payload_validate_remote_get_path(const char *path) {
    size_t i;
    size_t len;
    size_t seg_len = 0;

    if (path == NULL || path[0] == '\0')
        return -1;

    if (!win_payload_is_absolute_windows_path(path)) {
        len = strlen(path);
        if (len > MAX_REMOTE_PATH)
            return -1;

        for (i = 0; i < len; i++) {
            unsigned char c = (unsigned char) path[i];

            if (c == '\\' || c == '/') {
                if (seg_len == 0)
                    return -1;
                seg_len = 0;
                continue;
            }

            if (c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                return -1;

            if (c < 32)
                return -1;

            if (!(isalnum(c) || c == '_' || c == '-' || c == '.'))
                return -1;

            seg_len++;
            if (seg_len >= MAX_REMOTE_FILENAME)
                return -1;
        }

        if (seg_len == 0)
            return -1;

        if (strstr(path, "..") != NULL)
            return -1;

        return 0;
    }

    len = strlen(path);
    if (len > MAX_REMOTE_PATH)
        return -1;

    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char) path[i];

        if (c < 32)
            return -1;

        if (c == '*' || c == '?' || c == '"' || c == '<' || c == '>')
            return -1;
    }

    return 0;
}

int win_payload_put_file_cmd_impl(struct slash *slash) {
    unsigned int node = slash_dfl_node;
    unsigned int timeout = slash_dfl_timeout;
    unsigned int upload_timeout = (timeout < 3000) ? 3000 : timeout;
    const char *local_path;
    const char *remote_name;
    FILE *fp;
    uint8_t file_buf[UPLOAD_CHUNK_SIZE];
    long file_size_long;
    uint32_t file_size;
    uint32_t remote_offset = 0;
    uint32_t start_offset = 0;
    uint32_t session_id;
    uint32_t chunk_index;
    uint32_t offset;
    uint8_t status = 0;
    int rc;

    if (slash->argc < 2) {
        slash_printf(slash, "Usage: win_payload put_file <local_path> [remote_name_or_full_path]\n");
        return SLASH_EINVAL;
    }

    local_path = slash->argv[1];
    remote_name = (slash->argc >= 3) ? slash->argv[2] : win_payload_basename(local_path);

    if (win_payload_validate_remote_get_path(remote_name) != 0) {
        slash_printf(slash, "Invalid remote destination path\n");
        return SLASH_EINVAL;
    }

    fp = fopen(local_path, "rb");
    if (!fp) {
        slash_printf(slash, "Failed to open local file: %s\n", local_path);
        return SLASH_SUCCESS;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        slash_printf(slash, "Failed to seek local file\n");
        fclose(fp);
        return SLASH_SUCCESS;
    }

    file_size_long = ftell(fp);
    if (file_size_long < 0 || file_size_long > 0x7FFFFFFFL) {
        slash_printf(slash, "Invalid local file size\n");
        fclose(fp);
        return SLASH_SUCCESS;
    }

    file_size = (uint32_t) file_size_long;
    rewind(fp);

    if (win_payload_query_remote_offset(slash, node, upload_timeout, remote_name, &remote_offset, 0) != 0) {
        fclose(fp);
        return SLASH_SUCCESS;
    }

    start_offset = remote_offset;

    if (start_offset > file_size) {
        slash_printf(slash, "Remote offset beyond local file, restarting from 0\n");
        start_offset = 0;
    }

    if ((start_offset % UPLOAD_CHUNK_SIZE) != 0 && start_offset != file_size) {
        slash_printf(slash, "Remote offset is not chunk-aligned, restarting from 0\n");
        start_offset = 0;
    }

    srand((unsigned int) time(NULL) ^ (unsigned int) getpid());
    session_id = (uint32_t) time(NULL) ^ (uint32_t) getpid() ^ (uint32_t) rand();

    {
        uint8_t begin_buf[PAYLOAD_MAX_ARG_LEN];
        size_t remote_len = strlen(remote_name);
        size_t begin_len = sizeof(uint32_t) + sizeof(uint32_t) + remote_len + 1;

        if (begin_len > sizeof(begin_buf)) {
            slash_printf(slash, "Remote destination path too long\n");
            fclose(fp);
            return SLASH_SUCCESS;
        }

        memcpy(begin_buf, &session_id, sizeof(uint32_t));
        memcpy(begin_buf + sizeof(uint32_t), &start_offset, sizeof(uint32_t));
        memcpy(begin_buf + sizeof(uint32_t) + sizeof(uint32_t), remote_name, remote_len + 1);

        rc = win_payload_send_request_raw_retry_ex(slash, node, upload_timeout,
                                             CMD_PUT_FILE_BEGIN,
                                             begin_buf, begin_len,
                                             1, 1, &status, NULL, 0);
        if ((rc != 0) || (status != 0)) {
            fclose(fp);
            return SLASH_SUCCESS;
        }
    }

    if (fseek(fp, (long) start_offset, SEEK_SET) != 0) {
        slash_printf(slash, "Failed to seek local file to resume offset\n");
        fclose(fp);
        return SLASH_SUCCESS;
    }

    chunk_index = start_offset / UPLOAD_CHUNK_SIZE;
    offset = start_offset;
    slash_printf(slash, "upload resume offset: %lu\n", (unsigned long) start_offset);

    while (offset < file_size) {
        size_t remaining = (size_t) (file_size - offset);
        size_t nread = (remaining > UPLOAD_CHUNK_SIZE) ? UPLOAD_CHUNK_SIZE : remaining;
        win_payload_file_chunk_t chunk;
        int tries = 0;
        int sent = 0;

        if (fread(file_buf, 1, nread, fp) != nread) {
            slash_printf(slash, "Failed to read local file\n");
            fclose(fp);
            return SLASH_SUCCESS;
        }

        memset(&chunk, 0, sizeof(chunk));
        chunk.session_id = session_id;
        chunk.chunk_index = chunk_index;
        chunk.offset = offset;
        chunk.data_len = (uint16_t) nread;
        chunk.flags = ((chunk_index % UPLOAD_ACK_EVERY) == 0) ? UPLOAD_FLAG_ACK_REQUIRED : 0;
        memcpy(chunk.data, file_buf, nread);

        while (!sent && tries < UPLOAD_MAX_RETRIES) {
            rc = win_payload_send_request_raw_ex(slash, node, upload_timeout,
                                                 CMD_PUT_FILE_DATA,
                                                 &chunk,
                                                 offsetof(win_payload_file_chunk_t, data) + nread,
                                                 1,
                                                 chunk.flags ? 1 : 0,
                                                 &status,
                                                 NULL,
                                                 0);
            if (rc == 0 && status == 0) {
                sent = 1;
                break;
            }

            tries++;
            slash_printf(slash, "retry chunk %lu offset %lu (try %d/%d)\n",
                         (unsigned long) chunk_index,
                         (unsigned long) offset,
                         tries,
                         UPLOAD_MAX_RETRIES);
        }

        if (!sent) {
            slash_printf(slash, "upload failed after retries\n");
            fclose(fp);
            return SLASH_SUCCESS;
        }

        offset += (uint32_t) nread;
        chunk_index++;
    }

    fclose(fp);
    slash_printf(slash, "sent %lu bytes, finalizing...\n", (unsigned long) offset);

    rc = win_payload_send_request_raw_retry_ex(slash, node, upload_timeout,
                                         CMD_PUT_FILE_END,
                                         &session_id, sizeof(session_id),
                                         1, 1, &status, NULL, 0);
    if ((rc != 0) || (status != 0))
        return SLASH_SUCCESS;

    return SLASH_SUCCESS;
}

int win_payload_get_file_cmd_impl(struct slash *slash) {
    unsigned int node = slash_dfl_node;
    unsigned int timeout = slash_dfl_timeout;
    unsigned int download_timeout = (timeout < 3000) ? 3000 : timeout;
    const char *remote_name;
    const char *local_path;
    FILE *fp = NULL;
    FILE *probe = NULL;
    long local_size_long = 0;
    uint32_t remote_size = 0;
    uint32_t offset = 0;

    if (slash->argc < 2) {
        slash_printf(slash, "Usage: win_payload get_file <remote_name_or_full_path> [local_path]\n");
        return SLASH_EINVAL;
    }

    remote_name = slash->argv[1];
    local_path = (slash->argc >= 3) ? slash->argv[2] : win_payload_basename(remote_name);

    if (win_payload_validate_remote_get_path(remote_name) != 0) {
        slash_printf(slash, "Invalid remote path\n");
        return SLASH_EINVAL;
    }

    if (win_payload_query_remote_size(slash, node, download_timeout, remote_name, &remote_size, 1) != 0)
        return SLASH_SUCCESS;

    probe = fopen(local_path, "rb");
    if (probe) {
        if (fseek(probe, 0, SEEK_END) != 0) {
            slash_printf(slash, "Failed to seek local file\n");
            fclose(probe);
            return SLASH_SUCCESS;
        }

        local_size_long = ftell(probe);
        fclose(probe);

        if (local_size_long < 0 || local_size_long > 0x7FFFFFFFL) {
            slash_printf(slash, "Invalid local file size\n");
            return SLASH_SUCCESS;
        }

        offset = (uint32_t) local_size_long;
    }

    if (offset > remote_size) {
        slash_printf(slash, "Local file is larger than remote file, restarting from 0\n");
        offset = 0;
    }

    fp = fopen(local_path, (offset > 0) ? "ab" : "wb");
    if (!fp) {
        slash_printf(slash, "Failed to open local output file: %s\n", local_path);
        return SLASH_SUCCESS;
    }

    slash_printf(slash, "download resume offset: %lu / %lu\n",
                 (unsigned long) offset,
                 (unsigned long) remote_size);

    while (offset < remote_size) {
        win_payload_file_read_resp_t chunk;
        int tries = 0;
        int got_chunk = 0;

        while (!got_chunk && tries < DOWNLOAD_MAX_RETRIES) {
        if (win_payload_get_file_chunk(slash, node, download_timeout, remote_name, offset, &chunk) == 0) {
                got_chunk = 1;
                break;
            }

            tries++;
            slash_printf(slash, "retry download offset %lu (try %d/%d)\n",
                         (unsigned long) offset,
                         tries,
                         DOWNLOAD_MAX_RETRIES);
        }

        if (!got_chunk) {
            slash_printf(slash, "download failed after retries\n");
            fclose(fp);
            return SLASH_SUCCESS;
        }

        if (chunk.offset != offset) {
            slash_printf(slash, "Offset mismatch in reply\n");
            fclose(fp);
            return SLASH_SUCCESS;
        }

        if (chunk.data_len > DOWNLOAD_CHUNK_SIZE) {
            slash_printf(slash, "Chunk too large in reply\n");
            fclose(fp);
            return SLASH_SUCCESS;
        }

        if (chunk.data_len == 0 && !chunk.eof) {
            slash_printf(slash, "Empty non-EOF chunk received\n");
            fclose(fp);
            return SLASH_SUCCESS;
        }

        if (chunk.data_len > 0) {
            if (fwrite(chunk.data, 1, chunk.data_len, fp) != chunk.data_len) {
                slash_printf(slash, "Failed to write local file\n");
                fclose(fp);
                return SLASH_SUCCESS;
            }

            slash_printf(slash, "received %u bytes, offset now %lu / %lu\n",
                         (unsigned int) chunk.data_len,
                         (unsigned long) (offset + chunk.data_len),
                         (unsigned long) remote_size);
        }

        offset += chunk.data_len;

        if (chunk.eof)
            break;
    }

    fclose(fp);

    if (offset != remote_size) {
        slash_printf(slash, "Download ended at %lu but remote size is %lu\n",
                     (unsigned long) offset,
                     (unsigned long) remote_size);
        return SLASH_SUCCESS;
    }

    slash_printf(slash, "download complete: %s (%lu bytes)\n",
                 local_path,
                 (unsigned long) offset);
    return SLASH_SUCCESS;
}

int win_payload_delete_file_cmd_impl(struct slash *slash) {
    unsigned int node = slash_dfl_node;
    unsigned int timeout = slash_dfl_timeout;
    const char *remote_name_or_path;

    if (slash->argc < 2) {
        slash_printf(slash, "Usage: win_payload delete_file <remote_name_or_full_path>\n");
        return SLASH_EINVAL;
    }

    remote_name_or_path = slash->argv[1];

    if (win_payload_validate_remote_get_path(remote_name_or_path) != 0) {
        slash_printf(slash, "Invalid remote path\n");
        return SLASH_EINVAL;
    }

    return win_payload_send_request(slash, node, timeout, CMD_DELETE_FILE, remote_name_or_path);
}
