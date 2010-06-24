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

#include <apr_strings.h>
#include <http_protocol.h>
#include "mod_okioki.h"
#include "urlencoding.h"

void mod_okioki_urldecode(char *out, const char *in)
{
    int i, j;
    int state = 0;
    int c;
    char buf[3]; buf[2] = 0;

    // Walk over all the input characters, until terminating zero.
    for (i = j = 0; (c = in[i]) != 0; i++) {
        switch (state) {
        case 0: // Normal character state.
            switch (c) {
            case '%': // Found a encoded character.
                state = 1; // Switch to the next state.
                break;
            case '+': // Found an encoded white space
                out[j++] = ' ';
                break;
            default: // Found normal character.
                out[j++] = c;
            }
            break;
        case 1: // First nible of an encoded character.
            state = 2; // Go to next state.
            buf[0] = c;
            break;
        case 2: // Second nible of an encoded character.
            state = 0; // Only two nibled in an encoded character so go back to normal state.
            buf[1] = c;

            // Decode a byte encoded as two hex nibles.
            sscanf(buf, "%2x", &c);
            out[j++] = c;
            break;
        }
    }

    // Terminate the output.
    out[j] = 0;
}

int mod_okioki_parse_query_phrase(request_rec *http_req, apr_hash_t *arguments, char *s)
{
    apr_pool_t *pool = http_req->pool;
    char *last;
    char *name;
    char *value;

    // Get the name by looking for a '=' character
    ASSERT_NOT_NULL(
        name = apr_strtok(s, "=", &last),
        HTTP_BAD_REQUEST, "[mod_okioki] Failed to get name from query phrase."
    )

    // Get the value, which is the rest, the '&' and ';' do not exist, so this runs till the end.
    ASSERT_NOT_NULL(
        value = apr_strtok(NULL, "&;", &last),
        HTTP_BAD_REQUEST, "[mod_okioki] Failed to get value from query phrase."
    )

    // Each part is encoded, decode it here.
    mod_okioki_urldecode(name, name);
    mod_okioki_urldecode(value, value);

    // Removing leading and trailing spaces.
    apr_collapse_spaces(name, name);
    apr_collapse_spaces(value, value);


    // name and value is already newly allocated by duplication of value.
    apr_hash_set(arguments, name, APR_HASH_KEY_STRING, value);

    return HTTP_OK;
}

int mod_okioki_parse_query(request_rec *http_req, apr_hash_t *arguments, char *_s)
{
    apr_pool_t *pool = http_req->pool;
    char *s;
    char *last;
    char *phrase;
    int  ret;

    // Check if there is a query to decode.
    if (_s == NULL) {
        return HTTP_OK;
    }

    // Copy the query string, as we will modify it, and we will add it to the hash table.
    ASSERT_NOT_NULL(
        s = apr_pstrdup(pool, _s),
        HTTP_INTERNAL_SERVER_ERROR, "[mod_okioki] Failed to copy query '%s'.", _s
    )

    // Split the query up in phrases using the '&' character as seperator.
    phrase = apr_strtok(s, "&;", &last);
    while (phrase) {
        // Parse the phrase.
        ASSERT_HTTP_OK(
            ret = mod_okioki_parse_query_phrase(http_req, arguments, phrase),
            ret, "[mod_okioki] Could not parse query."
        )
        phrase = apr_strtok(NULL, "&", &last);
    }

    return HTTP_OK;
}

