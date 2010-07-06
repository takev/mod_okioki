#ifndef JSON_H
#define JSON_H
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
#include <apr_tables.h>
#include "mod_okioki.h"

int mod_okioki_generate_json(request_rec *http_request, apr_pool_t *pool, apr_bucket_alloc_t *alloc, const apr_dbd_driver_t *db_driver, apr_dbd_results_t *db_result, apr_hash_t *result_strings, char **error);

#endif
