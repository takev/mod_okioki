/* mod_okioki is an apche module which provides a RESTful data service.
 * Copyright (C) 2010  Take Vos
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <sys/types.h>
#include <regex.h>
#include <httpd.h>
#include <http_config.h>
#include <http_core.h>
#include <http_log.h>
#include <http_main.h>
#include <http_protocol.h>
#include <http_request.h>
#include <apr_atomic.h>
#include <apr_hash.h>
#include <apr_strings.h>
#include <libpq-fe.h>
#include "mod_okioki.h"
#include "views.h"
#include "cookies.h"
#include "urlencoding.h"
#include "csv.h"

module AP_MODULE_DECLARE_DATA okioki_module;

/** Allocate per-directory configuration structure.
 * The structure contains information on how to connect to the backend database.
 * It also contains a resource pool of database connections.
 *
 * @param pool   Memory pool to allocate structure on.
 * @param dir    Name of the directory.
 * @returns      The configuration structure.
 */
static void *mod_okioki_create_dir_config(apr_pool_t *pool, char *dir)
{
    mod_okioki_dir_config *new_cfg;

    // Allocate structure.
    ASSERT_NOT_NULL(
        new_cfg = (mod_okioki_dir_config *)apr_pcalloc(pool, sizeof (mod_okioki_dir_config)),
        NULL, "[mod_okioki] Failed to allocate per-directory config."
    )

    ASSERT_NOT_NULL(
        new_cfg->views = apr_hash_make(pool),
        NULL, "[mod_okioki] Failed to allocate views hash table."
    )

    return (void *)new_cfg;
}

/** This is the main handler for any request withing some folder.
 * It will find a view based on the value of PATH_INFO.
 * Then it will get a free postgresql connection and pass it on to the view.
 * When finished it will return the connection back to the pool.
 *
 * @param http_request  Information about the http_request.
 * @returns             HTTP status.
 */
static int mod_okioki_handler(request_rec *http_request)
{
    apr_pool_t              *pool = http_request->pool;
    apr_pool_t              *bucket_pool = http_request->connection->pool;
    apr_bucket_alloc_t      *bucket_alloc = http_request->connection->bucket_alloc;
    mod_okioki_dir_config   *cfg;
    view_t                  *view;
    apr_hash_t              *arguments;
    apr_bucket_brigade      *bb_out;
    int                     ret;
    const apr_dbd_driver_t  *db_driver;
    apr_dbd_results_t       *db_result;

    // Check if we need to process the request. We only need to if the handler is set to "okioki-handler".
    if (http_request->handler == NULL || (strcmp(http_request->handler, "okioki-handler") != 0)) {
        return DECLINED;
    }

    // Retrieve the per-directory configuration for this request.
    ASSERT_NOT_NULL(
        cfg = (mod_okioki_dir_config *)ap_get_module_config(http_request->per_dir_config, &okioki_module),
        HTTP_INTERNAL_SERVER_ERROR, "[mod_okioki] Could not get the per-directory config."
    )

    // Create hash table.
    ASSERT_NOT_NULL(
        arguments = apr_hash_make(pool),
        HTTP_INTERNAL_SERVER_ERROR, "[mod_okioki] Failed to allocate argument table."
    )

    // Find a view matching the url.
    ASSERT_NOT_NULL(
        view = apr_hash_get(cfg->views, http_request->path_info, APR_HASH_KEY_STRING),
        HTTP_NOT_FOUND, "[mod_okioki] Could not find view for '%s'.", http_request->path_info
    )

    // Extract cookies from request.
    ASSERT_HTTP_OK(
        ret = mod_okioki_get_cookies(http_request, arguments),
        ret, "[mod_okioki] Could not parse http-cookies."
    )

    // Extract parameters from the query string.
    ASSERT_HTTP_OK(
        ret = mod_okioki_parse_query(http_request, arguments, http_request->args),
        ret, "[mod_okioki] Could not parse url-query."
    )

    // Extract parameters from the POST/PUT data.
    ASSERT_HTTP_OK(
        ret = mod_okioki_parse_posted_query(http_request, arguments),
        ret, "[mod_okioki] Could not parse posted-query."
    )

    // Handle the view.
    ASSERT_HTTP_OK(
        ret = mod_okioki_view_execute(http_request, cfg, view, arguments, &db_driver, &db_result),
        ret, "[mod_okioki] Could not execute view"
    )

    // Convert result to csv string.
    if (db_result != NULL) {
        ASSERT_NOT_NULL(
            bb_out = mod_okioki_generate_csv(bucket_pool, bucket_alloc, view, db_driver, db_result),
            HTTP_INTERNAL_SERVER_ERROR, "[mod_okioki] Could not generate text/csv."
        )
        ap_set_content_type(http_request, "text/csv");
        return ap_pass_brigade(http_request->output_filters, bb_out);
    }

    return HTTP_OK;    
}

