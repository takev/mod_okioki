#ifndef URLENCODING_H
#define URLENCODING_H
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

void mod_okioki_urldecode(char *out, const char *in);
int mod_okioki_parse_query_phrase(request_rec *http_req, apr_hash_t *arguments, char *s, char **error);
int mod_okioki_parse_query(request_rec *http_req, apr_hash_t *arguments, char *_s, char **error);


#endif
