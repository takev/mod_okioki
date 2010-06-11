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

#include "cookies.h"

typedef struct {
    request_rec *http_req;
    apr_hash_t  *arguments;
} rec_t;

int mod_okioki_get_cookie(void *_rec, const char *key, const char *_value)
{
    rec_t *rec = (rec_t *)_rec;
    apr_pool_t *pool = rec->http_req->pool;
    char *value;
    char *last;
    char *k;
    char *v;

    // Extra check to make sure we only handle Cookie headers.
    if (strcmp(key, "Cookie") != 0) {
        ap_log_perror(APLOG_MARK, APLOG_WARNING, 0, pool, "[mod_okioki.c] - Expected only Cookie header, found '%s'.", key);
        return 0;
    }

    // Make a copy of the value.
    if ((value = apr_pstrdup(pool, _value)) == NULL) {
        ap_log_perror(APLOG_MARK, APLOG_ERR, 0, pool, "[mod_okioki.c] - Failed to copy cookie '%s'.", _value);
        return 0;
    }

    // Take the key from the header.
    if ((k = apr_strtok(value, "=", &last)) == NULL) {
        ap_log_perror(APLOG_MARK, APLOG_WARNING, 0, pool, "[mod_okioki.c] - Failed to get name of the Cookie.");
        return 0;
    }

    // Take the value from the header.
    if ((v = apr_strtok(NULL, ";", &last)) == NULL) {
        ap_log_perror(APLOG_MARK, APLOG_WARNING, 0, pool, "[mod_okioki.c] - Failed to get value of the Cookie.");
        return 0;
    }

    // Remove whitespace.
    apr_collapse_spaces(k, k);
    apr_collapse_spaces(v, v);

    // k and v is already newly allocated by duplication of value.
    apr_hash_set(rec->arguments, k, strlen(k), v);

    return 1;
}

int mod_okioki_get_cookies(request_rec *http_req, apr_hash_t *arguments)
{
    rec_t rec;

    // Extract parameters from cookies.
    rec.http_req = http_req;
    rec.arguments = arguments;
    if (apr_table_do(mod_okioki_get_cookie, &rec, http_req->headers_in, "Cookie", NULL) == 0) {
        return HTTP_BAD_REQUEST;
    }

    return HTTP_OK;
}

