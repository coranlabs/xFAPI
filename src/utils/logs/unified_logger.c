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

#define _GNU_SOURCE

#include "unified_logger.h"
#include <pthread.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>

int horizontalLevels_logs[5] = {1, 1, 1, 1, 1};
int horizontalLevels_log_file[5] = {1, 1, 1, 1, 1};

static logging_config_t g_log_cfg;
static log_file_config_t g_logfile_cfg;

char coranLabs_pdu_list[coranLabs_MAX_TEXTS][coranLabs_MAX_TEXT_LENGTH];
int coranLabs_num_texts = 0;

void save_logs_to_file(const char *filename) {
    if (!filename) {
        SM_Logs(LOG_ERROR, _XFAPI_, "File name is NULL. Cannot save logs.");
        return;
    }

    FILE *file = fopen(filename, "w");
    if (!file) {
        SM_Logs(LOG_ERROR, _XFAPI_, "Failed to open the file %s for writing.", filename);
        perror("Error opening file");
        return;
    }

    for (int i = 0; i < coranLabs_num_texts; i++) {
        fprintf(file, "%s", coranLabs_pdu_list[i]);
    }

    fclose(file);
    SM_Logs(LOG_INFO, _XFAPI_, "Logs saved to %s.", filename);
}

int get_log_level_from_string(const char *level_str) {
    if (strcasecmp(level_str, "LOG_CRTERR") == 0 || strcasecmp(level_str, "CRTERR") == 0)
        return 0;
    else if (strcasecmp(level_str, "LOG_ERROR") == 0 || strcasecmp(level_str, "ERROR") == 0)
        return 1;
    else if (strcasecmp(level_str, "LOG_NONE") == 0 || strcasecmp(level_str, "NONE") == 0)
        return 2;
    else if (strcasecmp(level_str, "LOG_WARN") == 0 || strcasecmp(level_str, "WARN") == 0)
        return 3;
    else if (strcasecmp(level_str, "LOG_INFO") == 0 || strcasecmp(level_str, "INFO") == 0)
        return 4;
    else if (strcasecmp(level_str, "LOG_DEBUG") == 0 || strcasecmp(level_str, "DEBUG") == 0)
        return 5;
    else if (strcasecmp(level_str, "LOG_SUPER") == 0 || strcasecmp(level_str, "SUPER") == 0)
        return 6;
    else
        return -1;
}

void logger_init(const xFAPI_Config* config) {

    g_log_cfg.horizontal_level.xFAPI_log = config->logging.horizontal_level.xFAPI_log;
    g_log_cfg.horizontal_level.xSM_log = config->logging.horizontal_level.xSM_log;
    g_log_cfg.horizontal_level.P5_log = config->logging.horizontal_level.P5_log;
    g_log_cfg.horizontal_level.P7_log = config->logging.horizontal_level.P7_log;

    horizontalLevels_logs[0] = config->logging.horizontal_level.xFAPI_log;
    horizontalLevels_logs[1] = config->logging.horizontal_level.xSM_log;
    horizontalLevels_logs[2] = config->logging.horizontal_level.P5_log;
    horizontalLevels_logs[3] = config->logging.horizontal_level.P7_log;

    strncpy(g_log_cfg.vertical_level, config->logging.vertical_level, sizeof(config->logging.vertical_level) - 1);

    g_log_cfg.print_config = config->logging.print_config;
    g_log_cfg.print_datetime = config->logging.print_datetime;

    g_logfile_cfg.horizontal_level.xFAPI_log = config->log_file.horizontal_level.xFAPI_log;
    g_logfile_cfg.horizontal_level.xSM_log = config->log_file.horizontal_level.xSM_log;
    g_logfile_cfg.horizontal_level.P5_log = config->log_file.horizontal_level.P5_log;
    g_logfile_cfg.horizontal_level.P7_log = config->log_file.horizontal_level.P7_log;

    horizontalLevels_log_file[0] = config->log_file.horizontal_level.xFAPI_log;
    horizontalLevels_log_file[1] = config->log_file.horizontal_level.xSM_log;
    horizontalLevels_log_file[2] = config->log_file.horizontal_level.P5_log;
    horizontalLevels_log_file[3] = config->log_file.horizontal_level.P7_log;

    strncpy(g_logfile_cfg.vertical_level, config->log_file.vertical_level, sizeof(config->log_file.vertical_level) - 1);

    g_logfile_cfg.file_size = config->log_file.file_size;
    g_logfile_cfg.generate_log_file = config->log_file.generate_log_file;
    g_logfile_cfg.print_config = config->log_file.print_config;
    g_logfile_cfg.print_datetime = config->log_file.print_datetime;
}

