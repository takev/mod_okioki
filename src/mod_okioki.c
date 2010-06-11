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
#include "dbconns.h"
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
    HTTP_ASSERT_NOT_NULL(
        new_cfg = (mod_okioki_dir_config *)apr_pcalloc(pool, sizeof (mod_okioki_dir_config)),
        NULL, "[mod_okioki] Failed to allocate per-directory config."
    )

    // Setup the backend database connection pool.
    HTTP_ASSERT_OK(
        mod_okioki_dbconns_init(pool, new_cfg),
        NULL, "[mod_okioki] Failed to setup database resource pool."
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
    apr_pool_t            *pool = http_request->pool;
    mod_okioki_dir_config *cfg;
    view_t                *view;
    apr_hash_t            *arguments;
    apr_array_header_t    *result;
    char                  *result_s;
    size_t                result_s_len;
    int                   ret;

    // Check if we need to process the request. We only need to if the handler is set to "okioki-handler".
    if (http_request->handler == NULL || (strcmp(http_request->handler, "okioki-handler") != 0)) {
        return DECLINED;
    }

    // Retrieve the per-directory configuration for this request.
    HTTP_ASSERT_NOT_NULL(
        cfg = (mod_okioki_dir_config *)ap_get_module_config(http_request->per_dir_config, &okioki_module),
        HTTP_INTERNAL_SERVER_ERROR, "[mod_okioki] Could not get the per-directory config."
    )

    // Create hash table.
    HTTP_ASSERT_NOT_NULL(
        arguments = apr_hash_make(pool),
        HTTP_INTERNAL_SERVER_ERROR, "[mod_okioki] Failed to allocate argument table."
    )

    // Create the result.
    HTTP_ASSERT_NOT_NULL(
        result = apr_array_make(pool, 0, sizeof (apr_hash_t *)),
        HTTP_INTERNAL_SERVER_ERROR, "[mod_okioki] Failed to allocate result table."
    )

    // Find a view matching the url.
    HTTP_ASSERT_NOT_NULL(
        view = mod_okioki_view_lookup(cfg, http_request, arguments),
        HTTP_NOT_FOUND, "[mod_okioki] Could not find view for '%s'.", http_request->path_info
    )

    // Extract cookies from request.
    HTTP_ASSERT_OK(
        ret = mod_okioki_get_cookies(http_request, arguments),
        ret, "[mod_okioki] Could not parse http-cookies."
    )

    // Extract parameters from the query string.
    HTTP_ASSERT_OK(
        ret = mod_okioki_parse_query(http_request, arguments, http_request->args),
        ret, "[mod_okioki] Could not parse url-query."
    )

    // Extract parameters from the POST/PUT data.
    HTTP_ASSERT_OK(
        ret = mod_okioki_parse_posted_query(http_request, arguments),
        ret, "[mod_okioki] Could not parse posted-query."
    )

    // Handle the view.
    HTTP_ASSERT_OK(
        ret = mod_okioki_execute_view(http_request, cfg, view, arguments, result),
        ret, "[mod_okioki] Could not execute view"
    )

    // Convert result to csv string.
    HTTP_ASSERT_NOT_NULL(
        result_s = mod_okioki_generate_csv(pool, view, result, &result_s_len),
        HTTP_INTERNAL_SERVER_ERROR, "[mod_okioki] Could not generate text/csv."
    )

    if (result_s_len > 0) {
        ap_set_content_type(http_request, "text/csv");
        ap_set_content_length(http_request, result_s_len);

        HTTP_ASSERT_NOT_NEG(
            ap_rwrite(result_s, result_s_len, http_request),
            HTTP_INTERNAL_SERVER_ERROR, "[mod_okioki] Write failed, client closed connection."
        )

        HTTP_ASSERT_NOT_NEG(
            ap_rflush(http_request),
            HTTP_INTERNAL_SERVER_ERROR, "[mod_okioki] Write failed, client closed connection."
        )
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

/** Process the OkiokiSQL configuration directive.
 */
const char *mod_okioki_dircfg_set_sql(cmd_parms *cmd, void *_conf, const char *args)
{
    mod_okioki_dir_config *conf   = (mod_okioki_dir_config *)_conf;
    off_t                 view_nr = conf->nr_views - 1;
    view_t                *view   = &conf->views[view_nr];
    size_t                args_length = strlen(args);
    off_t                 sql_start = 0;
    off_t                 sql_end = 0;
    off_t                 params_start = 0;
    off_t                 i = 0;
    char                  **argv;

    // Skip over leading spaces and quotes.
    for (i = 0; i < args_length; i++) {
        char c = args[i];
        if ((c != '"') & (c != ' ')) {
            sql_start = i;
            break;
        }
    }
    if (sql_start == 0) {
        return "[TPApiSQL] expecting a leading quote character before SQL statement";
    }

    // Find the last quote.
    for (i = args_length - 1; i >= sql_start; i--) {
        if (args[i] == '"') {
            sql_end = i;
            params_start = i + 1;
            break;
        }
    }
    if (sql_end == 0) {
        return "[TPApiSQL] expecting a trailing quote after SQL statement.";
    }

    // Copy the SQL statement.
    if ((view->sql = apr_pstrndup(cmd->pool, &args[sql_start], sql_end - sql_start)) == NULL) {
        return "[TPApiSQL] could not copy SQL statement.";
    }

    i = 0;
    view->nr_sql_params = 0;
    if (params_start < args_length) {
        // Convert arguments in seperate tokens.
        if (apr_tokenize_to_argv(&args[params_start], &argv, cmd->pool) != 0) {
            return "[TPApiSQL] could not tokenize the arguments to the sql state.";
        }
        // Copy parameters to internal array.
        for (; argv[i] != NULL; i++) {
            view->sql_params[i]     = argv[i];
            view->sql_params_len[i] = strlen(argv[i]);
            view->nr_sql_params++;
        }
    }
    // Clear the rest of the parameters.
    for (; i < MAX_PARAMETERS; i++) {
        view->sql_params[i]     = NULL;
        view->sql_params_len[i] = 0;
    }

    return NULL;
}

/** Process the OkiokiSetCommand configuration directive.
 */
const char *mod_okioki_dircfg_set_command(cmd_parms *cmd, void *_conf, int argc, char *const argv[])
{
    mod_okioki_dir_config *conf   = (mod_okioki_dir_config *)_conf;
    off_t                 view_nr = conf->nr_views++;
    view_t                *view   = &conf->views[view_nr];
    unsigned int          i;

    // Make sure this configuration directive has at least two arguments.
    if (argc < 2) {
        return "[OkiokiSetCommand] Requires at least two arguments.";
    }

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

    // Compile the regular expression to match the url.
    if (regcomp(&view->link, argv[1], REG_EXTENDED) != 0) {
        return "[OkiokiSetCommand] Second argument needs to be a valid regular expression";
    }

    // Copy the parameter names from the rest of argv.
    view->nr_link_params = 0;
    for (i = 0; i < MAX_PARAMETERS; i++) {
        if (i < argc - 2) {
            char *param = apr_pstrdup(cmd->pool, argv[i + 2]);

            view->link_params[i]     = param;
            view->link_params_len[i] = strlen(param);
            view->nr_link_params++;
        } else {
            view->link_params[i]     = NULL;
            view->link_params_len[i] = 0;
        }
    }

    return NULL;
}

/** Process the OkiokiCSV configuration directive.
 */
const char *mod_okioki_dircfg_set_csv(cmd_parms *cmd, void *_conf, int argc, char *const argv[])
{
    mod_okioki_dir_config *conf   = (mod_okioki_dir_config *)_conf;
    off_t                 view_nr = conf->nr_views - 1;
    view_t                *view   = &conf->views[view_nr];
    off_t                 i;

    view->nr_csv_params = 0;
    for (i = 0; i < MAX_PARAMETERS; i++) {
        if (i < argc) {
            char *param = apr_pstrdup(cmd->pool, argv[i]);

            view->csv_params[i]     = param;
            view->csv_params_len[i] = strlen(param);
            view->nr_csv_params++;
        } else {
            view->csv_params[i]     = NULL;
            view->csv_params_len[i] = 0;
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
        "OkiokiCommand GET|POST|PUT|DELETE <regex> [<params>[ <params>]...]"
    ),
    AP_INIT_RAW_ARGS(
        "OkiokiSQL",
        mod_okioki_dircfg_set_sql,
        NULL,
        OR_AUTHCFG,
        "OkiokiSQL \"<sql statement>\" [<params>[ <params>]...]"
    ),
    AP_INIT_TAKE_ARGV(
        "OkiokiCSV",
        mod_okioki_dircfg_set_csv,
        NULL,
        OR_AUTHCFG,
        "OkiokiCSV [<result>[ <result>]...]"
    ),
    AP_INIT_TAKE1(
        "OkiokiConnectionInfo",
        ap_set_string_slot,
        (void *)APR_OFFSETOF(mod_okioki_dir_config, connection_info),
        OR_AUTHCFG,
        "OkiokiConnectionInfo (string) The database to connect to."
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

