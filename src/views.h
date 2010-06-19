#ifndef VIEWS_H
#define VIEWS_H
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
#include <apr_dbd.h>
#include "mod_okioki.h"

/** Handle the view.
 */
int mod_okioki_view_execute(request_rec *http_request, mod_okioki_dir_config *cfg, view_t *view, apr_hash_t *arguments, const apr_dbd_driver_t **db_driver, apr_dbd_results_t **db_result);

#endif
