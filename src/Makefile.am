moddir = ${AP_LIBEXECDIR}
mod_LTLIBRARIES = mod_okioki.la

mod_okioki_la_SOURCES = mod_okioki.c views.c cookies.c urlencoding.c csv.c
mod_okioki_la_CFLAGS = -Wall ${MODULE_CFLAGS} ${POSTGRESQL_CFLAGS}
mod_okioki_la_LDFLAGS = -avoid-version -module ${MODULE_LDFLAGS} ${POSTGRESQL_LDFLAGS}

