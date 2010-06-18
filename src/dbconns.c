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

#include <apr_atomic.h>
#include <apr_strings.h>
#include <apr_dbd.h>
#include <httpd.h>
#include <http_log.h>
#include <http_request.h>
#include <mod_dbd.h>
#include "views.h"
#include "mod_okioki.h"
#include "dbconns.h"

int mod_okioki_execute_view(request_rec *http_request, mod_okioki_dir_config *cfg, view_t *view, apr_hash_t *arguments, apr_array_header_t *result)
{
    apr_pool_t         *pool = http_request->pool;
    ap_dbd_t           *db_conn;
    apr_dbd_results_t  *db_result;
    apr_dbd_prepared_t *db_statement;
    int                have_result = (view->link_cmd == M_POST) | (view->link_cmd == M_GET);
    char               *arg;
    int                argc = view->nr_sql_params;
    char               *argv[argc + 1];
    off_t              i;
    const char         *db_value;
    char               *value;
    apr_hash_t         *row;
    apr_hash_t         **row_item;
    apr_dbd_row_t      *db_row;
    int                row_nr;
    int                nr_rows;
    int                col_nr;
    int                nr_cols;

    // Copy the pointers parameters in the right order for the SQL statement.
    for (i = 0; i < argc; i++) {
        HTTP_ASSERT_NOT_NULL(
            arg = (char *)apr_hash_get(arguments, view->sql_params[i], view->sql_params_len[i]),
            HTTP_BAD_GATEWAY, "[mod_okioki] Could not find parameter '%s' in request.", view->sql_params[i]
        )

        argv[i] = arg;
    }
    argv[i] = NULL;

    ap_log_perror(APLOG_MARK, APLOG_WARNING, 0, pool, "[mod_okioki] test 1.");

    // Retrieve a database connection from the resource pool.
    HTTP_ASSERT_NOT_NULL(
        db_conn = ap_dbd_acquire(http_request),
        HTTP_BAD_GATEWAY, "[mod_okioki] Can not get database connection."
    )

    ap_log_perror(APLOG_MARK, APLOG_WARNING, 0, pool, "[mod_okioki] test 2.");

    // Get the prepared statement.
    HTTP_ASSERT_NOT_NULL(
        db_statement = apr_hash_get(db_conn->prepared, view->sql, view->sql_len),
        HTTP_NOT_FOUND, "[mod_okioki] Can not find '%s'", view->sql
    )

    ap_log_perror(APLOG_MARK, APLOG_WARNING, 0, pool, "[mod_okioki] test 3 argc:%i first='%s'.", argc, argv[0]);

    // Execute the statement.
    if (have_result) {
        db_result = NULL;
        HTTP_ASSERT_ZERO(
            apr_dbd_pselect(db_conn->driver, db_conn->pool, db_conn->handle, &db_result, db_statement, 1, argc, (const char **)argv),
            HTTP_INTERNAL_SERVER_ERROR, "[mod_okioki] Can not execute select statement."
        )
        HTTP_ASSERT_NOT_NULL(
            db_result,
            HTTP_INTERNAL_SERVER_ERROR, "[mod_okioki] Result was not set by apr_dbd_pselect."
        )

        nr_rows = apr_dbd_num_tuples(db_conn->driver, db_result);
        //nr_rows = 0;
    } else {
        HTTP_ASSERT_ZERO(
            apr_dbd_pquery(db_conn->driver, db_conn->pool, db_conn->handle, &nr_rows, db_statement, argc, (const char **)argv),
            HTTP_INTERNAL_SERVER_ERROR, "[mod_okioki] Can not execute query."
        )
    }

    ap_log_perror(APLOG_MARK, APLOG_WARNING, 0, pool, "[mod_okioki] test 4.");

    if (nr_rows < 1) {
        return HTTP_NOT_FOUND;
    }

    ap_log_perror(APLOG_MARK, APLOG_WARNING, 0, pool, "[mod_okioki] test 5.");

    if (have_result) {
        nr_cols = apr_dbd_num_cols(db_conn->driver, db_result);
        for (row_nr = 0; row_nr < nr_rows; row_nr++) {
            ap_log_perror(APLOG_MARK, APLOG_WARNING, 0, pool, "[mod_okioki] test 6.");

            // Create a new hash table row.
            HTTP_ASSERT_NOT_NULL(
                row = apr_hash_make(pool),
                HTTP_INTERNAL_SERVER_ERROR, "[mod_okioki] Can not allocate hash table for row."
            )

            ap_log_perror(APLOG_MARK, APLOG_WARNING, 0, pool, "[mod_okioki] test 7.");

            // Add the empty row to the result, we fill in the row afterwards.
            HTTP_ASSERT_NOT_NULL(
                row_item = (apr_hash_t **)apr_array_push(result),
                HTTP_INTERNAL_SERVER_ERROR, "[mod_okioki] Failed to add row to result."
            )
            *row_item = row;

            ap_log_perror(APLOG_MARK, APLOG_WARNING, 0, pool, "[mod_okioki] test 8.");

            // Retrieve a row from the result.
            db_row = NULL;
            HTTP_ASSERT_ZERO(
                apr_dbd_get_row(db_conn->driver, pool, db_result, &db_row, row_nr + 1),
                HTTP_BAD_GATEWAY, "[mod_okioki] Failed to retrieve row from select."
            )

            ap_log_perror(APLOG_MARK, APLOG_WARNING, 0, pool, "[mod_okioki] test 9.");

            // Add the columns to the row.
            for (col_nr = 0; col_nr < nr_cols; col_nr++) {
                // Get the value from the database.
                HTTP_ASSERT_NOT_NULL(
                    db_value = apr_dbd_get_entry(db_conn->driver, db_row, col_nr),
                    HTTP_BAD_GATEWAY, "[mod_okioki] Could not retrieve value from select"
                )

                // Copy the value.
                HTTP_ASSERT_NOT_NULL(
                    value = apr_pstrdup(pool, db_value),
                    HTTP_INTERNAL_SERVER_ERROR, "[mod_okioki] Could not copy value from result"
                )

                // Add value to resul.t
                apr_hash_set(row, apr_dbd_get_name(db_conn->driver, db_result, col_nr), APR_HASH_KEY_STRING, value);
            }
            ap_log_perror(APLOG_MARK, APLOG_WARNING, 0, pool, "[mod_okioki] test 10.");

        }
    }

    ap_log_perror(APLOG_MARK, APLOG_WARNING, 0, pool, "[mod_okioki] test 11.");

    return HTTP_OK;
}

