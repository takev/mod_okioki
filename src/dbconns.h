#ifndef CONNECTIONS_H
#define CONNECTIONS_H
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

#include <libpq-fe.h>
#include "mod_okioki.h"

/** Initialize the database part of the per directory config.
 * This initializes the stack, mutex, counter and dbconn info.
 *
 * @param cfg    The per-directory config of this module.
 * @returns      0 on success, -1 on error.
 */
int mod_okioki_dbconns_init(apr_pool_t *p, mod_okioki_dir_config *cfg);

/** Pop an database dbconn from the stack.
 *
 * @param req    The http request.
 * @param cfg    The per-directory config.
 * @returns      A postrgesql dbconn pointer, NULL on empty or error.
 */
PGconn *mod_okioki_pop_dbconn(request_rec *http_req, mod_okioki_dir_config *cfg);

/** Push a dbconn back on the stack.
 *
 * @param req      The http request.
 * @param cfg      The per-directory config.
 * @param db_conn  The postrgresql dbconn to put back on the stack.
 * @returns        0 on success, -1 on lock error, -2 on unlock error.
 */
int mod_okioki_push_dbconn(request_rec *http_req, mod_okioki_dir_config *cfg, PGconn *db_conn);

/** Get a database dbconn.
 * Get a unused database dbconn, this may come from the resource pool or created fresh.
 *
 * @param req    The http request.
 * @param cfg    The per-directory config.
 * @returns      A postrgesql dbconn pointer, NULL on empty or error.
 */
PGconn *mod_okioki_get_dbconn(request_rec *http_req, mod_okioki_dir_config *cfg);

/** Return a dbconn, back to the resource pool.
 *
 * @param req               The http request.
 * @param cfg               The per-directory config.
 * @param db_conn           The postrgresql dbconn to put back on the stack.
 * @param close_dbconn  1 if the dbconn needs to be closed instead of pushed back on the stack.
 * @returns                 0 on success, <0 on error.
 */
int mod_okioki_return_dbconn(request_rec *http_req, mod_okioki_dir_config *cfg, PGconn *db_conn, int close_dbconn);

int mod_okioki_execute_view(request_rec *http_request, mod_okioki_dir_config *cfg, view_t *view, apr_hash_t *arguments, apr_array_header_t *result);

#endif
