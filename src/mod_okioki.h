#ifndef MOD_TDBAPI_H
#define MOD_TDBAPI_H
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

#include <apr.h>
#include <regex.h>

#define MAX_PARAMETERS 32
#define MAX_VIEWS 200

#define HTTP_ASSERT_NOT_NULL(expr, http_code, msg...) \
    if (__builtin_expect((expr) == NULL, 0)) { \
        ap_log_perror(APLOG_MARK, APLOG_ERR, 0, pool, msg); \
        return http_code; \
    }

#define HTTP_ASSERT_OK(expr, http_code, msg...) \
    if (__builtin_expect((expr) != HTTP_OK, 0)) { \
        ap_log_perror(APLOG_MARK, APLOG_ERR, 0, pool, msg); \
        return http_code; \
    }

#define HTTP_ASSERT_NOT_NEG(expr, http_code, msg...) \
    if (__builtin_expect((expr) < 0, 0)) { \
        ap_log_perror(APLOG_MARK, APLOG_ERR, 0, pool, msg); \
        return http_code; \
    }


typedef struct {
    regex_t link;
    int     link_cmd;
    size_t  nr_link_params;
    char    *link_params[MAX_PARAMETERS];
    size_t  link_params_len[MAX_PARAMETERS];
    char    *sql;
    size_t  nr_sql_params;
    char    *sql_params[MAX_PARAMETERS];
    size_t  sql_params_len[MAX_PARAMETERS];
    size_t  nr_csv_params;
    char    *csv_params[MAX_PARAMETERS];
    size_t  csv_params_len[MAX_PARAMETERS];
} view_t;

typedef struct {
    // Database connections pool.
    struct apr_array_header_t *connections;
    apr_thread_mutex_t        *connections_mutex;
    apr_uint32_t              nr_connections;
    char                      *connection_info;

    // Views.
    size_t nr_views;
    view_t views[MAX_VIEWS];

} mod_okioki_dir_config;

#endif
