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
#include <http_core.h>
#include <http_log.h>
#include <http_main.h>
#include <http_protocol.h>
#include <http_request.h>
#include <apr_hash.h>
#include <apr_dbd.h>
#include "csv.h"

int mod_okioki_csv_append_value(apr_bucket_brigade *bb, apr_pool_t *pool, apr_bucket_alloc_t *alloc, const char *s, char **error)
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
            ASSERT_NOT_NULL(
                b = apr_bucket_transient_create(&s[j], (i - j) + 1, alloc),
                HTTP_INTERNAL_SERVER_ERROR, "Could not allocate bucket."
            )
            APR_BRIGADE_INSERT_TAIL(bb, b);

            // Double the quote.
            ASSERT_NOT_NULL(
                b = apr_bucket_immortal_create("\"", 1, alloc),
                HTTP_INTERNAL_SERVER_ERROR, "Could not allocate bucket."
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
        ASSERT_NOT_NULL(
            b = apr_bucket_transient_create(&s[j], i - j, alloc),
            HTTP_INTERNAL_SERVER_ERROR, "Could not allocate bucket."
        )
        APR_BRIGADE_INSERT_TAIL(bb, b);
    }

    if (need_quote) {
        // Add a quote at the end of the value.
        ASSERT_NOT_NULL(
            b = apr_bucket_immortal_create("\"", 1, alloc),
            HTTP_INTERNAL_SERVER_ERROR, "Could not allocate bucket."
        )
        APR_BRIGADE_INSERT_TAIL(bb, b);

        // Also add a quote before the value.
        ASSERT_NOT_NULL(
            b = apr_bucket_immortal_create("\"", 1, alloc),
            HTTP_INTERNAL_SERVER_ERROR, "Could not allocate bucket."
        )
        APR_BUCKET_INSERT_AFTER(b_before, b);
    }

    return HTTP_OK;
}

int mod_okioki_generate_csv(request_rec *http_request, apr_pool_t *pool, apr_bucket_alloc_t *alloc, const apr_dbd_driver_t *db_driver, apr_dbd_results_t *db_result, char **error)
{
    const char *name;
    const char *value;
    apr_dbd_row_t *db_row;
    apr_bucket_brigade *bb;
    apr_bucket *b;
    int col_nr;
    int nr_cols;
    int row_nr;
    int nr_rows;

    ASSERT_NOT_NULL(
        bb = apr_brigade_create(pool, alloc),
        HTTP_INTERNAL_SERVER_ERROR, "Could not allocate a bucket brigade."
    )

    nr_cols = apr_dbd_num_cols(db_driver, db_result);
    nr_rows = apr_dbd_num_tuples(db_driver, db_result);

    // Create a csv header.
    for (col_nr = 0; col_nr < nr_cols; col_nr++) {
        // Add a comma between each entry.
        if (col_nr != 0) {
            ASSERT_NOT_NULL(
                b = apr_bucket_immortal_create(",", 1, alloc),
                HTTP_INTERNAL_SERVER_ERROR, "Could not allocate bucket."
            )
            APR_BRIGADE_INSERT_TAIL(bb, b);
        }

        ASSERT_NOT_NULL(
            name = apr_dbd_get_name(db_driver, db_result, col_nr),
            HTTP_INTERNAL_SERVER_ERROR, "Could not retrieve name of column from database result."
        )

        ASSERT_HTTP_OK(
            mod_okioki_csv_append_value(bb, pool, alloc, name, error),
            HTTP_INTERNAL_SERVER_ERROR, "Not enough room to store CSV result."
        )
    }

    // Finish off the header with a carriage return and linefeed.
    ASSERT_NOT_NULL(
        b = apr_bucket_immortal_create("\r\n", 2, alloc),
        HTTP_INTERNAL_SERVER_ERROR, "Could not allocate bucket."
    )
    APR_BRIGADE_INSERT_TAIL(bb, b);

    // Check each row and figure out all the column names.
    db_row = NULL;
    for (row_nr = 0; row_nr < nr_rows; row_nr++) {
        ASSERT_APR_SUCCESS(
            apr_dbd_get_row(db_driver, pool, db_result, &db_row, -1),
            HTTP_INTERNAL_SERVER_ERROR, "Could not get row"
        )
        ASSERT_NOT_NULL(
            db_row,
            HTTP_INTERNAL_SERVER_ERROR, "Could not retrieve row."
        )

        for (col_nr = 0; col_nr < nr_cols; col_nr++) {
            // Add a comma between each entry.
            if (col_nr != 0) {
                ASSERT_NOT_NULL(
                    b = apr_bucket_immortal_create(",", 1, alloc),
                    HTTP_INTERNAL_SERVER_ERROR, "Could not allocate bucket."
                )
                APR_BRIGADE_INSERT_TAIL(bb, b);
            }

            ASSERT_NOT_NULL(
                value = apr_dbd_get_entry(db_driver, db_row, col_nr),
                HTTP_INTERNAL_SERVER_ERROR, "Could not retrieve name of column from database result."
            )

            ASSERT_HTTP_OK(
                mod_okioki_csv_append_value(bb, pool, alloc, value, error),
                HTTP_INTERNAL_SERVER_ERROR, "Not enough room to store CSV result."
            )
        }

        // Finish off the row with a carriage return and linefeed.
        ASSERT_NOT_NULL(
            b = apr_bucket_immortal_create("\r\n", 2, alloc),
            HTTP_INTERNAL_SERVER_ERROR, "Could not allocate bucket."
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
    ap_set_content_type(http_request, "text/csv");
    http_request->status = HTTP_OK;
    return ap_pass_brigade(http_request->output_filters, bb);
}

