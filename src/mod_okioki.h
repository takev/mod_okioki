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
#include <apr_strings.h>

#define MAX_PARAMETERS 32
#define MAX_VIEWS 200
#define MAX_ROWS 1024
#define MIN_INPUT_BUFFER   65536        // 64 kbyte
#define MAX_INPUT_BUFFER   67108864     // 64 MByte

#define ASSERT_NOT_NULL(expr, http_code, msg...) \
    if (__builtin_expect((expr) == NULL, 0)) { \
        *error = apr_psprintf(pool, msg); \
        ap_log_perror(APLOG_MARK, APLOG_ERR, 0, pool, "[mod_okioki] " msg); \
        return http_code; \
    }

#define ASSERT_HTTP_OK(expr, http_code, msg...) \
    if (__builtin_expect((expr) != HTTP_OK, 0)) { \
        *error = apr_psprintf(pool, msg); \
        ap_log_perror(APLOG_MARK, APLOG_ERR, 0, pool, "[mod_okioki] " msg); \
        return http_code; \
    }

#define ASSERT_APR_SUCCESS(expr, http_code, msg...) \
    if (__builtin_expect((expr) != APR_SUCCESS, 0)) { \
        *error = apr_psprintf(pool, msg); \
        ap_log_perror(APLOG_MARK, APLOG_ERR, 0, pool, "[mod_okioki] " msg); \
        return http_code; \
    }

#define ASSERT_POSITIVE(expr, http_code, msg...) \
    if (__builtin_expect((expr) < 0, 0)) { \
        *error = apr_psprintf(pool, msg); \
        ap_log_perror(APLOG_MARK, APLOG_ERR, 0, pool, "[mod_okioki] " msg); \
        return http_code; \
    }

#define ASSERT_ZERO(expr, http_code, msg...) \
    if (__builtin_expect((expr) != 0, 0)) { \
        *error = apr_psprintf(pool, msg); \
        ap_log_perror(APLOG_MARK, APLOG_ERR, 0, pool, "[mod_okioki] " msg); \
        return http_code; \
    }

typedef enum {
    O_CSV,
    O_JSON
} output_type_t;

typedef struct {
    char           *sql;
    size_t         sql_len;
    size_t         nr_sql_params;
    char           *sql_params[MAX_PARAMETERS];
    size_t         sql_params_len[MAX_PARAMETERS];
    output_type_t  output_type;
    apr_hash_t     *result_strings;
} view_t;

typedef struct {
    // Views.
    apr_hash_t *views;
    apr_hash_t *result_strings;
} mod_okioki_dir_config;

#endif
