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
#include <apr_hash.h>
#include <apr_strings.h>
#include <apr_dbd.h>
#include <httpd.h>
#include <http_log.h>
#include <http_request.h>
#include <http_protocol.h>
#include <mod_dbd.h>
#include "views.h"

#define MAX_ARGUMENTS 32

int mod_okioki_view_execute(request_rec *http_request, mod_okioki_dir_config *cfg, view_t *view, apr_hash_t *arguments, const apr_dbd_driver_t **db_driver, apr_dbd_results_t **db_result)
{
    apr_pool_t         *pool = http_request->pool;
    ap_dbd_t           *db_conn;
    apr_dbd_prepared_t *db_statement;
    int                have_result = (view->link_cmd == M_POST) | (view->link_cmd == M_GET);
    char               *arg;
    int                argc = view->nr_sql_params;
    char               *argv[argc + 1];
    off_t              i;
    int                nr_rows;

    // Copy the pointers parameters in the right order for the SQL statement.
    for (i = 0; i < argc; i++) {
        HTTP_ASSERT_NOT_NULL(
            arg = (char *)apr_hash_get(arguments, view->sql_params[i], view->sql_params_len[i]),
            HTTP_INTERNAL_SERVER_ERROR, "[mod_okioki] Could not find parameter '%s' in request.", view->sql_params[i]
        )

        argv[i] = arg;
    }
    argv[i] = NULL;

    // Retrieve a database connection from the resource pool.
    HTTP_ASSERT_NOT_NULL(
        db_conn = ap_dbd_acquire(http_request),
        HTTP_INTERNAL_SERVER_ERROR, "[mod_okioki] Can not get database connection."
    )
    *db_driver = db_conn->driver;

    // Get the prepared statement.
    HTTP_ASSERT_NOT_NULL(
        db_statement = apr_hash_get((db_conn)->prepared, view->sql, view->sql_len),
        HTTP_INTERNAL_SERVER_ERROR, "[mod_okioki] Can not find '%s'", view->sql
    )

    // Execute the statement.
    *db_result = NULL;
    if (have_result) {
        // Execute a select statement. We allow random access here as it allows easier configuration because the number
        // of columns and the name of the columns are known when random access is enabled.
        // Also because we use buckets and brigades everything is done in memory already, so streaming data would not
        // have worked anyway.
        HTTP_ASSERT_ZERO(
            apr_dbd_pselect(db_conn->driver, db_conn->pool, db_conn->handle, db_result, db_statement, 1, argc, (const char **)argv),
            HTTP_BAD_GATEWAY, "[mod_okioki] Can not execute select statement."
        )
        HTTP_ASSERT_NOT_NULL(
            *db_result,
            HTTP_BAD_GATEWAY, "[mod_okioki] Result was not set by apr_dbd_pselect."
        )
    } else {
        HTTP_ASSERT_ZERO(
            apr_dbd_pquery(db_conn->driver, db_conn->pool, db_conn->handle, &nr_rows, db_statement, argc, (const char **)argv),
            HTTP_BAD_GATEWAY, "[mod_okioki] Can not execute query."
        )
        if (nr_rows < 1) {
            ap_log_perror(APLOG_MARK, APLOG_ERR, 0, pool, "[mod_okioki] query modified zero rows.");
            return HTTP_NOT_FOUND;
        }
    }
    return HTTP_OK;
}


