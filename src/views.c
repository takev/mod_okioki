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

#include <sys/types.h>
#include <regex.h>
#include <apr_hash.h>
#include <apr_strings.h>
#include <http_protocol.h>
#include "views.h"

#define MAX_ARGUMENTS 32


view_t *mod_okioki_view_lookup(mod_okioki_dir_config *cfg, request_rec *http_req, apr_hash_t *arguments)
{
    int k;
    regmatch_t subs[MAX_ARGUMENTS + 1];
    apr_pool_t *pool   = http_req->pool;
    off_t      view_nr;
    view_t     *view;

    // Walk through each configured url.
    for (view_nr = 0; view_nr < cfg->nr_views; view_nr++) {
        view = &cfg->views[view_nr];

        // Test the path info against the url.
        if (
            view->link_cmd == http_req->method_number &&
            regexec(&view->link, http_req->path_info, MAX_ARGUMENTS + 1, subs, 0) == 0
        ) {
            // Found the url with the current regex, jump forward to extract the arguments.
            goto found;
        }
    }
    // The path_info did not match any of the configured urls.
    return NULL;

found:

    // Extract parameters from url.
    for (k = 0; k < MAX_ARGUMENTS; k++) {
        char *key = view->link_params[k];
        size_t key_len = view->link_params_len[k];

        if (subs[k + 1].rm_so != -1 && key != NULL) {
            // Copy the value from the path_info.
            size_t val_len = subs[k + 1].rm_eo - subs[k + 1].rm_so;
            char *val = apr_pstrndup(pool, &http_req->path_info[subs[k + 1].rm_so], val_len);

            // Add to the arguments.
            apr_hash_set(arguments, key, key_len, val);
        }
    }

    return view;
}


