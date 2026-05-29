#pragma once

#include <stdint.h>
#include <stddef.h>

#define PAYLOAD_CMD_PORT 12
#define PAYLOAD_MAX_ARG_LEN 320
#define MAX_REMOTE_FILENAME 55
#define MAX_REMOTE_PATH 260
#define UPLOAD_CHUNK_SIZE 224
#define UPLOAD_FLAG_ACK_REQUIRED 0x01
#define UPLOAD_ACK_EVERY 1
#define UPLOAD_MAX_RETRIES 30
#define DOWNLOAD_MAX_RETRIES 30
#define WIN_PAYLOAD_CMD_MAX_RETRIES 30

#define MAX_RESP_TEXT 248
#define DOWNLOAD_CHUNK_SIZE 240

typedef enum {
    CMD_GET_STATE        = 1,
    CMD_START_APP        = 2,
    CMD_STOP_APP         = 3,
    CMD_REBOOT_OS        = 4,
    CMD_CLEAR_TESTFOLDER = 6,
    CMD_PUT_FILE_BEGIN   = 7,
    CMD_PUT_FILE_DATA    = 8,
    CMD_PUT_FILE_END     = 9,
    CMD_PUT_FILE_QUERY   = 10,
    CMD_GET_FILE_INFO    = 11,
    CMD_GET_FILE_DATA    = 12,
    CMD_DELETE_FILE      = 13,
    CMD_LIST_FILES       = 14,
    CMD_MOVE_FILE        = 15,
    CMD_CREATE_FOLDER    = 16,
    CMD_DELETE_FOLDER    = 17
} win_payload_cmd_t;

typedef enum {
    RESP_OK               = 0,
    RESP_ERR_BAD_PACKET   = 1,
    RESP_ERR_UNKNOWN_CMD  = 2,
    RESP_ERR_BAD_ARG      = 3,
    RESP_ERR_NOT_FOUND    = 4,
    RESP_ERR_ALREADY_RUN  = 5,
    RESP_ERR_NOT_RUNNING  = 6,
    RESP_ERR_OS_FAILED    = 7
} win_payload_status_t;

#pragma pack(push, 1)
typedef struct {
    uint8_t  cmd;
    uint8_t  reserved;
    uint16_t arg_len;
    char     arg[PAYLOAD_MAX_ARG_LEN];
} win_payload_req_t;

typedef struct {
    uint8_t  status;
    uint8_t  reserved;
    uint16_t data_len;
    char     data[MAX_RESP_TEXT];
} win_payload_resp_t;

typedef struct {
    uint32_t session_id;
    uint32_t chunk_index;
    uint32_t offset;
    uint16_t data_len;
    uint8_t  flags;
    uint8_t  data[UPLOAD_CHUNK_SIZE];
} win_payload_file_chunk_t;

typedef struct {
    uint32_t offset;
    uint16_t data_len;
    uint8_t  eof;
    uint8_t  reserved;
    uint8_t  data[DOWNLOAD_CHUNK_SIZE];
} win_payload_file_read_resp_t;
#pragma pack(pop)
