/*
** $Id: loslib.c $
** Standard Operating System library
** See Copyright Notice in lua.h
*/

// Defines for Textadept's process spawning extension.
#if __linux__
#define _XOPEN_SOURCE 1 // for kill from signal.h
#define _XOPEN_SOURCE_EXTENDED 1 // for kill from signal.h
#if !GTK
#define _GNU_SOURCE 1 // for execvpe from unistd.h
#endif
#endif

#define loslib_c
#define LUA_LIB

#include "lprefix.h"


#include <errno.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"


/*
** {==================================================================
** List of valid conversion specifiers for the 'strftime' function;
** options are grouped by length; group of length 2 start with '||'.
** ===================================================================
*/
#if !defined(LUA_STRFTIMEOPTIONS)	/* { */

/* options for ANSI C 89 (only 1-char options) */
#define L_STRFTIMEC89		"aAbBcdHIjmMpSUwWxXyYZ%"

/* options for ISO C 99 and POSIX */
#define L_STRFTIMEC99 "aAbBcCdDeFgGhHIjmMnprRStTuUVwWxXyYzZ%" \
    "||" "EcECExEXEyEY" "OdOeOHOIOmOMOSOuOUOVOwOWOy"  /* two-char options */

/* options for Windows */
#define L_STRFTIMEWIN "aAbBcdHIjmMpSUwWxXyYzZ%" \
    "||" "#c#x#d#H#I#j#m#M#S#U#w#W#y#Y"  /* two-char options */

#if defined(LUA_USE_WINDOWS)
#define LUA_STRFTIMEOPTIONS	L_STRFTIMEWIN
#elif defined(LUA_USE_C89)
#define LUA_STRFTIMEOPTIONS	L_STRFTIMEC89
#else  /* C99 specification */
#define LUA_STRFTIMEOPTIONS	L_STRFTIMEC99
#endif

#endif					/* } */
/* }================================================================== */


/*
** {==================================================================
** Configuration for time-related stuff
** ===================================================================
*/

/*
** type to represent time_t in Lua
*/
#if !defined(LUA_NUMTIME)	/* { */

#define l_timet			lua_Integer
#define l_pushtime(L,t)		lua_pushinteger(L,(lua_Integer)(t))
#define l_gettime(L,arg)	luaL_checkinteger(L, arg)

#else				/* }{ */

#define l_timet			lua_Number
#define l_pushtime(L,t)		lua_pushnumber(L,(lua_Number)(t))
#define l_gettime(L,arg)	luaL_checknumber(L, arg)

#endif				/* } */


#if !defined(l_gmtime)		/* { */
/*
** By default, Lua uses gmtime/localtime, except when POSIX is available,
** where it uses gmtime_r/localtime_r
*/

#if defined(LUA_USE_POSIX)	/* { */

#define l_gmtime(t,r)		gmtime_r(t,r)
#define l_localtime(t,r)	localtime_r(t,r)

#else				/* }{ */

/* ISO C definitions */
#define l_gmtime(t,r)		((void)(r)->tm_sec, gmtime(t))
#define l_localtime(t,r)	((void)(r)->tm_sec, localtime(t))

#endif				/* } */

#endif				/* } */

/* }================================================================== */


/*
** {==================================================================
** Configuration for 'tmpnam':
** By default, Lua uses tmpnam except when POSIX is available, where
** it uses mkstemp.
** ===================================================================
*/
#if !defined(lua_tmpnam)	/* { */

#if defined(LUA_USE_POSIX)	/* { */

#include <unistd.h>

#define LUA_TMPNAMBUFSIZE	32

#if !defined(LUA_TMPNAMTEMPLATE)
#define LUA_TMPNAMTEMPLATE	"/tmp/lua_XXXXXX"
#endif

#define lua_tmpnam(b,e) { \
        strcpy(b, LUA_TMPNAMTEMPLATE); \
        e = mkstemp(b); \
        if (e != -1) close(e); \
        e = (e == -1); }

#else				/* }{ */

/* ISO C definitions */
#define LUA_TMPNAMBUFSIZE	L_tmpnam
#define lua_tmpnam(b,e)		{ e = (tmpnam(b) == NULL); }

#endif				/* } */

#endif				/* } */
/* }================================================================== */



static int os_execute (lua_State *L) {
  const char *cmd = luaL_optstring(L, 1, NULL);
  int stat;
  errno = 0;
  stat = system(cmd);
  if (cmd != NULL)
    return luaL_execresult(L, stat);
  else {
    lua_pushboolean(L, stat);  /* true if there is a shell */
    return 1;
  }
}


static int os_remove (lua_State *L) {
  const char *filename = luaL_checkstring(L, 1);
  return luaL_fileresult(L, remove(filename) == 0, filename);
}


static int os_rename (lua_State *L) {
  const char *fromname = luaL_checkstring(L, 1);
  const char *toname = luaL_checkstring(L, 2);
  return luaL_fileresult(L, rename(fromname, toname) == 0, NULL);
}


static int os_tmpname (lua_State *L) {
  char buff[LUA_TMPNAMBUFSIZE];
  int err;
  lua_tmpnam(buff, err);
  if (l_unlikely(err))
    return luaL_error(L, "unable to generate a unique filename");
  lua_pushstring(L, buff);
  return 1;
}


