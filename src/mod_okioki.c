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
#include "urlencoding.h"
#include "csv.h"
#include "json.h"
#include "util.h"

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
    if ((new_cfg = (mod_okioki_dir_config *)apr_palloc(pool, sizeof (mod_okioki_dir_config))) == NULL) {
        ap_log_perror(APLOG_MARK, APLOG_ERR, 0, pool, "Failed to allocate per-directory config.");
        return NULL;
    }

    if ((new_cfg->views = apr_hash_make(pool)) == NULL) {
        ap_log_perror(APLOG_MARK, APLOG_ERR, 0, pool, "Failed to allocate views hash table.");
        return NULL;
    }

    if ((new_cfg->result_strings = apr_hash_make(pool)) == NULL) {
        ap_log_perror(APLOG_MARK, APLOG_ERR, 0, pool, "Failed to allocate result_strings hash table.");
        return NULL;
    }

    return (void *)new_cfg;
}

/** Read data from the client.
 * Read from the client using the bucket and brigade interface, so that input filers
 * can modify the data.
 *
 * @param http_request   The HTTP request record.
 * @param _data          On return this contains a pointer to the data read.
 * @param _data_len      On return this contains the amount of data read.
 * @retrurns             HTTP_OK on success, or an other HTTP error value.
 */
static int mod_okioki_read_data(request_rec *http_request, char **_data, size_t *_data_len, char **error)
{
    apr_pool_t              *pool = http_request->pool;
    apr_pool_t              *bucket_pool = http_request->connection->pool;
    apr_bucket_alloc_t      *bucket_alloc = http_request->connection->bucket_alloc;
    apr_bucket_brigade      *bb;
    apr_bucket              *bucket;
    int                     seen_eos;
    char                    *data;
    size_t                  data_size = MIN_INPUT_BUFFER;
    size_t                  data_len = 0;
    const char              *tmp_data;
    size_t                  tmp_data_len;

    // Allocate input buffer.
    ASSERT_NOT_NULL(
        data = apr_palloc(pool, data_size),
        HTTP_INTERNAL_SERVER_ERROR, "Could not allocate input buffer of size %i", (int)data_size
    )

    // Create a brigade to work with.
    ASSERT_NOT_NULL(
        bb = apr_brigade_create(bucket_pool, bucket_alloc),
        HTTP_INTERNAL_SERVER_ERROR, "Could not create brigade for input."
    )

    // We will need to read multiple times into the brigade, until we find an eod bucket.
    do {
        ASSERT_APR_SUCCESS(
            ap_get_brigade(http_request->input_filters, bb, AP_MODE_READBYTES, APR_BLOCK_READ, HUGE_STRING_LEN),
            HTTP_INTERNAL_SERVER_ERROR, "Could not get input brigade from request."
        )

        for (bucket = APR_BRIGADE_FIRST(bb); bucket != APR_BRIGADE_SENTINEL(bb); bucket = APR_BUCKET_NEXT(bucket)) {
            // We stop reading completely when we find an EOS bucket.
            if (APR_BUCKET_IS_EOS(bucket)) {
                seen_eos = 1;
                break;
            }

            // We don't do anything with flush.
            if (APR_BUCKET_IS_FLUSH(bucket)) {
                continue;
            }

            // If our data buffer is full, we stop reading, but we still need to process buckets.
            if (data_len == data_size) {
                continue;
            }

            // Copy the data from the bucket into our data buffer.
            apr_bucket_read(bucket, &tmp_data, &tmp_data_len, APR_BLOCK_READ);

            // Check if there is enough room to copy, if not make room.
            if (tmp_data_len > (data_size - data_len)) {
                size_t new_size = mod_okioki_nlpo2(data_len + tmp_data_len);

                // Check if the new size is within what is allowed.
                ASSERT_POSITIVE(
                    MAX_INPUT_BUFFER - new_size,
                    HTTP_BAD_REQUEST, "To much input data %i", (int)new_size
                )

                // Try and reallocate the buffer and copy current data into it.
                ASSERT_NOT_NULL(
                    data = mod_okioki_realloc(pool, data, data_len, new_size),
                    HTTP_INTERNAL_SERVER_ERROR, "Could not resize input buffer to %i", (int)new_size
                )
            }

            // Copy the data into the buffer.
            memcpy(&data[data_len], tmp_data, tmp_data_len);
            data_len+= tmp_data_len;
        }

        // Remove all buckets from brigades so we can reuse it on the next iteration.
        apr_brigade_cleanup(bb);
    } while (!seen_eos);

    // Pass the data buffer, and the length of the data to the caller.
    *_data = data;
    *_data_len = data_len;
    return HTTP_OK;
}

