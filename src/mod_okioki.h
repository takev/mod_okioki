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
#include <apr_hash.h>

#define MAX_PARAMETERS 32
#define MAX_VIEWS 200
#define MAX_ROWS 1024
#define MIN_INPUT_BUFFER   65536        // 64 kbyte
#define MAX_INPUT_BUFFER   67108864     // 64 MByte

#define ASSERT_NOT_NULL(expr, http_code, msg...) \
    if (__builtin_expect((expr) == NULL, 0)) { \
        ap_log_perror(APLOG_MARK, APLOG_ERR, 0, pool, msg); \
        return http_code; \
    }

#define ASSERT_HTTP_OK(expr, http_code, msg...) \
    if (__builtin_expect((expr) != HTTP_OK, 0)) { \
        ap_log_perror(APLOG_MARK, APLOG_ERR, 0, pool, msg); \
        return http_code; \
    }

#define ASSERT_APR_SUCCESS(expr, http_code, msg...) \
    if (__builtin_expect((expr) != APR_SUCCESS, 0)) { \
        ap_log_perror(APLOG_MARK, APLOG_ERR, 0, pool, msg); \
        return http_code; \
    }

#define ASSERT_NOT_NEG(expr, http_code, msg...) \
    if (__builtin_expect((expr) < 0, 0)) { \
        ap_log_perror(APLOG_MARK, APLOG_ERR, 0, pool, msg); \
        return http_code; \
    }

#define ASSERT_ZERO(expr, http_code, msg...) \
    if (__builtin_expect((expr) != 0, 0)) { \
        ap_log_perror(APLOG_MARK, APLOG_ERR, 0, pool, msg); \
        return http_code; \
    }

#define O_CSV      1
#define O_COOKIE   2

typedef struct {
    char    *sql;
    size_t  sql_len;
    size_t  nr_sql_params;
    char    *sql_params[MAX_PARAMETERS];
    size_t  sql_params_len[MAX_PARAMETERS];
    int     output_type;
} view_t;

typedef struct {
    // Views.
    apr_hash_t *get_views;
    apr_hash_t *post_views;
    apr_hash_t *put_views;
    apr_hash_t *delete_views;
} mod_okioki_dir_config;

#endif