static int os_getenv (lua_State *L) {
  lua_pushstring(L, getenv(luaL_checkstring(L, 1)));  /* if NULL push nil */
  return 1;
}


static int os_clock (lua_State *L) {
  lua_pushnumber(L, ((lua_Number)clock())/(lua_Number)CLOCKS_PER_SEC);
  return 1;
}


/*
** {======================================================
** Time/Date operations
** { year=%Y, month=%m, day=%d, hour=%H, min=%M, sec=%S,
**   wday=%w+1, yday=%j, isdst=? }
** =======================================================
*/

/*
** About the overflow check: an overflow cannot occur when time
** is represented by a lua_Integer, because either lua_Integer is
** large enough to represent all int fields or it is not large enough
** to represent a time that cause a field to overflow.  However, if
** times are represented as doubles and lua_Integer is int, then the
** time 0x1.e1853b0d184f6p+55 would cause an overflow when adding 1900
** to compute the year.
*/
static void setfield (lua_State *L, const char *key, int value, int delta) {
  #if (defined(LUA_NUMTIME) && LUA_MAXINTEGER <= INT_MAX)
    if (l_unlikely(value > LUA_MAXINTEGER - delta))
      luaL_error(L, "field '%s' is out-of-bound", key);
  #endif
  lua_pushinteger(L, (lua_Integer)value + delta);
  lua_setfield(L, -2, key);
}


static void setboolfield (lua_State *L, const char *key, int value) {
  if (value < 0)  /* undefined? */
    return;  /* does not set field */
  lua_pushboolean(L, value);
  lua_setfield(L, -2, key);
}


/*
** Set all fields from structure 'tm' in the table on top of the stack
*/
static void setallfields (lua_State *L, struct tm *stm) {
  setfield(L, "year", stm->tm_year, 1900);
  setfield(L, "month", stm->tm_mon, 1);
  setfield(L, "day", stm->tm_mday, 0);
  setfield(L, "hour", stm->tm_hour, 0);
  setfield(L, "min", stm->tm_min, 0);
  setfield(L, "sec", stm->tm_sec, 0);
  setfield(L, "yday", stm->tm_yday, 1);
  setfield(L, "wday", stm->tm_wday, 1);
  setboolfield(L, "isdst", stm->tm_isdst);
}


static int getboolfield (lua_State *L, const char *key) {
  int res;
  res = (lua_getfield(L, -1, key) == LUA_TNIL) ? -1 : lua_toboolean(L, -1);
  lua_pop(L, 1);
  return res;
}


static int getfield (lua_State *L, const char *key, int d, int delta) {
  int isnum;
  int t = lua_getfield(L, -1, key);  /* get field and its type */
  lua_Integer res = lua_tointegerx(L, -1, &isnum);
  if (!isnum) {  /* field is not an integer? */
    if (l_unlikely(t != LUA_TNIL))  /* some other value? */
      return luaL_error(L, "field '%s' is not an integer", key);
    else if (l_unlikely(d < 0))  /* absent field; no default? */
      return luaL_error(L, "field '%s' missing in date table", key);
    res = d;
  }
  else {
    /* unsigned avoids overflow when lua_Integer has 32 bits */
    if (!(res >= 0 ? (lua_Unsigned)res <= (lua_Unsigned)INT_MAX + delta
                   : (lua_Integer)INT_MIN + delta <= res))
      return luaL_error(L, "field '%s' is out-of-bound", key);
    res -= delta;
  }
  lua_pop(L, 1);
  return (int)res;
}


static const char *checkoption (lua_State *L, const char *conv,
                                ptrdiff_t convlen, char *buff) {
  const char *option = LUA_STRFTIMEOPTIONS;
  int oplen = 1;  /* length of options being checked */
  for (; *option != '\0' && oplen <= convlen; option += oplen) {
    if (*option == '|')  /* next block? */
      oplen++;  /* will check options with next length (+1) */
    else if (memcmp(conv, option, oplen) == 0) {  /* match? */
      memcpy(buff, conv, oplen);  /* copy valid option to buffer */
      buff[oplen] = '\0';
      return conv + oplen;  /* return next item */
    }
  }
  luaL_argerror(L, 1,
    lua_pushfstring(L, "invalid conversion specifier '%%%s'", conv));
  return conv;  /* to avoid warnings */
}


static time_t l_checktime (lua_State *L, int arg) {
  l_timet t = l_gettime(L, arg);
  luaL_argcheck(L, (time_t)t == t, arg, "time out-of-bounds");
  return (time_t)t;
}


/* maximum size for an individual 'strftime' item */
#define SIZETIMEFMT	250


static int os_date (lua_State *L) {
  size_t slen;
  const char *s = luaL_optlstring(L, 1, "%c", &slen);
  time_t t = luaL_opt(L, l_checktime, 2, time(NULL));
  const char *se = s + slen;  /* 's' end */
  struct tm tmr, *stm;
  if (*s == '!') {  /* UTC? */
    stm = l_gmtime(&t, &tmr);
    s++;  /* skip '!' */
  }
  else
    stm = l_localtime(&t, &tmr);
  if (stm == NULL)  /* invalid date? */
    return luaL_error(L,
                 "date result cannot be represented in this installation");
  if (strcmp(s, "*t") == 0) {
    lua_createtable(L, 0, 9);  /* 9 = number of fields */
    setallfields(L, stm);
  }
  else {
    char cc[4];  /* buffer for individual conversion specifiers */
    luaL_Buffer b;
    cc[0] = '%';
    luaL_buffinit(L, &b);
    while (s < se) {
      if (*s != '%')  /* not a conversion specifier? */
        luaL_addchar(&b, *s++);
      else {
        size_t reslen;
        char *buff = luaL_prepbuffsize(&b, SIZETIMEFMT);
        s++;  /* skip '%' */
        s = checkoption(L, s, se - s, cc + 1);  /* copy specifier to 'cc' */
        reslen = strftime(buff, SIZETIMEFMT, cc, stm);
        luaL_addsize(&b, reslen);
      }
    }
    luaL_pushresult(&b);
  }
  return 1;
}