static int mod_okioki_input_handler(request_rec *http_request, apr_hash_t **_arguments, char **error)
{
    apr_pool_t              *pool = http_request->pool;
    apr_hash_t              *arguments;
    char                    *data;
    size_t                  data_size;
    int                     ret;
    const char              *_content_type;
    char                    *content_type;
    char                    *charset;
    char                    *last_token;

    // Create arguments hash table. Then pass these arguments back to the caller.
    ASSERT_NOT_NULL(
        arguments = apr_hash_make(pool),
        HTTP_INTERNAL_SERVER_ERROR, "Failed to allocate argument table."
    )
    *_arguments = arguments;

    // Extract parameters from the query string.
    ASSERT_HTTP_OK(
        ret = mod_okioki_parse_query(http_request, arguments, http_request->args, error),
        ret, "Could not parse url-query."
    )

    // Extract parameters from the POST/PUT data.
    if (((http_request->method_number == M_POST) | (http_request->method_number == M_PUT))) {
        // Read all data from the buckets and brigades.
        ASSERT_HTTP_OK(
            ret = mod_okioki_read_data(http_request, &data, &data_size, error),
            ret, "Could not read input from buckets."
        )

        // Check for the content-type of the request.
        ASSERT_NOT_NULL(
            _content_type = apr_table_get(http_request->headers_in, "Content-type"),
            HTTP_BAD_REQUEST, "[mod_okioki.c] No content-type for PUT or POST."
        )

        ASSERT_NOT_NULL(
            content_type = apr_pstrdup(pool, _content_type),
            HTTP_INTERNAL_SERVER_ERROR, "Could not allocate content-type"
        )

        // Get the content_type from the content_type string.
        content_type = apr_strtok(content_type, ";", &last_token);
        // Get the charset from the content_type string.
        charset = apr_strtok(NULL, ";", &last_token);

        if (strcmp(content_type, "application/x-www-form-urlencoded") == 0) {
            ASSERT_HTTP_OK(
                ret = mod_okioki_parse_query(http_request, arguments, data, error),
                ret, "Could not parse posted-query."
            )
        } else {
            ap_log_perror(APLOG_MARK, APLOG_ERR, 0, pool, "Input content-type '%s' not supported.", content_type);
            return HTTP_BAD_REQUEST;
        }
    }

    return HTTP_OK;
}

/** Generate error output.
 */
int mod_okioki_generate_error(request_rec *http_request, apr_pool_t *pool, apr_bucket_alloc_t *alloc, int ret, char **error)
{
    apr_bucket_brigade *bb;
    apr_bucket *b;

    ASSERT_NOT_NULL(
        bb = apr_brigade_create(pool, alloc),
        HTTP_INTERNAL_SERVER_ERROR, "Could not allocate a bucket brigade."
    )

    // Add a line for the error.
    if (error != NULL) {
        ASSERT_NOT_NULL(
            b = apr_bucket_transient_create(*error, strlen(*error), alloc),
            HTTP_INTERNAL_SERVER_ERROR, "[mod_okioki] Could not allocate bucket."
        )
        APR_BRIGADE_INSERT_TAIL(bb, b);

        ASSERT_NOT_NULL(
            b = apr_bucket_immortal_create("\n", 1, alloc),
            HTTP_INTERNAL_SERVER_ERROR, "[mod_okioki] Could not allocate bucket."
        )
        APR_BRIGADE_INSERT_TAIL(bb, b);

    } else {
        ASSERT_NOT_NULL(
            b = apr_bucket_immortal_create("Unknown error.\n", 15, alloc),
            HTTP_INTERNAL_SERVER_ERROR, "[mod_okioki] Could not allocate bucket."
        )
        APR_BRIGADE_INSERT_TAIL(bb, b);
    }

    // Add an end-of-stream.
    ASSERT_NOT_NULL(
        b = apr_bucket_eos_create(alloc),
        HTTP_INTERNAL_SERVER_ERROR, "Could not allocate bucket."
    )
    APR_BRIGADE_INSERT_TAIL(bb, b);

    // Return the data.
    ap_set_content_type(http_request, "text/plain");
    http_request->status = ret;
    return ap_pass_brigade(http_request->output_filters, bb);
}

/** Generate no output.
 */
