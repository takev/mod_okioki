

# Required initializer
AC_PREREQ(2.61)
AC_INIT([mod_tdbapi], [0.3], [take.vos@vosgames.nl])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([m4])

# Automake initialization
AM_INIT_AUTOMAKE(mod_tdbapi, 1.0)

# Add a test for a compiler.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LIBTOOL
AM_MISSING_PROG([GPERF], [gperf])

AP_VERSION=2.0.0
AP_CHECK_APACHE([$AP_VERSION], [
  LIBTOOL="`$APR_CONFIG --apr-libtool`"
  AC_SUBST([LIBTOOL])

  MODULE_CFLAGS="$AP_CFLAGS"
  AC_SUBST([MODULE_CFLAGS])

  MODULE_LDFLAGS="`$APR_CONFIG --link-libtool` `$APU_CONFIG --link-libtool`"
  AC_SUBST([MODULE_LDFLAGS])

  BIN_LDFLAGS="`$APR_CONFIG --link-libtool` `$APU_CONFIG --link-libtool` `$APR_CONFIG --ldflags --libs` `$APU_CONFIG --ldflags --libs`"
  AC_SUBST([BIN_LDFLAGS])

  prefix="$AP_PREFIX"
], AC_MSG_ERROR([*** Apache version $AP_VERSION not found!]))


# Write config.status and the Makefile
AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT

