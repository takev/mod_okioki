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

#define MAX_CSV_LINE_SIZE 256

int mod_okioki_buffer_append(char *buffer, size_t buffer_size, size_t *buffer_length, char *s, int s_length)
{
    // If unknown, calculate the length of s.
    if (s_length == -1) {
        s_length = strlen(s);
    }

    // Check if there is enough room in the buffer.
    if (*buffer_length + s_length > buffer_size) {
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    // Copy s into the buffer. We know the length, so no strcpy needed.
    memcpy(&buffer[*buffer_length], s, s_length);
    *buffer_length += s_length;
    return HTTP_OK;
}

int mod_okioki_buffer_append_value(apr_pool_t *pool, char *buffer, size_t buffer_size, size_t *buffer_length, char *s, int s_length)
{
    int i, j;
    int need_quote = 0;
    int need_escape = 0;
    char *s2;
    char c;
    int ret;

    // If the length of s is unknown, calculate it.
    if (s_length == -1) {
        s_length = strlen(s);
    }

    // PASS 1: Check the string for any characters that need to be escaped.
    for (i = 0; i < s_length; i++) {
        c = s[i];
        switch (c) {
        case '"':
            need_escape++; // fall tru
        case '\n':
        case '\r':
        case ',':
            need_quote++;
            break;
        }
    }

    // PASS 2: If there was quote we need to escape for each quote.
    if (need_escape) {
        // Escaping uses more space, so allocate a new string.
        s2 = apr_palloc(pool, s_length + need_escape + 1);

        // Copy the string, doubling a quote and ending with a nul.
        for (j = i = 0; i < (s_length + 1); i++) {
            c = s[i];
            s2[j++] = c;
            if (c == '"') {
                s2[j++] = c;
            }
        }

        // After this we are going to use s like normally.
        s = s2;
    }

    // Add a quote if the string needed to be quoted.
    if (need_quote) {
        HTTP_ASSERT_OK(
            ret = mod_okioki_buffer_append(buffer, buffer_size, buffer_length, "\"", 1),
            ret, "[mod_okioki] Not enough room to store CSV result."
        )
    }

    // Add the "escaped" string.
    HTTP_ASSERT_OK(
        ret = mod_okioki_buffer_append(buffer, buffer_size, buffer_length, s, s_length + need_escape),
        ret, "[mod_okioki] Not enough room to store CSV result."
    )

    // Add an other quote if the string needed to be quoted.
    if (need_quote) {
        HTTP_ASSERT_OK(
            ret = mod_okioki_buffer_append(buffer, buffer_size, buffer_length, "\"", 1),
            ret, "[mod_okioki] Not enough room to store CSV result."
        )
    }

    return HTTP_OK;
}

char *mod_okioki_generate_csv(apr_pool_t *pool, view_t *view, apr_array_header_t *result, size_t *result_s_len)
{
    int i, j;
    char *value;
    apr_hash_t *row;

    size_t  buffer_size = (result->nelts + 1) * MAX_CSV_LINE_SIZE;
    size_t  buffer_length = 0;
    char    *buffer = apr_palloc(pool, buffer_size);

    *result_s_len = 0;

    if (result == NULL || result->nelts == 0) {
        return "";
    }

    // Create a csv header.
    for (i = 0; i < view->nr_csv_params; i++) {
        // Add a comma between each entry.
        if (i != 0) {
            HTTP_ASSERT_OK(
                mod_okioki_buffer_append(buffer, buffer_size, &buffer_length, ",", 1),
                NULL, "[mod_okioki] Not enough room to store CSV result."
            )
        }

        // Copy the name of the column.
        HTTP_ASSERT_OK(
            mod_okioki_buffer_append_value(pool, buffer, buffer_size, &buffer_length, view->csv_params[i], view->csv_params_len[i]),
            NULL, "[mod_okioki] Not enough room to store CSV result."
        )
    }
    // Finish off the header with a carriage return and linefeed.
    HTTP_ASSERT_OK(
        mod_okioki_buffer_append(buffer, buffer_size, &buffer_length, "\r\n", 2),
        NULL, "[mod_okioki] Not enough room to store CSV result."
    )

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
                HTTP_ASSERT_OK(
                    mod_okioki_buffer_append(buffer, buffer_size, &buffer_length, ",", 1),
                    NULL, "[mod_okioki] Not enough room to store CSV result."
                )
            }

            HTTP_ASSERT_NOT_NULL(
                value = apr_hash_get(row, view->csv_params[i], view->csv_params_len[i]),
                NULL, "[mod_okioki] column '%s' not found in result.", view->csv_params[i]
            )

            // Copy the name of the column.
            HTTP_ASSERT_OK(
                mod_okioki_buffer_append_value(pool, buffer, buffer_size, &buffer_length, value, -1),
                NULL, "[mod_okioki] Not enough room to store CSV result."
            )
        }

        // Finish off the header with a carriage return and linefeed.
        HTTP_ASSERT_OK(
            mod_okioki_buffer_append(buffer, buffer_size, &buffer_length, "\r\n", 2),
            NULL, "[mod_okioki] Not enough room to store CSV result."
        )
    }

    *result_s_len = buffer_length;
    return buffer;
}