static int os_time (lua_State *L) {
  time_t t;
  if (lua_isnoneornil(L, 1))  /* called without args? */
    t = time(NULL);  /* get current time */
  else {
    struct tm ts;
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_settop(L, 1);  /* make sure table is at the top */
    ts.tm_year = getfield(L, "year", -1, 1900);
    ts.tm_mon = getfield(L, "month", -1, 1);
    ts.tm_mday = getfield(L, "day", -1, 0);
    ts.tm_hour = getfield(L, "hour", 12, 0);
    ts.tm_min = getfield(L, "min", 0, 0);
    ts.tm_sec = getfield(L, "sec", 0, 0);
    ts.tm_isdst = getboolfield(L, "isdst");
    t = mktime(&ts);
    setallfields(L, &ts);  /* update fields with normalized values */
  }
  if (t != (time_t)(l_timet)t || t == (time_t)(-1))
    return luaL_error(L,
                  "time result cannot be represented in this installation");
  l_pushtime(L, t);
  return 1;
}


static int os_difftime (lua_State *L) {
  time_t t1 = l_checktime(L, 1);
  time_t t2 = l_checktime(L, 2);
  lua_pushnumber(L, (lua_Number)difftime(t1, t2));
  return 1;
}

/* }====================================================== */


static int os_setlocale (lua_State *L) {
  static const int cat[] = {LC_ALL, LC_COLLATE, LC_CTYPE, LC_MONETARY,
                      LC_NUMERIC, LC_TIME};
  static const char *const catnames[] = {"all", "collate", "ctype", "monetary",
     "numeric", "time", NULL};
  const char *l = luaL_optstring(L, 1, NULL);
  int op = luaL_checkoption(L, 2, "all", catnames);
  lua_pushstring(L, setlocale(cat[op], l));
  return 1;
}


static int os_exit (lua_State *L) {
  int status;
  if (lua_isboolean(L, 1))
    status = (lua_toboolean(L, 1) ? EXIT_SUCCESS : EXIT_FAILURE);
  else
    status = (int)luaL_optinteger(L, 1, EXIT_SUCCESS);
  if (lua_toboolean(L, 2))
    lua_close(L);
  if (L) exit(status);  /* 'if' to avoid warnings for unreachable 'return' */
  return 0;
}

// Forward declarations and exports for Textadept's process spawning extension.
static int os_spawn(lua_State *L);
int os_spawn_pushfds(lua_State *L);
int os_spawn_readfds(lua_State *L);


static const luaL_Reg syslib[] = {
  {"clock",     os_clock},
  {"date",      os_date},
  {"difftime",  os_difftime},
  {"execute",   os_execute},
  {"exit",      os_exit},
  {"getenv",    os_getenv},
  {"remove",    os_remove},
  {"rename",    os_rename},
  {"setlocale", os_setlocale},
  {"spawn",     os_spawn},
  {"time",      os_time},
  {"tmpname",   os_tmpname},
  {NULL, NULL}
};

/* }====================================================== */



LUAMOD_API int luaopen_os (lua_State *L) {
  luaL_newlib(L, syslib);
#if (!GTK && !_WIN32 || __APPLE__)
  // Need to keep track of running processes for monitoring fds and pids.
  lua_newtable(L), lua_setfield(L, LUA_REGISTRYINDEX, "spawn_procs");
#endif
  return 1;
}


// Process spawning extension for Textadept using GLib or POSIX.
// Copyright 2012-2022 Mitchell. See LICENSE.

#include <signal.h>
//#include <stdlib.h>
//#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#if GTK
#include <glib.h>
#endif
#if !_WIN32
#if (!GTK || __APPLE__)
#include <errno.h>
#include <sys/select.h>
#endif
#include <sys/wait.h>
#include <signal.h>
#else
#include <fcntl.h>
#include <windows.h>
#endif

#if _WIN32
#define kill(pid, _) TerminateProcess(pid, 1)
#define g_io_channel_unix_new g_io_channel_win32_new_fd
#define close CloseHandle
#define FD(handle) _open_osfhandle((intptr_t)handle, _O_RDONLY)
#if !GTK
// The following macro is only for quieting compiler warnings. Spawning in Win32
// console is not supported.
#define read(fd, buf, len) read((int)fd, buf, len)
#endif
#endif

typedef struct {
  lua_State *L;
  int ref;
#if !_WIN32
  int pid, fstdin, fstdout, fstderr;
#else
  HANDLE pid, fstdin, fstdout, fstderr;
#endif
#if (GTK && !__APPLE__)
  GIOChannel *cstdout, *cstderr;
#endif
  int stdout_cb, stderr_cb, exit_cb, exit_status;
} PStream;

/** p:status() Lua function. */
static int proc_status(lua_State *L) {
  PStream *p = luaL_checkudata(L, 1, "ta_spawn");
  return (lua_pushstring(L, p->pid ? "running" : "terminated"), 1);
}

/** Process exit cleanup function. */
static void exited(PStream *p, int status) {
  lua_State *L = p->L;
  if (p->exit_cb != LUA_REFNIL) {
    // Call exit callback function with exit status.
    lua_rawgeti(L, LUA_REGISTRYINDEX, p->exit_cb);
    lua_pushinteger(L, status);
    if (lua_pcall(L, 1, 0, 0) != LUA_OK)
      fprintf(stderr, "Lua: %s\n", lua_tostring(L, -1)), lua_pop(L, 1);
  }
#if GTK && !__APPLE__
  g_source_remove_by_user_data(p); // disconnect cstdout watch
  g_source_remove_by_user_data(p); // disconnect cstderr watch
  g_source_remove_by_user_data(p); // disconnect child watch
  g_spawn_close_pid(p->pid);
#elif (!GTK || __APPLE__)
  // Stop tracking and monitoring this proc.
  lua_getfield(L, LUA_REGISTRYINDEX, "spawn_procs");
  for (lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1)) {
    PStream *monitored_p = lua_touserdata(L, -2);
    if (monitored_p->pid == p->pid) {
      lua_pushnil(L), lua_replace(L, -2), lua_settable(L, -3); // t[proc] = nil
      break;
    }
  }
  lua_pop(L, 1); // spawn_procs
#endif
  close(p->fstdin), close(p->fstdout), close(p->fstderr);
  luaL_unref(L, LUA_REGISTRYINDEX, p->stdout_cb);
  luaL_unref(L, LUA_REGISTRYINDEX, p->stderr_cb);
  luaL_unref(L, LUA_REGISTRYINDEX, p->exit_cb);
  luaL_unref(L, LUA_REGISTRYINDEX, p->ref); // allow proc to be collected
  p->pid = 0, p->exit_status = status;
}

/** p:wait() Lua function. */
static int proc_wait(lua_State *L) {
  PStream *p = luaL_checkudata(L, 1, "ta_spawn");
  if (!p->pid) return (lua_pushinteger(L, p->exit_status), 1);
#if !_WIN32
  int status;
  waitpid(p->pid, &status, 0);
  status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#else
  DWORD status;
  WaitForSingleObject(p->pid, INFINITE);
  GetExitCodeProcess(p->pid, &status);
#endif
  exited(p, status);
  return (lua_pushinteger(L, status), 1);
}

/** p:read() Lua function. */
static int proc_read(lua_State *L) {
  PStream *p = luaL_checkudata(L, 1, "ta_spawn");
  luaL_argcheck(L, p->pid, 1, "process terminated");
  char *c = (char *)luaL_optstring(L, 2, "l");
  if (*c == '*') c++; // skip optional '*' (for compatibility)
  luaL_argcheck(
    L, *c == 'l' || *c == 'L' || *c == 'a' || lua_isnumber(L, 2), 2,
    "invalid option");
#if (GTK && !__APPLE__)
  char *buf;
  size_t len;
  GError *error = NULL;
  GIOStatus status = G_IO_STATUS_NORMAL;
  if (!g_io_channel_get_buffered(p->cstdout))
    g_io_channel_set_buffered(p->cstdout, true); // needed for functions below
  if (!lua_isnumber(L, 2)) {
    if (*c == 'l' || *c == 'L') {
      GString *s = g_string_new(NULL);
      status = g_io_channel_read_line_string(p->cstdout, s, NULL, &error);
      len = s->len, buf = g_string_free(s, false);
    } else if (*c == 'a') {
      status = g_io_channel_read_to_end(p->cstdout, &buf, &len, &error);
      if (status == G_IO_STATUS_EOF) status = G_IO_STATUS_NORMAL;
    }
  } else {
    size_t bytes = (size_t)lua_tointeger(L, 2);
    buf = malloc(bytes);
    status = g_io_channel_read_chars(p->cstdout, buf, bytes, &len, &error);
  }
  if ((g_io_channel_get_buffer_condition(p->cstdout) & G_IO_IN) == 0)
    g_io_channel_set_buffered(p->cstdout, false); // needed for stdout callback
  if (*c == 'l' && buf[len - 1] == '\n') len--;
  if (*c == 'l' && buf[len - 1] == '\r') len--;
  lua_pushlstring(L, buf, len);
  free(buf);
  if (status != G_IO_STATUS_NORMAL) {
    lua_pushnil(L);
    if (status == G_IO_STATUS_EOF) return 1;
    lua_pushinteger(L, error->code);
    lua_pushstring(L, error->message);
    return 3;
  } else return 1;
#else
  int len = 0;
  if (!lua_isnumber(L, 2)) {
    luaL_Buffer buf;
    luaL_buffinit(L, &buf);
    int n;
    char ch;
    while ((n = read(p->fstdout, &ch, 1)) > 0) {
      if ((ch != '\r' && ch != '\n') || *c == 'L' || *c == 'a')
        luaL_addchar(&buf, ch), len++;
      if (ch == '\n' && *c != 'a') break;
    }
    if (n < 0 && len == 0) len = n;
    luaL_pushresult(&buf);
    if (n == 0 && len == 0 && *c != 'a') lua_pushnil(L); // EOF
  } else {
    size_t bytes = (size_t)lua_tointeger(L, 2);
    char *buf = malloc(bytes);
    if ((len = read(p->fstdout, buf, bytes)) > 0)
      lua_pushlstring(L, buf, len);
    else if (len == 0)
      lua_pushnil(L); // EOF
    free(buf);
  }
  if (len < 0) {
    lua_pushnil(L);
    lua_pushinteger(L, errno);
    lua_pushstring(L, strerror(errno));
    return 3;
  } else return 1;
#endif
}

/** p:write() Lua function. */
static int proc_write(lua_State *L) {
  PStream *p = luaL_checkudata(L, 1, "ta_spawn");
  luaL_argcheck(L, p->pid, 1, "process terminated");
  for (int i = 2; i <= lua_gettop(L); i++) {
    size_t len;
    const char *s = luaL_checklstring(L, i, &len);
#if !_WIN32
    len = write(p->fstdin, s, len); // assign result to fix compiler warning
#else
    DWORD len_written;
    WriteFile(p->fstdin, s, len, &len_written, NULL);
#endif
  }
  return 0;
}

/** p:close() Lua function. */
static int proc_close(lua_State *L) {
  PStream *p = luaL_checkudata(L, 1, "ta_spawn");
  luaL_argcheck(L, p->pid, 1, "process terminated");
  return (close(p->fstdin), 0);
}

/** p:kill() Lua function. */
static int proc_kill(lua_State *L) {
  PStream *p = luaL_checkudata(L, 1, "ta_spawn");
  if (p->pid) kill(p->pid, luaL_optinteger(L, 2, SIGKILL));
  return 0;
}

/** tostring(p) Lua function. */
static int proc_tostring(lua_State *L) {
  PStream *p = luaL_checkudata(L, 1, "ta_spawn");
  if (p->pid)
    lua_pushfstring(L, "process (pid=%d)", p->pid);
  else
    lua_pushstring(L, "process (terminated)");
  return 1;
}

#if (GTK && !__APPLE__)
/** __gc Lua metamethod. */
static int proc_gc(lua_State *L) {
  PStream *p = luaL_checkudata(L, 1, "ta_spawn");
  if (p->pid) {
    // lua_close() was called, forcing GC. Disconnect listeners since GTK is
    // still running and may try to invoke callbacks.
    g_source_remove_by_user_data(p); // disconnect cstdout watch
    g_source_remove_by_user_data(p); // disconnect cstderr watch
    g_source_remove_by_user_data(p); // disconnect child watch
    g_spawn_close_pid(p->pid);
  }
  return 0;
}

/** Signal that channel output is available for reading. */
static int ch_read(GIOChannel *source, GIOCondition cond, void *data) {
  PStream *p = data;
  if (!p->pid || !(cond & G_IO_IN)) return false;
  char buf[BUFSIZ];
  size_t len = 0;
  do {
    int status = g_io_channel_read_chars(source, buf, BUFSIZ, &len, NULL);
    int r = (source == p->cstdout) ? p->stdout_cb : p->stderr_cb;
    if (status == G_IO_STATUS_NORMAL && len > 0 && r > 0) {
      lua_rawgeti(p->L, LUA_REGISTRYINDEX, r);
      lua_pushlstring(p->L, buf, len);
      if (lua_pcall(p->L, 1, 0, 0) != LUA_OK)
        fprintf(stderr, "Lua: %s\n", lua_tostring(p->L, -1)), lua_pop(p->L, 1);
    }
  } while (len == BUFSIZ);
  return p->pid && !(cond & G_IO_HUP);
}

/**
 * Creates a new channel that monitors a file descriptor for output.
 * @param fd File descriptor returned by `g_spawn_async_with_pipes()` or
 *   `_open_osfhandle()`.
 * @param p PStream to notify when output is available for reading.
 * @param watch Whether or not to watch for output to send to a Lua callback.
 */
static GIOChannel *new_channel(int fd, PStream *p, bool watch) {
  GIOChannel *channel = g_io_channel_unix_new(fd);
  g_io_channel_set_encoding(channel, NULL, NULL);
  g_io_channel_set_buffered(channel, false);
  if (watch) {
    g_io_add_watch(channel, G_IO_IN | G_IO_HUP, ch_read, p);
    g_io_channel_unref(channel);
  }
  return channel;
}

/** Signal that the child process finished. */
static void proc_exited(GPid pid, int status, void *data) {
  exited(data, status);
}
#elif !_WIN32
/**
 * Pushes onto the stack an fd_set of all spawned processes for use with
 * `select()` and `os_spawn_readfds()` and returns the `nfds` to pass to
 * `select()`.
 */
int os_spawn_pushfds(lua_State *L) {
  int nfds = 1;
  fd_set *fds = lua_newuserdata(L, sizeof(fd_set));
  FD_ZERO(fds);
  lua_getfield(L, LUA_REGISTRYINDEX, "spawn_procs");
  for (lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1)) {
    PStream *p = lua_touserdata(L, -2);
    FD_SET(p->fstdout, fds);
    FD_SET(p->fstderr, fds);
    if (p->fstdout >= nfds) nfds = p->fstdout + 1;
    if (p->fstderr >= nfds) nfds = p->fstderr + 1;
  }
  lua_pop(L, 1); // spawn_procs
  return nfds;
}

