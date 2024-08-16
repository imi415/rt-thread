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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* RT-Thread */
#include <finsh.h>
#include <rtthread.h>
#include <webnet.h>
#include <wn_module.h>

#define AW_ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

#define AW_WEBROOT "/webroot"

typedef int (*app_webserver_handler_t)(int argc, char **argv);

typedef struct {
    const char *command;
    const char *help;
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

static void app_webserver_cgi_capture(struct webnet_session *session) {
    RT_ASSERT(session != RT_NULL);

    const char *response = "{\"status\": \"success\"}\r\n";
    const char *mimetype = mime_get_type(".json");

    /* TODO: Capture image and save to FS, return file name relative to webroot. */

    webnet_session_set_header(session, mimetype, 200, "OK", rt_strlen(response));
    webnet_session_write(session, (const rt_uint8_t *)response, rt_strlen(response));
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