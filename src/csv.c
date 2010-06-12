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
#include <apr_hash.h>
#include "csv.h"

int mod_okioki_buffer_append_value(apr_bucket_brigade *bb, apr_pool_t *pool, apr_bucket_alloc_t *alloc, char *s)
{
    int i, j;
    int need_quote = 0;
    char c;
    int s_length = strlen(s);
    apr_bucket *b;
    apr_bucket *b_before = APR_BRIGADE_LAST(bb);

    // Copy the string, and escape quotes.
    for (j = i = 0; i < s_length; i++) {
        c = s[i];
        switch (c) {
        case '"':
            // Add the text, before and including this quote.
            HTTP_ASSERT_NOT_NULL(
                b = apr_bucket_transient_create(&s[j], (i - j) + 1, alloc),
                HTTP_INTERNAL_SERVER_ERROR, "[mod_okioki] Could not allocate bucket."
            )
            APR_BRIGADE_INSERT_TAIL(bb, b);

            // Double the quote.
            HTTP_ASSERT_NOT_NULL(
                b = apr_bucket_immortal_create("\"", 1, alloc),
                HTTP_INTERNAL_SERVER_ERROR, "[mod_okioki] Could not allocate bucket."
            )
            APR_BRIGADE_INSERT_TAIL(bb, b);

            // Set j to just after this character.
            j = i + 1;
        case '\n':
        case '\r':
        case ',':
            need_quote++;
            break;
        }
    }
    if (j < i) {
        // Add the last piece of text.
        HTTP_ASSERT_NOT_NULL(
            b = apr_bucket_transient_create(&s[j], i - j, alloc),
            HTTP_INTERNAL_SERVER_ERROR, "[mod_okioki] Could not allocate bucket."
        )
        APR_BRIGADE_INSERT_TAIL(bb, b);
    }

    if (need_quote) {
        // Add a quote at the end of the value.
        HTTP_ASSERT_NOT_NULL(
            b = apr_bucket_immortal_create("\"", 1, alloc),
            HTTP_INTERNAL_SERVER_ERROR, "[mod_okioki] Could not allocate bucket."
        )
        APR_BRIGADE_INSERT_TAIL(bb, b);

        // Also add a quote before the value.
        HTTP_ASSERT_NOT_NULL(
            b = apr_bucket_immortal_create("\"", 1, alloc),
            HTTP_INTERNAL_SERVER_ERROR, "[mod_okioki] Could not allocate bucket."
        )
        APR_BUCKET_INSERT_AFTER(b_before, b);
    }

    return HTTP_OK;
}

apr_bucket_brigade *mod_okioki_generate_csv(apr_pool_t *pool, apr_bucket_alloc_t *alloc, view_t *view, apr_array_header_t *result)
{
    int i, j;
    char *value;
    apr_hash_t *row;
    apr_bucket_brigade *bb;
    apr_bucket *b;

    HTTP_ASSERT_NOT_NULL(
        bb = apr_brigade_create(pool, alloc),
        NULL, "[mod_okioki] Could not allocate a bucket brigade."
    )

    // Create a csv header.
    for (i = 0; i < view->nr_csv_params; i++) {
        // Add a comma between each entry.
        if (i != 0) {
            HTTP_ASSERT_NOT_NULL(
                b = apr_bucket_immortal_create(",", 1, alloc),
                NULL, "[mod_okioki] Could not allocate bucket."
            )
            APR_BRIGADE_INSERT_TAIL(bb, b);
        }

        // Copy the name of the column.
        HTTP_ASSERT_NOT_NULL(
            b = apr_bucket_immortal_create(view->csv_params[i], view->csv_params_len[i], alloc),
            NULL, "[mod_okioki] Could not allocate bucket."
        )
        APR_BRIGADE_INSERT_TAIL(bb, b);
    }
    // Finish off the header with a carriage return and linefeed.
    HTTP_ASSERT_NOT_NULL(
        b = apr_bucket_immortal_create("\r\n", 2, alloc),
        NULL, "[mod_okioki] Could not allocate bucket."
    )
    APR_BRIGADE_INSERT_TAIL(bb, b);

    // Check each row and figure out all the column names.
    for (j = 0; j < result->nelts; j++) {
        HTTP_ASSERT_NOT_NULL(
            row = APR_ARRAY_IDX(result, j, apr_hash_t *),
            NULL, "[mod_okioki.c] - Found a null row in the result."
        )

        // Create a csv header.
        for (i = 0; i < view->nr_csv_params; i++) {
            // Add a comma between each entry.
            if (i != 0) {
                HTTP_ASSERT_NOT_NULL(
                    b = apr_bucket_immortal_create(",", 1, alloc),
                    NULL, "[mod_okioki] Could not allocate bucket."
                )
                APR_BRIGADE_INSERT_TAIL(bb, b);
            }

            HTTP_ASSERT_NOT_NULL(
                value = apr_hash_get(row, view->csv_params[i], view->csv_params_len[i]),
                NULL, "[mod_okioki] column '%s' not found in result.", view->csv_params[i]
            )

            // Copy the value of the column.
            HTTP_ASSERT_OK(
                mod_okioki_buffer_append_value(bb, pool, alloc, value),
                NULL, "[mod_okioki] Not enough room to store CSV result."
            )
        }

        // Finish off the header with a carriage return and linefeed.
        HTTP_ASSERT_NOT_NULL(
            b = apr_bucket_immortal_create("\r\n", 2, alloc),
            NULL, "[mod_okioki] Could not allocate bucket."
        )
        APR_BRIGADE_INSERT_TAIL(bb, b);
    }

    // Add an end-of-stream.
    HTTP_ASSERT_NOT_NULL(
        b = apr_bucket_eos_create(alloc),
        NULL, "[mod_okioki] Could not allocate bucket."
    )
    APR_BRIGADE_INSERT_TAIL(bb, b);
    return bb;
}