/** This function setups all the handlers at startup.
 * @param pool  The memory pool in case we need to allocate anything.
 */
static void mod_okioki_register_hooks(apr_pool_t *pool)
{
    // Setup a standard request handler.
    ap_hook_handler(mod_okioki_handler, NULL, NULL, APR_HOOK_LAST);
}

/** Process the OkiokiSetCommand configuration directive.
 */
const char *mod_okioki_dircfg_set_command(cmd_parms *cmd, void *_conf, int argc, char *const argv[])
{
    apr_pool_t            *pool      = cmd->pool;
    mod_okioki_dir_config *conf      = (mod_okioki_dir_config *)_conf;
    view_t                *view;
    unsigned int          i;
    char                  *param;
    char                  *link;

    // Make sure this configuration directive has at least two arguments.
    if (argc < 4) {
        return "[OkiokiSetCommand] Requires at least four arguments.";
    }

    // Create a new view.
    if ((view = (view_t *)apr_pcalloc(pool, sizeof (view_t))) == NULL) {
        return "[OkiokiSetCommand] Could not allocate view.";
    }

    // Copy the link, as the hash table remembers only a reference.
    if ((link = apr_pstrdup(pool, argv[1])) == NULL) {
        return "[OkiokiSetCommand] Could not copy link.";
    }

    // Add the view to the hash table.
    apr_hash_set(conf->views, link, APR_HASH_KEY_STRING, view);

    // Decode the command.
    if (strcmp(argv[0], "GET") == 0) {
        view->link_cmd = M_GET;
    } else if (strcmp(argv[0], "POST") == 0) {
        view->link_cmd = M_POST;
    } else if (strcmp(argv[0], "PUT") == 0) {
        view->link_cmd = M_PUT;
    } else if (strcmp(argv[0], "DELETE") == 0) {
        view->link_cmd = M_DELETE;
    } else {
        return "[OkiokiSetCommand] First argument must be GET, POST, PUT or DELETE";
    }

    if (strcmp(argv[2], "CSV") == 0) {
        view->output_type = O_CSV;
    } else if (strcmp(argv[2], "COOKIE") == 0) {
        view->output_type = O_COOKIE;
    } else {
        return "[OkiokiSetCommand] Third argument must be CSV or COOKIE";
    }

    if ((view->sql = apr_pstrdup(pool, argv[3])) == NULL) {
        return "[OkiokiSetCommand] Failed to copy fourth argument.";
    }
    view->sql_len = strlen(view->sql);

    // Copy the parameter names from the rest of argv.
    view->nr_sql_params = 0;
    for (i = 0; i < MAX_PARAMETERS; i++) {
        if (i < argc - 4) {
            if ((param = apr_pstrdup(pool, argv[i + 4])) == NULL) {
                return "[OkiokiSetCommand] Failed to copy sql parameter.";
            }

            view->sql_params[i]     = param;
            view->sql_params_len[i] = strlen(param);
            view->nr_sql_params++;
        } else {
            view->sql_params[i]     = NULL;
            view->sql_params_len[i] = 0;
        }
    }

    return NULL;
}

/** A set of command to execute when a configuration parameter is parsed.
 */
static const command_rec mod_okioki_cmds[] = {
    AP_INIT_TAKE_ARGV(
        "OkiokiCommand",
        mod_okioki_dircfg_set_command,
        NULL,
        OR_AUTHCFG,
        "OkiokiCommand GET|POST|PUT|DELETE <path> CSV|COOKIE <prepared sql> [<params>[ <params>]...]"
    ),
    {NULL}
};

/** The module's datastructe, which is used by the server for information about this module.
 */
module AP_MODULE_DECLARE_DATA okioki_module =
{
    STANDARD20_MODULE_STUFF,
    mod_okioki_create_dir_config,
    NULL,
    NULL,
    NULL,
    mod_okioki_cmds,
    mod_okioki_register_hooks,
};