int mod_okioki_generate_empty(request_rec *http_request, apr_pool_t *pool, apr_bucket_alloc_t *alloc, char **error)
{
    apr_bucket_brigade *bb;
    apr_bucket *b;

    ASSERT_NOT_NULL(
        bb = apr_brigade_create(pool, alloc),
        HTTP_INTERNAL_SERVER_ERROR, "Could not allocate a bucket brigade."
    )

    // Add an end-of-stream.
    ASSERT_NOT_NULL(
        b = apr_bucket_eos_create(alloc),
        HTTP_INTERNAL_SERVER_ERROR, "Could not allocate bucket."
    )
    APR_BRIGADE_INSERT_TAIL(bb, b);

    // Return the data.
    ap_set_content_type(http_request, "text/plain");
    http_request->status = HTTP_OK;
    return ap_pass_brigade(http_request->output_filters, bb);
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
    mod_okioki_dir_config   *cfg = (mod_okioki_dir_config *)ap_get_module_config(http_request->per_dir_config, &okioki_module);
    char                    *view_name;
    view_t                  *view;
    apr_hash_t              *arguments;
    int                     ret;
    const apr_dbd_driver_t  *db_driver;
    apr_dbd_results_t       *db_result;
    char                    *_error;
    char                    **error = &_error;

    // Check if we need to process the request. We only need to if the handler is set to "okioki-handler".
    if (http_request->handler == NULL || (strcmp(http_request->handler, "okioki-handler") != 0)) {
        return DECLINED;
    }

    // Find a view matching the url.
    view_name = apr_pstrcat(pool, http_request->method, " ", http_request->path_info, NULL);
    ASSERT_NOT_NULL(
        view = apr_hash_get(cfg->views, view_name, APR_HASH_KEY_STRING),
        HTTP_NOT_FOUND, "Could not find view for '%s'.", view_name
    )

    // Handle all input data and build up the arguments table. These arguments are used in the execution
    // of the prepared sql statement.
    if ((ret = mod_okioki_input_handler(http_request, &arguments, error)) != HTTP_OK) {
        return mod_okioki_generate_error(http_request, bucket_pool, bucket_alloc, ret, error);
    }

    // Handle the view.
    if ((ret = mod_okioki_view_execute(http_request, cfg, view, arguments, &db_driver, &db_result, error)) != HTTP_OK) {
        return mod_okioki_generate_error(http_request, bucket_pool, bucket_alloc, ret, error);
    }

    if (db_result != NULL) {
        switch (view->output_type) {
        case O_CSV:
            return mod_okioki_generate_csv(http_request, bucket_pool, bucket_alloc, db_driver, db_result, error);
        case O_JSON:
            return mod_okioki_generate_json(http_request, bucket_pool, bucket_alloc, db_driver, db_result, view->result_strings, error);
        }
    } else {
        return mod_okioki_generate_empty(http_request, bucket_pool, bucket_alloc, error);
    }

    /* NOTREACHED */
    return HTTP_INTERNAL_SERVER_ERROR;
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
    char                  *view_name;
    view_t                *view;
    unsigned int          i;
    char                  *param;

    // Make sure this configuration directive has at least two arguments.
    if (argc < 4) {
        return "[OkiokiSetCommand] Requires at least four arguments.";
    }

    // Create a new view.
    if ((view = (view_t *)apr_palloc(pool, sizeof (view_t))) == NULL) {
        return "[OkiokiSetCommand] Could not allocate view.";
    }

    // Add the view to the hash table. The name of the view is the method
    // and the url joined by a white space.
    if ((view_name = apr_pstrcat(pool, argv[0], " ", argv[1], NULL)) == NULL) {
        return "[OkiokiSetCommand] Failed to allocate name of view.";
    }
    apr_hash_set(conf->views, view_name, APR_HASH_KEY_STRING, view);

    if (strcmp(argv[2], "CSV") == 0) {
        view->output_type = O_CSV;
    } else if (strcmp(argv[2], "JSON") == 0) {
        view->output_type = O_JSON;
    } else {
        return "[OkiokiSetCommand] Third argument must be CSV or JSON";
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

    // Copy the result strings, multiple views can use the same result strings.
    view->result_strings = conf->result_strings;

    return NULL;
}

const char *mod_okioki_dircfg_result_strings(cmd_parms *cmd, void *_conf, int argc, char *const argv[])
{
    apr_pool_t            *pool      = cmd->pool;
    mod_okioki_dir_config *conf      = (mod_okioki_dir_config *)_conf;
    char                  *param;
    int                   i;

    if ((conf->result_strings = apr_hash_make(pool)) == NULL) {
        return "Failed to allocate result_strings hash table.";
    }

    for (i = 0; i < argc; i++) {
        if ((param = apr_pstrdup(pool, argv[i])) == NULL) {
            return "Failed to allocate parameter.";
        }

        // Add the parameter to the hash table, we are using the hash table as a set, so we use the
        // hash table itself as value.
        apr_hash_set(conf->result_strings, param, APR_HASH_KEY_STRING, conf->result_strings);
    }

    return NULL;
}

/** A set of command to execute when a configuration parameter is parsed.
 */
static const command_rec mod_okioki_cmds[] = {
    AP_INIT_TAKE_ARGV(
        "OkiokiResultStrings",
        mod_okioki_dircfg_result_strings,
        NULL,
        OR_AUTHCFG,
        "OkiokiResultStrings [<params>[ <params>]...]"
    ),
    AP_INIT_TAKE_ARGV(
        "OkiokiCommand",
        mod_okioki_dircfg_set_command,
        NULL,
        OR_AUTHCFG,
        "OkiokiCommand GET|POST|PUT|DELETE <path> CSV|JSON <prepared sql> [<params>[ <params>]...]"
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

