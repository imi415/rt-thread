/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2024-08-07     Yilin Sun    first version
 *
 */

#include <dirent.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* RT-Thread */
#include <finsh.h>
#include <rtthread.h>
#include <webnet.h>
#include <wn_module.h>

/* cJSON */
#include "cJSON.h"

/* App */
#include "app_camera.h"

#define AW_ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

#define AW_WEBROOT "/webroot"

typedef int (*app_webserver_handler_t)(int argc, char **argv);

typedef struct {
    const char             *command;
    const char             *help;
    app_webserver_handler_t handler;
} app_webserver_opts_t;

static void app_webserver_cgi_capture(struct webnet_session *session);

static int app_webserver_cmd_help(int argc, char **argv);
static int app_webserver_cmd_start(int argc, char **argv);
static int app_webserver_cmd_stop(int argc, char **argv);

static const app_webserver_opts_t s_app_webserver_opts[] = {
    {.command = "help", .help = "print this help.", .handler = app_webserver_cmd_help},
    {.command = "start", .help = "start internal web server.", .handler = app_webserver_cmd_start},
    {.command = "stop", .help = "stop internal web server.", .handler = app_webserver_cmd_stop},
};

/**
 * Capture an image and save to internal file system.
 * @param session WebNet session pointer.
 */
static void app_webserver_cgi_capture(struct webnet_session *session) {
    RT_ASSERT(session != RT_NULL);

    const char *mimetype = mime_get_type(".json");
    char       *response = rt_malloc(128);
    char        filename[64];
    rt_tick_t   curr_tick = rt_tick_get();

    rt_snprintf(filename, 64, "/webroot/images/%010ld.bmp", curr_tick);
    rt_snprintf(response, 128, "{\"status\": \"success\", \"path\": \"/images/%010ld.bmp\"}\r\n", curr_tick);

    app_camera_capture(filename);

    webnet_session_set_header(session, mimetype, 200, "OK", rt_strlen(response));
    webnet_session_write(session, (const rt_uint8_t *)response, rt_strlen(response));

    rt_free(response);
}

/**
 * Stream a live image from camera, does not save to filesystem.
 * @param session WebNet session pointer.
 */
static void app_webserver_cgi_live(struct webnet_session *session) {
    const char *mimetype = mime_get_type(".bmp");

    app_camera_capture("/ram/image.bmp");

    struct stat file_stat;
    stat("/ram/image.bmp", &file_stat);

    int fd = open("/ram/image.bmp", O_RDONLY);

    /* TODO: Check return values. */

    webnet_session_set_header(session, mimetype, 200, "OK", file_stat.st_size);

    uint8_t data[64];

    while (1) {
        const int br = read(fd, data, 64);
        if (br != 0) {
            webnet_session_write(session, (const rt_uint8_t *)data, br);
        } else {
            break;
        }
    }

    close(fd);
}

static void app_webserver_cgi_list(struct webnet_session *session) {
    const char *mimetype = mime_get_type(".json");

    char path[64];

    cJSON *j_root = cJSON_CreateObject();
    if (!j_root) return;

    cJSON *j_items = cJSON_CreateArray();
    if (j_items == NULL) goto del_json_exit;
    cJSON_AddItemToObject(j_root, "items", j_items);

    DIR *dp = opendir("/webroot/images");

    while (1) {
        struct dirent *ep = readdir(dp);
        if (ep == NULL) {
            break;
        }

        rt_snprintf(path, 64, "/images/%s", ep->d_name);

        cJSON *j_path = cJSON_CreateString(path);
        if (j_path == NULL) goto close_dir_exit;

        cJSON_AddItemToArray(j_items, j_path);
    }

    char *payload = cJSON_PrintUnformatted(j_root);
    webnet_session_set_header(session, mimetype, 200, "OK", strlen(payload));
    webnet_session_write(session, (rt_uint8_t *)payload, strlen(payload));

    cJSON_free(payload);

close_dir_exit:
    closedir(dp);

del_json_exit:
    cJSON_Delete(j_root);
}

static int app_webserver_cmd_help(int argc, char **argv) {
    rt_kprintf("Usage: webserver <COMMAND> [ARGS]\n");
    rt_kprintf("Commands:\n");

    for (size_t i = 0; i < AW_ARRAY_SIZE(s_app_webserver_opts); i++) {
        const app_webserver_opts_t *opt = &s_app_webserver_opts[i];

        rt_kprintf("\t%s\t- %s\n", opt->command, opt->help);
    }

    return 0;
}

static int app_webserver_cmd_start(int argc, char **argv) {
    if (argc != 1) {
        rt_kprintf("Usage: webserver start <PORT>\n");
        return -1;
    }

    int port = strtol(argv[0], NULL, 10);

    if (port <= 0 || port > 65535) {
        rt_kprintf("Invalid port %d.\n", port);
        return -1;
    }

    webnet_set_root(AW_WEBROOT);
    webnet_set_port(port);

    webnet_cgi_register("capture.cgi", app_webserver_cgi_capture);
    webnet_cgi_register("live.cgi", app_webserver_cgi_live);
    webnet_cgi_register("list.cgi", app_webserver_cgi_list);

    rt_kprintf("Web server listening on port %d\n", port);

    webnet_init();

    return 0;
}

static int app_webserver_cmd_stop(int argc, char **argv) {
    rt_kprintf("Web server does not support stop command...\n");
    return 0;
}

static int app_webserver_command(int argc, char **argv) {
    if (argc < 2) goto print_help_exit;

    char *command = argv[1];

    for (size_t i = 0; i < AW_ARRAY_SIZE(s_app_webserver_opts); i++) {
        const app_webserver_opts_t *opt = &s_app_webserver_opts[i];

        if (strncmp(opt->command, command, strlen(opt->command)) == 0) {
            return opt->handler(argc - 2, &argv[2]);
        }
    }

    rt_kprintf("Unknown command %s\n", command);

print_help_exit:
    return app_webserver_cmd_help(0, NULL);
}

MSH_CMD_EXPORT_ALIAS(app_webserver_command, webserver, Manage internal Web server);