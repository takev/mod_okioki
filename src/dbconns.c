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

#include <httpd.h>
#include <http_log.h>
#include <http_request.h>
#include <apr_atomic.h>
#include <apr_strings.h>
#include "views.h"
#include "mod_okioki.h"
#include "dbconns.h"

int mod_okioki_dbconns_init(apr_pool_t *pool, mod_okioki_dir_config *cfg)
{
    // Create an array for a resource pool.
    HTTP_ASSERT_NOT_NULL(
        cfg->connections = apr_array_make(pool, 0, sizeof (PGconn *)),
        HTTP_INTERNAL_SERVER_ERROR, "[mod_okioki] Failed to allocate connection array."
    )

    // Create a mutex to protect the array.
    if (apr_thread_mutex_create(&cfg->connections_mutex, APR_THREAD_MUTEX_DEFAULT, pool) != APR_SUCCESS) {
        ap_log_perror(APLOG_MARK, APLOG_ERR, 0, pool, "[mod_okioki] Failed to allocate connection array mutex.");
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    // Number of database connections, being managed.
    cfg->nr_connections = 0;
    cfg->connection_info = NULL;
    return HTTP_OK;
}

PGconn *mod_okioki_pop_dbconn(request_rec *req, mod_okioki_dir_config *cfg)
{
    PGconn **entry;

    // Lock the resource pool
    if (apr_thread_mutex_lock(cfg->connections_mutex) != APR_SUCCESS) {
        ap_log_perror(APLOG_MARK, APLOG_ERR, 0, req->pool, "[mod_okioki] Could not lock connections.");
        return NULL;
    }

    // Pop a database connection from the resource pool.
    entry = (PGconn **)apr_array_pop(cfg->connections);

    // Unlock the resource pool.
    if (apr_thread_mutex_unlock(cfg->connections_mutex) != APR_SUCCESS) {
        ap_log_perror(APLOG_MARK, APLOG_ERR, 0, req->pool, "[mod_okioki] Could not unlock connections.");
        return NULL;
    }

    // Check if the entry is NULL, in wich case the resource pool was empty.
    if (entry == NULL) {
        return NULL;
    }

    // An entry contains a connection, itself is not a connection.
    return *entry;
}

int mod_okioki_push_dbconn(request_rec *req, mod_okioki_dir_config *cfg, PGconn *db_conn)
{
    PGconn **entry;
    apr_pool_t *pool = req->pool;

    // Lock the resoure pool
    if (apr_thread_mutex_lock(cfg->connections_mutex) != APR_SUCCESS) {
        ap_log_perror(APLOG_MARK, APLOG_ERR, 0, req->pool, "[mod_okioki] Could not lock connections.");
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    // Push an connection on the resource pool. Entry points to a place to store the connection pointer.
    HTTP_ASSERT_NOT_NULL(
        entry = (PGconn **)apr_array_push(cfg->connections),
        HTTP_INTERNAL_SERVER_ERROR, "[mod_okioki] Get an entry during push."
    )

    // Store the connection pointer in the entry.
    *entry = db_conn;

    // Unock the resource pool
    if (apr_thread_mutex_unlock(cfg->connections_mutex) != APR_SUCCESS) {
        ap_log_perror(APLOG_MARK, APLOG_ERR, 0, req->pool, "[mod_okioki] Could not unlock connections.");
        return HTTP_OK;
    }

    // Sucessfully stored the connection in the resource pool.
    return HTTP_OK;
}

PGconn *mod_okioki_get_dbconn(request_rec *req, mod_okioki_dir_config *cfg)
{
    PGconn      *db_conn;
    uint32_t    nr_connections;
    apr_pool_t  *pool = req->pool;
   
    // Try to get an old connection from the pool. 
    db_conn = mod_okioki_pop_dbconn(req, cfg);

    // Check if the connection is still functioning.
    if (db_conn != NULL && PQstatus(db_conn) == CONNECTION_BAD) {
        ap_log_perror(APLOG_MARK, APLOG_WARNING, 0, req->pool, "[mod_okioki] Connection to '%s' was bad, closing and getting a fresh one.", cfg->connection_info);
        mod_okioki_return_dbconn(req, cfg, db_conn, 1);
    }

    if (db_conn == NULL) {
        // Check if the database is configured correctly.
        if (cfg->connection_info == NULL) {
            ap_log_perror(APLOG_MARK, APLOG_ERR, 0, req->pool, "[mod_okioki] OkiokiConnectionInfo needs to be set.");
            return NULL;
        }

        // Connect to the database.
        HTTP_ASSERT_NOT_NULL(
            db_conn = PQconnectdb(cfg->connection_info),
            NULL, "[mod_okioki] Could connect to database using '%s' as connection info.", cfg->connection_info
        )

        // Record that a new connection was made.
        nr_connections = apr_atomic_inc32(&cfg->nr_connections) + 1;
        ap_log_perror(APLOG_MARK, APLOG_NOTICE, 0, req->pool, "[mod_okioki] Connected %lu times to '%s' as connection info.", (long)nr_connections, cfg->connection_info);
    }


    // Return the connection to the requestor.
    return db_conn;
}

int mod_okioki_return_dbconn(request_rec *req, mod_okioki_dir_config *cfg, PGconn *db_conn, int close_connection)
{
    int ret = HTTP_OK;

    if (!close_connection) {
        if (PQtransactionStatus(db_conn) != PQTRANS_IDLE) {
            ap_log_perror(APLOG_MARK, APLOG_WARNING, 0, req->pool, "[mod_okioki] Database connection to '%s' is not idle when being returned.", cfg->connection_info);
            close_connection = 1;

        } else {
            // If we don't close the connection we want to put it back on the pool.
            ret = mod_okioki_push_dbconn(req, cfg, db_conn);
        }
    }

    if (close_connection || ret != HTTP_OK) {
        // If there was an error during the storing of the connection on the pool, or we need to close the connection.
        PQfinish(db_conn);

        // Record that we removed the connection.
        apr_atomic_dec32(&cfg->nr_connections);
        ap_log_perror(APLOG_MARK, APLOG_NOTICE, 0, req->pool, "[mod_okioki] Closing connection to '%s'.", cfg->connection_info);
    }
    return ret;
}

int mod_okioki_execute_view(request_rec *http_request, mod_okioki_dir_config *cfg, view_t *view, apr_hash_t *arguments, apr_array_header_t *result)
{
    apr_pool_t *pool = http_request->pool;
    PGconn     *db_conn;
    PGresult   *db_result;
    int        ret = HTTP_OK;
    char       *values[view->nr_sql_params];
    off_t      i;
    apr_hash_t *row;
    apr_hash_t **row_item;
    int        row_nr;
    int        nr_rows;
    int        col_nr;
    int        nr_cols;
    char       *value;

    // Copy the pointers parameters in the right order for the SQL statement.
    for (i = 0; i < view->nr_sql_params; i++) {
        HTTP_ASSERT_NOT_NULL(
            value = (char *)apr_hash_get(arguments, view->sql_params[i], view->sql_params_len[i]),
            HTTP_BAD_GATEWAY, "[mod_okioki] Could not find parameter '%s' in request.", view->sql_params[i]
        )

        values[i] = value;
    }

    // Retrieve a database connection from the resource pool.
    HTTP_ASSERT_NOT_NULL(
        db_conn = mod_okioki_get_dbconn(http_request, cfg),
        HTTP_BAD_GATEWAY, "[mod_okioki] Can not get database connection."
    )

    // Execute the SQL statement.
    if ((db_result = PQexecParams(db_conn, view->sql, view->nr_sql_params, NULL, (const char * const *)values, NULL, NULL, 0)) == NULL) {
        ap_log_perror(APLOG_MARK, APLOG_ERR, 0, pool, "[mod_okioki] Can not execute statement.");
        ret = HTTP_BAD_GATEWAY;
        goto failed;
    }

    switch (PQresultStatus(db_result)) {
    case PGRES_EMPTY_QUERY:
    case PGRES_COPY_OUT:
    case PGRES_COPY_IN:
    case PGRES_BAD_RESPONSE:
    case PGRES_NONFATAL_ERROR:
    case PGRES_FATAL_ERROR:
        ap_log_perror(APLOG_MARK, APLOG_ERR, 0, pool, "[mod_okioki] Unexpected result from database: %s.", PQresultErrorMessage(db_result));
        ret = HTTP_BAD_GATEWAY;
        goto failed;
    default:
        // all is well.
        break;
    }

    nr_rows = PQntuples(db_result);
    nr_cols = PQnfields(db_result);
    for (row_nr = 0; row_nr < nr_rows; row_nr++) {
        // Create a new hash table row.
        if ((row = apr_hash_make(pool)) == NULL) {
            ap_log_perror(APLOG_MARK, APLOG_ERR, 0, pool, "[mod_okioki] Can not allocate hash table for row.");
            ret = HTTP_BAD_GATEWAY;
        }

        // Add the empty row to the result, we fill in the row afterwards.
        if ((row_item = (apr_hash_t **)apr_array_push(result)) == NULL) {
            ap_log_perror(APLOG_MARK, APLOG_ERR, 0, pool, "[mod_okioki] Failed to add row to result.");
            ret = HTTP_BAD_GATEWAY;
        }
        *row_item = row;

        // Add the columns to the row.
        for (col_nr = 0; col_nr < nr_cols; col_nr++) {
            value = apr_pstrdup(pool, PQgetvalue(db_result, row_nr, col_nr));
            apr_hash_set(row, PQfname(db_result, col_nr), APR_HASH_KEY_STRING, value);
        }
    }

failed:
    // Clean up the result from the database.
    PQclear(db_result);

    // Return the database connection to the resource pool. Unless there was a database error, denoted
    // by the HTTP_BAD_GATEWAY error, in which case we close the connection.
    if (mod_okioki_return_dbconn(http_request, cfg, db_conn, ret == HTTP_BAD_GATEWAY) < 0) {
        ap_log_perror(APLOG_MARK, APLOG_ERR, 0, pool, "[mod_okioki] Can not return database connection.");
        // The request was completed successfully already, so we pretend to the user that nothing is the matter.
    }
    return ret;
}