/** Signal that a fd has output to read. */
static void fd_read(int fd, PStream *p) {
  char buf[BUFSIZ];
  ssize_t len;
  do {
    len = read(fd, buf, BUFSIZ);
    int r = (fd == p->fstdout) ? p->stdout_cb : p->stderr_cb;
    if (len > 0 && r > 0) {
      lua_rawgeti(p->L, LUA_REGISTRYINDEX, r);
      lua_pushlstring(p->L, buf, len);
      if (lua_pcall(p->L, 1, 0, 0) != LUA_OK)
        fprintf(stderr, "Lua: %s\n", lua_tostring(p->L, -1)), lua_pop(p->L, 1);
    }
  } while (len == BUFSIZ);
}

/**
 * Reads any output from the fds in the fd_set at the top of the stack and
 * returns the number of fds read from.
 * Also signals any registered child processes that have finished and cleans up
 * after them.
 */
int os_spawn_readfds(lua_State *L) {
  int n = 0;
  fd_set *fds = lua_touserdata(L, -1);
  lua_getfield(L, LUA_REGISTRYINDEX, "spawn_procs");
  for (lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1)) {
    PStream *p = lua_touserdata(L, -2);
    // Read output if any is available.
    if (FD_ISSET(p->fstdout, fds)) fd_read(p->fstdout, p), n++;
    if (FD_ISSET(p->fstderr, fds)) fd_read(p->fstderr, p), n++;
    // Check process status.
    int status;
    if (waitpid(p->pid, &status, WNOHANG) > 0) {
      fd_read(p->fstdout, p), fd_read(p->fstderr, p); // read anything left
      exited(p, status);
    }
  }
  lua_pop(L, 1); // spawn_procs
  return n;
}

#if (GTK && __APPLE__)
static int monitoring_fds = 0;
/**
 * Monitors spawned fds when GTK is idle.
 * This is necessary because at the moment, using GLib on macOS to spawn and
 * monitor file descriptors mostly blocks when attempting to poll those fds.
 * Note that this idle event is considered a pending event, so the construct
 * `while (gtk_events_pending()) gtk_main_iteration();` will cycle for as long
 * as the monitor is active. To help get around this, this function sets a
 * "spawn_procs_polled" boolean in the registry after poll. An application can
 * set this boolean to `false` prior to calling `gtk_main_iteration()`. If there
 * are still pending events and this boolean is `true`, then there are no
 * non-idle pending events left.
 */
static int monitor_fds(void *L) {
  struct timeval timeout = {0, 1e5}; // 0.1s
  int nfds = os_spawn_pushfds(L);
  if (select(nfds, lua_touserdata(L, -1), NULL, NULL, &timeout) > 0)
    os_spawn_readfds(L);
  lua_pop(L, 1); // fds
  if (nfds == 1) monitoring_fds = 0;
  lua_pushboolean(L, 1);
  lua_setfield(L, LUA_REGISTRYINDEX, "spawn_procs_polled");
  return nfds > 1;
}
#endif
#endif

