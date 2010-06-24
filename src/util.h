#ifndef MOD_OKIOKI_UTIL_H
#define MOD_OKIOKI_UTIL_H
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

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) < (b) ? (a) : (b))
#endif

/** Reallocates memory from the pool.
 * This algorithm is designed to grow a buffer, it may return a new pointer for this.
 * @param pool      The pool to allocate the buffer from.
 * @param buf       The original buffer.
 * @param old_size  The amount of data to copy over.
 * @param new_size  The new size of the buffer.
 * @returns         The new buffer, or NULL on failure
 */
void *mod_okioki_realloc(apr_pool_t *pool, void *buf, size_t old_size, size_t new_size);

/** Next Largest Power of 2.
 * This calculated the next power of 2 for the integer x.
 * This algorithm is computed through the SWAR algorithm, which attempts to set
 * all the bits on the right of the most significant bit of x, then adds one.
 *
 * @param  x  unsigned integer.
 * @returns   The next largest power of 2.
 */
size_t mod_okioki_nlpo2(size_t x);

#endif