void display_date_time() {
    time_t rawtime;
    struct tm *dateandtime;
    char time_buffer[80];

    time(&rawtime);
    dateandtime = localtime(&rawtime);
    strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%dT%H:%M:%S", dateandtime);
    printf("%s[%s]%s ", BLUE_1, time_buffer, RESET_COLOR);
}

void Display(int vert_lvl, int horzt_lvl, char *text_to_print, int date_time_flag) {
    const char *vertical_names[] = {"CERROR", "ERROR", "NONE", "WARN", "INFO", "DEBUG", "SUPER"};
    const char *horizontal_names[] = {
        "XFAPI", "XSM", "P5", "P7"
    };

    if (vert_lvl < 0 || vert_lvl >= 7) vert_lvl = 2;
    if (horzt_lvl < 0 || horzt_lvl >= 8) horzt_lvl = 6;

    if (date_time_flag == 1) {
        display_date_time();
    }

    const char *level_color = (vert_lvl <= 1) ? RED : (vert_lvl == 3) ? ORANGE : BLUE_2;
    const char *text_color = (vert_lvl <= 1) ? RED : RESET_COLOR;

    printf("%s[%-6s]%s ", level_color, vertical_names[vert_lvl], RESET_COLOR);
    printf("%s[%-6s]%s ", BLUE_3, horizontal_names[horzt_lvl], RESET_COLOR);
    printf("%s%s%s\n", text_color, text_to_print, RESET_COLOR);

    text_to_print[0] = '\0';
}

void Display_file(int vert_lvl, int horzt_lvl, char *text_to_print, int date_time_flag) {
    char line_buffer[DEFAULT_LOGBUF_SIZE] = {0};
    char temp_buffer[DEFAULT_LOGBUF_SIZE];
    int buffer_length = 0;

    time_t rawtime;
    struct tm *dateandtime;
    char time_buffer[80];
    const char *vertical_names[] = {"CERROR", "ERROR", "NONE", "WARN", "INFO", "DEBUG", "SUPER"};
    const char *horizontal_names[] = {
        "XFAPI", "XSM", "P5", "P7"
    };

    if (date_time_flag) {
        time(&rawtime);
        dateandtime = localtime(&rawtime);
        strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%dT%H:%M:%S", dateandtime);
        buffer_length = snprintf(temp_buffer, sizeof(temp_buffer), "[%s] ", time_buffer);
        strncat(line_buffer, temp_buffer, buffer_length);
    }

    buffer_length = snprintf(temp_buffer, sizeof(temp_buffer), "[%-6s] ", vertical_names[vert_lvl]);
    strncat(line_buffer, temp_buffer, sizeof(line_buffer) - strlen(line_buffer) - 1);
    buffer_length = snprintf(temp_buffer, sizeof(temp_buffer), "[%-6s] ", horizontal_names[horzt_lvl]);
    strncat(line_buffer, temp_buffer, sizeof(line_buffer) - strlen(line_buffer) - 1);
    buffer_length = snprintf(temp_buffer, sizeof(temp_buffer), "%s\n", text_to_print);
    strncat(line_buffer, temp_buffer, sizeof(line_buffer) - strlen(line_buffer) - 1);

    if (coranLabs_num_texts < coranLabs_MAX_TEXTS) {
        strncpy(coranLabs_pdu_list[coranLabs_num_texts], line_buffer, DEFAULT_LOGBUF_SIZE);
        coranLabs_num_texts++;
    }

    text_to_print[0] = '\0';
}

