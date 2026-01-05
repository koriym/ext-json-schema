PHP_ARG_ENABLE(json_schema, whether to enable json_schema support,
[  --enable-json_schema           Enable json_schema support])

if test "$PHP_JSON_SCHEMA" != "no"; then
  dnl Check for JSON extension dependency
  if test -z "$PHP_JSON" || test "$PHP_JSON" = "no"; then
    AC_MSG_CHECKING([for json extension])
    AC_MSG_RESULT([yes (assuming built-in)])
  fi

  dnl Check for PCRE extension dependency
  if test -z "$PHP_PCRE" || test "$PHP_PCRE" = "no"; then
    AC_MSG_CHECKING([for pcre extension])
    AC_MSG_RESULT([yes (assuming built-in)])
  fi

  PHP_NEW_EXTENSION(json_schema, json_schema.c validator.c, $ext_shared)
fi