/** spawn() Lua function. */
static int os_spawn(lua_State *L) {
  int narg = 1;
  // Determine process parameters (argv, cwd, envp).
#if !_WIN32
  // Construct argv from first string param.
#if (GTK && !__APPLE__)
  char **argv = NULL;
  GError *error = NULL;
  if (!g_shell_parse_argv(luaL_checkstring(L, narg++), NULL, &argv, &error)) {
    lua_pushfstring(L, "invalid argv: %s", error->message);
    luaL_argerror(L, 1, lua_tostring(L, -1));
  }
#else
  lua_newtable(L);
  const char *param = luaL_checkstring(L, narg++), *c = param;
  while (*c) {
    while (*c == ' ') c++;
    param = c;
    if (*c == '"') {
      param = ++c;
      while (*c && (*c != '"' || *(c - 1) == '\\')) c++;
    } else while (*c && *c != ' ') c++;
    lua_pushlstring(L, param, c - param);
    lua_rawseti(L, -2, lua_rawlen(L, -2) + 1);
    if (*c == '"') c++;
  }
  int argc = lua_rawlen(L, -1);
  char **argv = calloc(argc + 1, sizeof(char *));
  for (int i = 0; i < argc; i++) {
    lua_rawgeti(L, -1, i + 1);
    argv[i] = strcpy(malloc(lua_rawlen(L, -1) + 1), lua_tostring(L, -1));
    lua_pop(L, 1); // param
  }
  lua_pop(L, 1); // argv
#endif
  // Determine cwd from optional second string param.
  const char *cwd = lua_isstring(L, narg) ? lua_tostring(L, narg++) : NULL;
  // Construct environment from optional third table param.
  int envn = 0;
  char **envp = NULL;
  if (lua_istable(L, narg)) {
    for (lua_pushnil(L); lua_next(L, narg); lua_pop(L, 1)) envn++;
    envp = calloc(envn + 1, sizeof(char *));
    int i = 0;
    for (lua_pushnil(L); lua_next(L, narg); lua_pop(L, 1)) {
      if (!lua_isstring(L, -2) || !lua_isstring(L, -1)) continue;
      if (lua_type(L, -2) == LUA_TSTRING) {
        lua_pushvalue(L, -2), lua_pushliteral(L, "="), lua_pushvalue(L, -3),
          lua_concat(L, 3), lua_replace(L, -2); // construct "KEY=VALUE"
      }
      envp[i++] = strcpy(malloc(lua_rawlen(L, -1) + 1), lua_tostring(L, -1));
    }
    narg++;
  }
#else
  // Construct argv from first string param.
  lua_pushstring(L, getenv("COMSPEC")), lua_pushstring(L, " /c "),
    lua_pushvalue(L, 1), lua_concat(L, 3), lua_replace(L, 1);
  wchar_t argv[2048] = {L'\0'};
  MultiByteToWideChar(
    GetACP(), 0, lua_tostring(L, narg++), -1, (LPWSTR)&argv, sizeof(argv));
  // Determine cwd from optional second string param.
  wchar_t cwd[MAX_PATH] = {L'\0'};
  if (lua_isstring(L, narg))
    MultiByteToWideChar(
      GetACP(), 0, lua_tostring(L, narg++), -1, (LPWSTR)&cwd, MAX_PATH);
  // Construct environment from optional third table param.
  char *envp = NULL;
  if (lua_istable(L, narg)) {
    luaL_Buffer buf;
    luaL_buffinit(L, &buf);
    for (lua_pushnil(L); lua_next(L, narg); lua_pop(L, 1)) {
      if (!lua_isstring(L, -2) || !lua_isstring(L, -1)) continue;
      if (lua_type(L, -2) == LUA_TSTRING) {
        lua_pushvalue(L, -2), lua_pushliteral(L, "="), lua_pushvalue(L, -3),
          lua_concat(L, 3), lua_replace(L, -2); // construct "KEY=VALUE"
      }
      luaL_addstring(&buf, lua_tostring(L, -1)), luaL_addchar(&buf, '\0');
    }
    luaL_addchar(&buf, '\0');
    luaL_pushresult(&buf);
    envp = malloc(lua_rawlen(L, -1) * sizeof(char));
    memcpy(envp, lua_tostring(L, -1), lua_rawlen(L, -1));
    lua_pop(L, 1); // buf
    narg++;
  }
#endif
  lua_settop(L, 6); // ensure 6 values so userdata to be pushed is 7th

  // Create process object to be returned and link callback functions from
  // optional fourth, fifth, and sixth function params.
  PStream *p = lua_newuserdata(L, sizeof(PStream));
  p->L = L, p->ref = 0;
  for (int i = narg; i < narg + 3; i++)
    luaL_argcheck(
      L, lua_isfunction(L, i) || lua_isnoneornil(L, i), i,
      "function or nil expected");
  p->stdout_cb = (lua_pushvalue(L, narg++), luaL_ref(L, LUA_REGISTRYINDEX));
  p->stderr_cb = (lua_pushvalue(L, narg++), luaL_ref(L, LUA_REGISTRYINDEX));
  p->exit_cb = (lua_pushvalue(L, narg++), luaL_ref(L, LUA_REGISTRYINDEX));
  if (luaL_newmetatable(L, "ta_spawn")) {
    lua_pushcfunction(L, proc_status), lua_setfield(L, -2, "status");
    lua_pushcfunction(L, proc_wait), lua_setfield(L, -2, "wait");
    lua_pushcfunction(L, proc_read), lua_setfield(L, -2, "read");
    lua_pushcfunction(L, proc_write), lua_setfield(L, -2, "write");
    lua_pushcfunction(L, proc_close), lua_setfield(L, -2, "close");
    lua_pushcfunction(L, proc_kill), lua_setfield(L, -2, "kill");
    lua_pushcfunction(L, proc_tostring), lua_setfield(L, -2, "__tostring");
#if (GTK && !__APPLE__)
    lua_pushcfunction(L, proc_gc), lua_setfield(L, -2, "__gc");
#endif
    lua_pushvalue(L, -1), lua_setfield(L, -2, "__index");
  }
  lua_setmetatable(L, -2);

  // Spawn the process, connecting to stdin, stdout, stderr, and exit.
#if !_WIN32
#if (GTK && !__APPLE__)
  GSpawnFlags flags = G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH;
  if (g_spawn_async_with_pipes(
        cwd, argv, envp, flags, NULL, NULL, &p->pid, &p->fstdin, &p->fstdout,
        &p->fstderr, &error)) {
    p->cstdout = new_channel(p->fstdout, p, p->stdout_cb > 0);
    p->cstderr = new_channel(p->fstderr, p, p->stderr_cb > 0);
    g_child_watch_add(p->pid, proc_exited, p);
    lua_pushnil(L); // no error
  } else {
    lua_pushnil(L);
    lua_pushfstring(L, "%s: %s", lua_tostring(L, 1), error->message);
  }

  g_strfreev(argv), g_strfreev(envp);
#else
  // Adapted from Chris Emerson and GLib.
  // Attempt to create pipes for stdin, stdout, and stderr and fork process.
  int pstdin[2] = {-1, -1}, pstdout[2] = {-1, -1}, pstderr[2] = {-1, -1}, pid;
  if (pipe(pstdin) == 0 && pipe(pstdout) == 0 && pipe(pstderr) == 0 &&
      (pid = fork()) >= 0) {
    if (pid > 0) {
      // Parent process: register child for monitoring its fds and pid.
      close(pstdin[0]), close(pstdout[1]), close(pstderr[1]);
      p->pid = pid;
      p->fstdin = pstdin[1], p->fstdout = pstdout[0], p->fstderr = pstderr[0];
      lua_getfield(L, LUA_REGISTRYINDEX, "spawn_procs");
      // spawn_procs is of the form: t[proc] = true
      lua_pushvalue(L, -2), lua_pushboolean(L, 1), lua_settable(L, -3);
      lua_pop(L, 1); // spawn_procs
      lua_pushnil(L); // no error
#if (GTK && __APPLE__)
      // On GTK-OSX, manually monitoring spawned fds prevents the fd polling
      // aborts caused by GLib.
      if (!monitoring_fds) g_idle_add(monitor_fds, L), monitoring_fds = 1;
#endif
    } else if (pid == 0) {
      // Child process: redirect stdin, stdout, and stderr, chdir, and exec.
      close(pstdin[1]), close(pstdout[0]), close(pstderr[0]);
      close(0), close(1), close(2);
      dup2(pstdin[0], 0), dup2(pstdout[1], 1), dup2(pstderr[1], 2);
      close(pstdin[0]), close(pstdout[1]), close(pstderr[1]);
      if (cwd && chdir(cwd) < 0) {
        fprintf(
          stderr, "Failed to change directory '%s' (%s)", cwd, strerror(errno));
        exit(EXIT_FAILURE);
      }
      extern char **environ;
#if __linux__
      if (!envp) envp = environ;
      execvpe(argv[0], argv, envp); // does not return on success
#else
      if (envp) environ = envp;
      execvp(argv[0], argv); // does not return on success
#endif
      fprintf(
        stderr, "Failed to execute child process \"%s\" (%s)", argv[0],
        strerror(errno));
      exit(EXIT_FAILURE);
    }
  } else {
    if (pstdin[0] >= 0) close(pstdin[0]), close(pstdin[1]);
    if (pstdout[0] >= 0) close(pstdout[0]), close(pstdout[1]);
    if (pstderr[0] >= 0) close(pstderr[0]), close(pstderr[1]);
    lua_pushnil(L);
    lua_pushfstring(L, "%s: %s", lua_tostring(L, 1), strerror(errno));
  }
  for (int i = 0; i < argc; i++) free(argv[i]);
  free(argv);
  if (envp) {
    for (int i = 0; i < envn; i++) free(envp[i]);
    free(envp);
  }
#endif
#else
#if GTK
  // Adapted from SciTE.
  SECURITY_DESCRIPTOR sd;
  InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
  SetSecurityDescriptorDacl(&sd, true, NULL, false);
  SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), 0, 0};
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.lpSecurityDescriptor = &sd;
  sa.bInheritHandle = true;

  // Redirect stdin.
  HANDLE stdin_read = NULL, proc_stdin = NULL;
  CreatePipe(&stdin_read, &proc_stdin, &sa, 0);
  SetHandleInformation(proc_stdin, HANDLE_FLAG_INHERIT, 0);
  // Redirect stdout.
  HANDLE proc_stdout = NULL, stdout_write = NULL;
  CreatePipe(&proc_stdout, &stdout_write, &sa, 0);
  SetHandleInformation(proc_stdout, HANDLE_FLAG_INHERIT, 0);
  // Redirect stderr.
  HANDLE proc_stderr = NULL, stderr_write = NULL;
  CreatePipe(&proc_stderr, &stderr_write, &sa, 0);
  SetHandleInformation(proc_stderr, HANDLE_FLAG_INHERIT, 0);

  // Spawn with pipes and no window.
  // TODO: CREATE_UNICODE_ENVIRONMENT?
  STARTUPINFOW startup_info = {
    sizeof(STARTUPINFOW), NULL, NULL, NULL, 0, 0, 0, 0, 0, 0, 0,
    STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES, SW_HIDE, 0, 0, stdin_read,
    stdout_write, stderr_write
  };
  PROCESS_INFORMATION proc_info = {0, 0, 0, 0};
  if (CreateProcessW(
        NULL, argv, NULL, NULL, true, CREATE_NEW_PROCESS_GROUP, envp,
        *cwd ? cwd : NULL, &startup_info, &proc_info)) {
    p->pid = proc_info.hProcess;
    p->fstdin = proc_stdin, p->fstdout = proc_stdout, p->fstderr = proc_stderr;
    p->cstdout = new_channel(FD(proc_stdout), p, p->stdout_cb > 0);
    p->cstderr = new_channel(FD(proc_stderr), p, p->stderr_cb > 0);
    g_child_watch_add(p->pid, proc_exited, p);
    // Close unneeded handles.
    CloseHandle(proc_info.hThread);
    CloseHandle(stdin_read);
    CloseHandle(stdout_write), CloseHandle(stderr_write);
    lua_pushnil(L); // no error
  } else {
    char *message = NULL;
    FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
      FORMAT_MESSAGE_IGNORE_INSERTS, NULL, GetLastError(),
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&message, 0, NULL);
    lua_pushnil(L);
    lua_pushfstring(L, "%s: %s", lua_tostring(L, 1), message);
    LocalFree(message);
  }
  if (envp) free(envp);
#else
  luaL_error(L, "not implemented in this environment");
#endif
#endif
  if (lua_isuserdata(L, -2))
    p->ref = (lua_pushvalue(L, -2), luaL_ref(L, LUA_REGISTRYINDEX));

  return 2;
}
