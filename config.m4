dnl $ Id: $
dnl config.m4 for extension rdns

PHP_ARG_WITH(rdns, for rdns support,
[  --with-rdns             Include RDNS support])

if test -z "$PHP_DEBUG"; then
  AC_ARG_ENABLE(debug,
  [  --enable-debug          compile with debugging symbols],[
    PHP_DEBUG=$enableval
  ],[    PHP_DEBUG=no
  ])
fi

if test "$PHP_RDNS" = "yes"; then
  AC_DEFINE(HAVE_RDNS, 1, [Whether you have RDNS])

  PHP_RDNS_SOURCES=rdns.c

  PHP_ADD_LIBRARY(ev, 1, RDNS_SHARED_LIBADD)
  PHP_ADD_LIBRARY(rdns, 1, RDNS_SHARED_LIBADD)
  PHP_SUBST(RDNS_SHARED_LIBADD)
  PHP_NEW_EXTENSION(rdns, $PHP_RDNS_SOURCES, $ext_shared)
fi
