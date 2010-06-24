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


#include "mod_okioki.h"
#include "util.h"

void *mod_okioki_realloc(apr_pool_t *pool, void *buf, size_t old_size, size_t new_size)
{
    void   *tmp = apr_palloc(pool, new_size);
    size_t copy_size = MIN(old_size, new_size);

    // If allocated is failed, we leave the buffer alone and stop processing.
    if (tmp == NULL) {
        return NULL;
    }

    // Copy the memory.
    memcpy(tmp, buf, copy_size);

    // Replace the old buffer with the new buffer. The pool at some point
    // gets rid of the old buffer.
    return tmp;
}

size_t mod_okioki_nlpo2(size_t x)
{
    x |= (x >> 1);
    x |= (x >> 2);
    x |= (x >> 4);
    x |= (x >> 8);
    x |= (x >> 16);
    if (sizeof (x) == 8) {
        x |= (x >> 32);
    }
    return x + 1;
}