void Display_file_big(int vert_lvl, int horzt_lvl, char *text_to_print, int date_time_flag) {
    char line_buffer[MAX_LOGBUF_SIZE] = {0};
    char temp_buffer[MAX_LOGBUF_SIZE];
    int buffer_length = 0;

    time_t rawtime;
    struct tm *dateandtime;
    char time_buffer[80];
    const char *vertical_names[] = {"CERROR", "ERROR", "NONE", "WARN", "INFO", "DEBUG", "SUPER"};
    const char *horizontal_names[] = {
        "XFAPI", "XSM", "P5", "P7"
    };

    if (date_time_flag) {
        time(&rawtime);
        dateandtime = localtime(&rawtime);
        strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%dT%H:%M:%S", dateandtime);
        buffer_length = snprintf(temp_buffer, sizeof(temp_buffer), "[%s] ", time_buffer);
        strncat(line_buffer, temp_buffer, buffer_length);
    }

    buffer_length = snprintf(temp_buffer, sizeof(temp_buffer), "[%-7s] ", vertical_names[vert_lvl]);
    strncat(line_buffer, temp_buffer, sizeof(line_buffer) - strlen(line_buffer) - 1);
    buffer_length = snprintf(temp_buffer, sizeof(temp_buffer), "[%-8s] ", horizontal_names[horzt_lvl]);
    strncat(line_buffer, temp_buffer, sizeof(line_buffer) - strlen(line_buffer) - 1);
    buffer_length = snprintf(temp_buffer, sizeof(temp_buffer), "%s\n", text_to_print);
    strncat(line_buffer, temp_buffer, sizeof(line_buffer) - strlen(line_buffer) - 1);

    if (coranLabs_num_texts < coranLabs_MAX_TEXTS) {
        strncpy(coranLabs_pdu_list[coranLabs_num_texts], line_buffer, MAX_LOGBUF_SIZE);
        coranLabs_num_texts++;
    }

    text_to_print[0] = '\0';
}

void Write_to_log_file(int vert_lvl, int horzt_lvl, const char *text_to_print, va_list args) {
    char log_buffer[DEFAULT_LOGBUF_SIZE] = {0};

    vsnprintf(log_buffer, sizeof(log_buffer), text_to_print, args);
    va_end(args);

    if (vert_lvl <= LOG_ERROR || (vert_lvl <= get_log_level_from_string(g_logfile_cfg.vertical_level) && horizontalLevels_log_file[horzt_lvl] == ENABLE)) {
        Display_file(vert_lvl, horzt_lvl, log_buffer, g_logfile_cfg.print_datetime);
    }
}

void SM_Logs(int vert_lvl, int horzt_lvl, const char *text_to_print, ...) {
    va_list args;
    va_start(args, text_to_print);

    if (g_logfile_cfg.generate_log_file == ENABLE ) {
        va_list args_for_file;
        va_copy(args_for_file, args);

        Write_to_log_file(vert_lvl, horzt_lvl, text_to_print,args_for_file);
    }

    char log_buffer[DEFAULT_LOGBUF_SIZE] = {0};
    vsnprintf(log_buffer, sizeof(log_buffer), text_to_print, args);
    va_end(args);

    if (vert_lvl <= LOG_ERROR || (vert_lvl <= get_log_level_from_string(g_log_cfg.vertical_level) && horizontalLevels_logs[horzt_lvl] == ENABLE)) {
        Display(vert_lvl, horzt_lvl, log_buffer, g_log_cfg.print_datetime);
        if (vert_lvl == LOG_CRTERR) {
            printf("\n%s================= CRITICAL ERROR, Aborting =================%s\n", RED, RESET_COLOR);
            abort();
        }
    }
}

void SM_Logs_Buffer(int vert_lvl,int horzt_lvl,const char *log_prefix, const uint8_t *buffer, uint32_t length) {
    char buffer_log[length * 3 + (length / 8) + (length / 16) + 1];
    buffer_log[0] = '\0';

    for (uint32_t i = 0; i < length; i++) {
        char byte_hex[4];
        snprintf(byte_hex, sizeof(byte_hex), "%02X ", buffer[i]);
        strcat(buffer_log, byte_hex);

        if ((i + 1) % 8 == 0 && (i + 1) % 16 != 0) {
            strcat(buffer_log, " ");
        }

        if ((i + 1) % 16 == 0 && (i + 1) < length) {
            strcat(buffer_log, "\n");
        }
    }

    SM_Logs(vert_lvl, horzt_lvl, "%s:\n%s \n\n", log_prefix, buffer_log);
}

void SM_LogCheck(int horzt_level, int condition, const char *format, ...) {
    if (!condition) {
        char log_buffer[1024];
        va_list args;
        va_start(args, format);
        vsnprintf(log_buffer, sizeof(log_buffer), format, args);
        va_end(args);
        SM_Logs(LOG_CRTERR, horzt_level, "Condition check failed: %s", log_buffer);
    }
}
