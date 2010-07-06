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
#include <stdlib.h>
#include <httpd.h>
#include <http_core.h>
#include <http_log.h>
#include <http_main.h>
#include <http_protocol.h>
#include <http_request.h>
#include <apr_hash.h>
#include <apr_dbd.h>
#include "json.h"

int mod_okioki_json_append_nonstring(apr_bucket_brigade *bb, apr_pool_t *pool, apr_bucket_alloc_t *alloc, const char *s, char **error)
{
    char       *t1;
    char       *t2;
    long long  r1;
    double     r2;
    apr_bucket *b;
    apr_bucket *b_before = APR_BRIGADE_LAST(bb);
    int        need_quote;

    if (s[0] == 0) {
        // NULL from database.
        ASSERT_NOT_NULL(
            b = apr_bucket_immortal_create("null", 4, alloc),
            HTTP_INTERNAL_SERVER_ERROR, "Could not allocate bucket."
        )
        APR_BRIGADE_INSERT_TAIL(bb, b);

    } else if (s[0] == 't' || s[0] == 'T' || s[0] == 'y' || s[0] == 'Y') {
        // NULL from database.
        ASSERT_NOT_NULL(
            b = apr_bucket_immortal_create("true", 4, alloc),
            HTTP_INTERNAL_SERVER_ERROR, "Could not allocate bucket."
        )
        APR_BRIGADE_INSERT_TAIL(bb, b);

    } else if (s[0] == 'f' || s[0] == 'F' || s[0] == 'n' || s[0] == 'N') {
        // NULL from database.
        ASSERT_NOT_NULL(
            b = apr_bucket_immortal_create("false", 5, alloc),
            HTTP_INTERNAL_SERVER_ERROR, "Could not allocate bucket."
        )
        APR_BRIGADE_INSERT_TAIL(bb, b);
    }

    // Check if the value is a float or int.
    r1 = strtoll(s, &t1, 10);
    r2 = strtod(s, &t2);
    if (t1[0] == 0 || t2[0] == 0) {
        // Could be fully parsed as an integer or float.
        need_quote = 0;
    } else {
        // It is something else
        need_quote = 1;
    }

    // Copy the value from the database to output.
    ASSERT_NOT_NULL(
        b = apr_bucket_transient_create(s, strlen(s), alloc),
        HTTP_INTERNAL_SERVER_ERROR, "Could not allocate bucket."
    )
    APR_BRIGADE_INSERT_TAIL(bb, b);

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

int mod_okioki_json_append_string(apr_bucket_brigade *bb, apr_pool_t *pool, apr_bucket_alloc_t *alloc, const char *s, char **error)
{
    int i, j;
    int need_quote = 1;
    char c;
    int s_length = strlen(s);
    apr_bucket *b;
    apr_bucket *b_before = APR_BRIGADE_LAST(bb);

    // Copy the string, and escape quotes.
    for (j = i = 0; i < s_length; i++) {
        c = s[i];
        if (c < 32 || c == '\\' || c == '"' || c == 127) {
            // Add the text, before but excluding this character.
            ASSERT_NOT_NULL(
                b = apr_bucket_transient_create(&s[j], (i - j), alloc),
                HTTP_INTERNAL_SERVER_ERROR, "Could not allocate bucket."
            )
            APR_BRIGADE_INSERT_TAIL(bb, b);

            // Escape the default set of control characters
            char *esc_c = NULL;
            switch (c) {
            case '"':   esc_c = "\\\""; break;
            case '\\':  esc_c = "\\\\"; break;
            case '\b':  esc_c = "\\b" ; break;
            case '\f':  esc_c = "\\f" ; break;
            case '\n':  esc_c = "\\n" ; break;
            case '\r':  esc_c = "\\r" ; break;
            case '\t':  esc_c = "\\t" ; break;
            }

            if (esc_c != NULL) {
                // The default set was encoded.
                ASSERT_NOT_NULL(
                    b = apr_bucket_immortal_create(esc_c, 2, alloc),
                    HTTP_INTERNAL_SERVER_ERROR, "Could not allocate bucket."
                )
                APR_BRIGADE_INSERT_TAIL(bb, b);
            } else {
                // Encode using escaped unicode.
                char esc_u[7];
                sprintf(esc_u, "\\u00%02hhx", c);
                ASSERT_NOT_NULL(
                    b = apr_bucket_immortal_create(esc_u, 6, alloc),
                    HTTP_INTERNAL_SERVER_ERROR, "Could not allocate bucket."
                )
                APR_BRIGADE_INSERT_TAIL(bb, b);
            }

            // Set j to just after this character.
            j = i + 1;
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

int mod_okioki_json_append_value(apr_bucket_brigade *bb, apr_pool_t *pool, apr_bucket_alloc_t *alloc, const char *s, int is_string, char **error)
{
    if (is_string) {
        return mod_okioki_json_append_string(bb, pool, alloc, s, error);
    } else {
        return mod_okioki_json_append_nonstring(bb, pool, alloc, s, error);
    }
}

int mod_okioki_generate_json(request_rec *http_request, apr_pool_t *pool, apr_bucket_alloc_t *alloc, const apr_dbd_driver_t *db_driver, apr_dbd_results_t *db_result, apr_hash_t *result_strings, char **error)
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

    // If there is more than one row we go in list mode.
    if (nr_rows > 1) {
        ASSERT_NOT_NULL(
            b = apr_bucket_immortal_create("[\n", 2, alloc),
            HTTP_INTERNAL_SERVER_ERROR, "Could not allocate bucket."
        )
        APR_BRIGADE_INSERT_TAIL(bb, b);
    }

    // Check each row and figure out all the column names.
    db_row = NULL;
    for (row_nr = 0; row_nr < nr_rows; row_nr++) {
        // Start an object/dictionary.
        if (row_nr == 0) {
            ASSERT_NOT_NULL(
                b = apr_bucket_immortal_create("{", 1, alloc),
                HTTP_INTERNAL_SERVER_ERROR, "Could not allocate bucket."
            )
            APR_BRIGADE_INSERT_TAIL(bb, b);
        } else {
            ASSERT_NOT_NULL(
                b = apr_bucket_immortal_create(", {", 3, alloc),
                HTTP_INTERNAL_SERVER_ERROR, "Could not allocate bucket."
            )
            APR_BRIGADE_INSERT_TAIL(bb, b);
        }

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
            if (col_nr == 0) {
                ASSERT_NOT_NULL(
                    b = apr_bucket_immortal_create("\n\t", 2, alloc),
                    HTTP_INTERNAL_SERVER_ERROR, "Could not allocate bucket."
                )
                APR_BRIGADE_INSERT_TAIL(bb, b);
            } else {
                ASSERT_NOT_NULL(
                    b = apr_bucket_immortal_create(",\n\t", 3, alloc),
                    HTTP_INTERNAL_SERVER_ERROR, "Could not allocate bucket."
                )
                APR_BRIGADE_INSERT_TAIL(bb, b);
            }

            ASSERT_NOT_NULL(
                name = apr_dbd_get_name(db_driver, db_result, col_nr),
                HTTP_INTERNAL_SERVER_ERROR, "Could not retrieve name of column from database result."
            )

            ASSERT_HTTP_OK(
                mod_okioki_json_append_value(bb, pool, alloc, name, 1, error),
                HTTP_INTERNAL_SERVER_ERROR, "Not enough room to store CSV result."
            )

            ASSERT_NOT_NULL(
                b = apr_bucket_immortal_create(": ", 2, alloc),
                HTTP_INTERNAL_SERVER_ERROR, "Could not allocate bucket."
            )
            APR_BRIGADE_INSERT_TAIL(bb, b);

            ASSERT_NOT_NULL(
                value = apr_dbd_get_entry(db_driver, db_row, col_nr),
                HTTP_INTERNAL_SERVER_ERROR, "Could not retrieve name of column from database result."
            )

            int is_string = (apr_hash_get(result_strings, name, APR_HASH_KEY_STRING) == result_strings);

            ASSERT_HTTP_OK(
                mod_okioki_json_append_value(bb, pool, alloc, value, is_string, error),
                HTTP_INTERNAL_SERVER_ERROR, "Not enough room to store CSV result."
            )
        }

        // Finish off the row with a carriage return and linefeed.
        ASSERT_NOT_NULL(
            b = apr_bucket_immortal_create("\n}", 2, alloc),
            HTTP_INTERNAL_SERVER_ERROR, "Could not allocate bucket."
        )
        APR_BRIGADE_INSERT_TAIL(bb, b);
    }

    if (nr_rows > 1) {
        ASSERT_NOT_NULL(
            b = apr_bucket_immortal_create("\n]", 2, alloc),
            HTTP_INTERNAL_SERVER_ERROR, "Could not allocate bucket."
        )
        APR_BRIGADE_INSERT_TAIL(bb, b);
    }

    ASSERT_NOT_NULL(
        b = apr_bucket_immortal_create("\n", 1, alloc),
        HTTP_INTERNAL_SERVER_ERROR, "Could not allocate bucket."
    )
    APR_BRIGADE_INSERT_TAIL(bb, b);

    // Add an end-of-stream.
    ASSERT_NOT_NULL(
        b = apr_bucket_eos_create(alloc),
        HTTP_INTERNAL_SERVER_ERROR, "Could not allocate bucket."
    )
    APR_BRIGADE_INSERT_TAIL(bb, b);

    // Return the data.
    ap_set_content_type(http_request, "application/json");
    http_request->status = HTTP_OK;
    return ap_pass_brigade(http_request->output_filters, bb);
}

