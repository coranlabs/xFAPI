// Copyright 2024-2026 coRAN LABS Private Limited
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef UNIFIED_LOGGER_H_
#define UNIFIED_LOGGER_H_

#include "common_global.h"

struct xFAPI_Config;

typedef struct {
    char *name;
    int value;
} mapping;

int mapStrToInt(const mapping *map, const char *str);
char *mapIntToStr(const mapping *map, const int val);

#ifdef LOG_CRTERR
#undef LOG_CRTERR
#endif
#ifdef LOG_ERROR
#undef LOG_ERROR
#endif
#ifdef LOG_NONE
#undef LOG_NONE
#endif
#ifdef LOG_WARN
#undef LOG_WARN
#endif
#ifdef LOG_INFO
#undef LOG_INFO
#endif
#ifdef LOG_DEBUG
#undef LOG_DEBUG
#endif
#ifdef LOG_SUPER
#undef LOG_SUPER
#endif
#define LOG_CRTERR  0
#define LOG_ERROR   1
#define LOG_NONE    2
#define LOG_WARN    3
#define LOG_INFO    4
#define LOG_DEBUG   5
#define LOG_SUPER   6

#define _XFAPI_     0
#define _XSM_       1
#define _P5_        2
#define _P7_        3

#define coranLabs_MAX_TEXTS 100000
#define coranLabs_MAX_TEXT_LENGTH 10000

#define DEFAULT_LOGBUF_SIZE 512
#define MAX_LOGBUF_SIZE 10000

extern char coranLabs_pdu_list[coranLabs_MAX_TEXTS][coranLabs_MAX_TEXT_LENGTH];
extern int coranLabs_num_texts;
void save_logs_to_file(const char *filename);

#define ENABLE  1
#define DISABLE 0

#define ORANGE     "\033[38;2;255;165;0m"
#define YELLOW     "\033[38;2;255;255;0m"
#define PURPLE     "\033[38;2;166;96;198m"
#define CYAN       "\033[38;2;96;192;198m"
#define RED        "\x1b[31m"
#define BLUE_1     "\033[38;2;186;198;255m"
#define BLUE_2     "\033[38;2;100;125;253m"
#define BLUE_3     "\033[38;2;0;35;212m"
#define GREEN   "\033[0;32m"
#define RESET_COLOR "\033[0m"

void Display(int vert_lvl, int horzt_lvl, char *text_to_print, int date_time_flag);
void Display_file(int vert_lvl, int horzt_lvl, char *text_to_print, int date_time_flag);
void Display_file_big(int vert_lvl, int horzt_lvl, char *text_to_print, int date_time_flag);
void SM_Logs(int vert_lvl, int horzt_lvl, const char *text_to_print, ...);
void SM_LogCheck(int horzt_level, int condition, const char *format, ...);
void SM_Logs_Buffer(int vert_lvl,int horzt_lvl,const char *log_prefix, const uint8_t *buffer, uint32_t length);
void logger_init(const xFAPI_Config* config);

typedef struct _Horizontal_lvlType {
    bool xFAPI_log;
    bool xSM_log;
    bool P5_log;
    bool P7_log;
} Horizontal_lvlType_t;

typedef struct _LogsConfs {
    uint8_t vertical_level;
    Horizontal_lvlType_t horizontal_level;
    bool print_datetime;
    int print_logs;
} LogsConfs_t;

typedef struct _LogsFileConfs {
    uint8_t vertical_level;
    Horizontal_lvlType_t horizontal_level;
    bool print_datetime;
    int print_configurations;
    int generate_log_file;
} LogsFileConfs_t;

typedef struct _nr5g_fapi_cfg {
    LogsConfs_t logConfigs;
    LogsFileConfs_t logfileConfigs;
} CUlogsConfs_t;

extern CUlogsConfs_t CUglobalConfigs;

#endif
