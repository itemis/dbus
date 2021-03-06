/* Utility functions for tests that rely on GLib
 *
 * Copyright © 2010-2011 Nokia Corporation
 * Copyright © 2013-2015 Collabora Ltd.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <config.h>
#include "test-utils-glib.h"

#include <errno.h>
#include <string.h>

#ifdef DBUS_WIN
# include <io.h>
# include <windows.h>
#else
# include <signal.h>
# include <unistd.h>
# include <sys/types.h>
# include <pwd.h>
#endif

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <dbus/dbus.h>

#ifdef G_OS_WIN
# define isatty(x) _isatty(x)
#endif

void
_test_assert_no_error (const DBusError *e,
    const char *file,
    int line)
{
  if (G_UNLIKELY (dbus_error_is_set (e)))
    g_error ("%s:%d: expected success but got error: %s: %s",
        file, line, e->name, e->message);
}

#ifdef DBUS_UNIX
static void
child_setup (gpointer user_data)
{
  const struct passwd *pwd = user_data;
  uid_t uid = geteuid ();

  if (pwd == NULL || (pwd->pw_uid == uid && getuid () == uid))
    return;

  if (uid != 0)
    g_error ("not currently euid 0: %lu", (unsigned long) uid);

  if (setuid (pwd->pw_uid) != 0)
    g_error ("could not setuid (%lu): %s",
        (unsigned long) pwd->pw_uid, g_strerror (errno));

  uid = getuid ();

  if (uid != pwd->pw_uid)
    g_error ("after successful setuid (%lu) my uid is %ld",
        (unsigned long) pwd->pw_uid, (unsigned long) uid);

  uid = geteuid ();

  if (uid != pwd->pw_uid)
    g_error ("after successful setuid (%lu) my euid is %ld",
        (unsigned long) pwd->pw_uid, (unsigned long) uid);
}
#endif

static gchar *
spawn_dbus_daemon (const gchar *binary,
    const gchar *configuration,
    const gchar *listen_address,
    TestUser user,
    const gchar *runtime_dir,
    GPid *daemon_pid)
{
  GError *error = NULL;
  GString *address;
  gint address_fd;
  GPtrArray *argv;
  gchar **envp;
#ifdef DBUS_UNIX
  const struct passwd *pwd = NULL;
#endif

  if (user != TEST_USER_ME)
    {
#ifdef DBUS_UNIX
      if (getuid () != 0)
        {
          g_test_skip ("cannot use alternative uid when not uid 0");
          return NULL;
        }

      switch (user)
        {
          case TEST_USER_ROOT:
            break;

          case TEST_USER_MESSAGEBUS:
            pwd = getpwnam (DBUS_USER);

            if (pwd == NULL)
              {
                gchar *message = g_strdup_printf ("user '%s' does not exist",
                    DBUS_USER);

                g_test_skip (message);
                g_free (message);
                return NULL;
              }

            break;

          case TEST_USER_OTHER:
            pwd = getpwnam (DBUS_TEST_USER);

            if (pwd == NULL)
              {
                gchar *message = g_strdup_printf ("user '%s' does not exist",
                    DBUS_TEST_USER);

                g_test_skip (message);
                g_free (message);
                return NULL;
              }

            break;

          case TEST_USER_ME:
            /* cannot get here, fall through */
          default:
            g_assert_not_reached ();
        }
#else
      g_test_skip ("cannot use alternative uid on Windows");
      return NULL;
#endif
    }

  envp = g_get_environ ();

  if (runtime_dir != NULL)
    envp = g_environ_setenv (envp, "XDG_RUNTIME_DIR", runtime_dir, TRUE);

  argv = g_ptr_array_new_with_free_func (g_free);
  g_ptr_array_add (argv, g_strdup (binary));
  g_ptr_array_add (argv, g_strdup (configuration));
  g_ptr_array_add (argv, g_strdup ("--nofork"));
  g_ptr_array_add (argv, g_strdup ("--print-address=1")); /* stdout */

  if (listen_address != NULL)
    g_ptr_array_add (argv, g_strdup (listen_address));

#ifdef DBUS_UNIX
  g_ptr_array_add (argv, g_strdup ("--systemd-activation"));
#endif

  g_ptr_array_add (argv, NULL);

  g_spawn_async_with_pipes (NULL, /* working directory */
      (gchar **) argv->pdata,
      envp,
      G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
#ifdef DBUS_UNIX
      child_setup, (gpointer) pwd,
#else
      NULL, NULL,
#endif
      daemon_pid,
      NULL, /* child's stdin = /dev/null */
      &address_fd,
      NULL, /* child's stderr = our stderr */
      &error);
  g_assert_no_error (error);

  g_ptr_array_free (argv, TRUE);
  g_strfreev (envp);

  address = g_string_new (NULL);

  /* polling until the dbus-daemon writes out its address is a bit stupid,
   * but at least it's simple, unlike dbus-launch... in principle we could
   * use select() here, but life's too short */
  while (1)
    {
      gssize bytes;
      gchar buf[4096];
      gchar *newline;

      bytes = read (address_fd, buf, sizeof (buf));

      if (bytes > 0)
        g_string_append_len (address, buf, bytes);

      newline = strchr (address->str, '\n');

      if (newline != NULL)
        {
          if ((newline > address->str) && ('\r' == newline[-1]))
            newline -= 1;
          g_string_truncate (address, newline - address->str);
          break;
        }

      g_usleep (G_USEC_PER_SEC / 10);
    }

  g_close (address_fd, NULL);

  return g_string_free (address, FALSE);
}

gchar *
test_get_dbus_daemon (const gchar *config_file,
                      TestUser     user,
                      const gchar *runtime_dir,
                      GPid        *daemon_pid)
{
  gchar *dbus_daemon;
  gchar *arg;
  const gchar *listen_address = NULL;
  gchar *address;

  /* we often have to override this because on Windows, the default may be
   * autolaunch:, which is globally-scoped and hence unsuitable for
   * regression tests */
  listen_address = "--address=" TEST_LISTEN;

  if (config_file != NULL)
    {

      if (g_getenv ("DBUS_TEST_DATA") == NULL)
        {
          g_test_message ("set DBUS_TEST_DATA to a directory containing %s",
              config_file);
          g_test_skip ("DBUS_TEST_DATA not set");
          return NULL;
        }

      arg = g_strdup_printf (
          "--config-file=%s/%s",
          g_getenv ("DBUS_TEST_DATA"), config_file);

      /* The configuration file is expected to give a suitable address,
       * do not override it */
      listen_address = NULL;
    }
  else if (g_getenv ("DBUS_TEST_DATADIR") != NULL)
    {
      arg = g_strdup_printf ("--config-file=%s/dbus-1/session.conf",
          g_getenv ("DBUS_TEST_DATADIR"));
    }
  else if (g_getenv ("DBUS_TEST_DATA") != NULL)
    {
      arg = g_strdup_printf (
          "--config-file=%s/valid-config-files/session.conf",
          g_getenv ("DBUS_TEST_DATA"));
    }
  else
    {
      arg = g_strdup ("--session");
    }

  dbus_daemon = g_strdup (g_getenv ("DBUS_TEST_DAEMON"));

  if (dbus_daemon == NULL)
    dbus_daemon = g_strdup ("dbus-daemon");

  if (g_getenv ("DBUS_TEST_DAEMON_ADDRESS") != NULL)
    {
      if (config_file != NULL || user != TEST_USER_ME)
        {
          g_test_skip ("cannot use DBUS_TEST_DAEMON_ADDRESS for "
              "unusally-configured dbus-daemon");
          address = NULL;
        }
      else
        {
          address = g_strdup (g_getenv ("DBUS_TEST_DAEMON_ADDRESS"));
        }
    }
  else
    {
      address = spawn_dbus_daemon (dbus_daemon, arg,
          listen_address, user, runtime_dir, daemon_pid);
    }

  g_free (dbus_daemon);
  g_free (arg);
  return address;
}

DBusConnection *
test_connect_to_bus (TestMainContext *ctx,
    const gchar *address)
{
  GError *error = NULL;
  DBusConnection *conn = test_try_connect_to_bus (ctx, address, &error);

  g_assert_no_error (error);
  g_assert (conn != NULL);
  return conn;
}

DBusConnection *
test_try_connect_to_bus (TestMainContext *ctx,
    const gchar *address,
    GError **gerror)
{
  DBusConnection *conn;
  DBusError error = DBUS_ERROR_INIT;

  conn = dbus_connection_open_private (address, &error);

  if (conn == NULL)
    goto fail;

  if (!dbus_bus_register (conn, &error))
    goto fail;

  g_assert (dbus_bus_get_unique_name (conn) != NULL);

  if (!test_connection_try_setup (ctx, conn))
    {
      _DBUS_SET_OOM (&error);
      goto fail;
    }

  return conn;

fail:
  if (gerror != NULL)
    *gerror = g_dbus_error_new_for_dbus_error (error.name, error.message);

  if (conn != NULL)
    {
      dbus_connection_close (conn);
      dbus_connection_unref (conn);
    }

  dbus_error_free (&error);
  return FALSE;
}

static gboolean
become_other_user (TestUser user,
                   GError **error)
{
  /* For now we only do tests like this on Linux, because I don't know how
   * safe this use of setresuid() is on other platforms */
#if defined(HAVE_GETRESUID) && defined(HAVE_SETRESUID) && defined(__linux__)
  uid_t ruid, euid, suid;
  const struct passwd *pwd;
  const char *username;

  g_return_val_if_fail (user != TEST_USER_ME, FALSE);

  switch (user)
    {
      case TEST_USER_ROOT:
        username = "root";
        break;

      case TEST_USER_MESSAGEBUS:
        username = DBUS_USER;
        break;

      case TEST_USER_OTHER:
        username = DBUS_TEST_USER;
        break;

      case TEST_USER_ME:
      default:
        g_return_val_if_reached (FALSE);
    }

  if (getresuid (&ruid, &euid, &suid) != 0)
    g_error ("getresuid: %s", g_strerror (errno));

  if (ruid != 0 || euid != 0 || suid != 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
          "not uid 0 (ruid=%ld euid=%ld suid=%ld)",
          (unsigned long) ruid, (unsigned long) euid, (unsigned long) suid);
      return FALSE;
    }

  pwd = getpwnam (username);

  if (pwd == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
          "getpwnam(\"%s\"): %s", username, g_strerror (errno));
      return FALSE;
    }

  /* Impersonate the desired user while we connect to the bus.
   * This should work, because we're root; so if it fails, we just crash. */
  if (setresuid (pwd->pw_uid, pwd->pw_uid, 0) != 0)
    g_error ("setresuid(%ld, (same), 0): %s",
        (unsigned long) pwd->pw_uid, g_strerror (errno));

  return TRUE;

#else
  g_return_val_if_fail (user != TEST_USER_ME, FALSE);

  switch (user)
    {
      case TEST_USER_ROOT:
      case TEST_USER_MESSAGEBUS:
      case TEST_USER_OTHER:
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
            "setresuid() not available, or unsure about "
            "credentials-passing semantics on this platform");
        return FALSE;

      case TEST_USER_ME:
      default:
        g_return_val_if_reached (FALSE);
    }

#endif
}

/* Undo the effect of a successful call to become_other_user() */
static void
back_to_root (void)
{
#if defined(HAVE_GETRESUID) && defined(HAVE_SETRESUID) && defined(__linux__)
  if (setresuid (0, 0, 0) != 0)
    g_error ("setresuid(0, 0, 0): %s", g_strerror (errno));
#else
  g_error ("become_other_user() cannot succeed on this platform");
#endif
}

/*
 * Raise G_IO_ERROR_NOT_SUPPORTED if the requested user is impossible.
 * Do not mark the test as skipped: we might have more to test anyway.
 */
DBusConnection *
test_try_connect_to_bus_as_user (TestMainContext *ctx,
    const char *address,
    TestUser user,
    GError **error)
{
  DBusConnection *conn;

  if (user != TEST_USER_ME && !become_other_user (user, error))
    return NULL;

  conn = test_try_connect_to_bus (ctx, address, error);

  if (user != TEST_USER_ME)
    back_to_root ();

  return conn;
}

/*
 * Raise G_IO_ERROR_NOT_SUPPORTED if the requested user is impossible.
 */
GDBusConnection *
test_try_connect_gdbus_as_user (const char *address,
                                TestUser user,
                                GError **error)
{
  GDBusConnection *conn;

  if (user != TEST_USER_ME && !become_other_user (user, error))
    return NULL;

  conn = g_dbus_connection_new_for_address_sync (address,
      (G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION |
       G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT),
      NULL, NULL, error);

  if (user != TEST_USER_ME)
    back_to_root ();

  return conn;
}

static void
pid_died (GPid pid,
          gint status,
          gpointer user_data)
{
  gboolean *result = user_data;

  g_assert (result != NULL);
  g_assert (!*result);
  *result = TRUE;
}

void
test_kill_pid (GPid pid)
{
  gint died = FALSE;

  g_child_watch_add (pid, pid_died, &died);

#ifdef DBUS_WIN
  if (pid != NULL)
    TerminateProcess (pid, 1);
#else
  if (pid > 0)
    kill (pid, SIGTERM);
#endif

  while (!died)
    g_main_context_iteration (NULL, TRUE);
}

static gboolean
time_out (gpointer data)
{
  puts ("Bail out! Test timed out (GLib main loop timeout callback reached)");
  fflush (stdout);
  abort ();
  return FALSE;
}

#ifdef G_OS_UNIX
static void wrap_abort (int signal) _DBUS_GNUC_NORETURN;

static void
wrap_abort (int signal)
{
  /* We might be halfway through writing out something else, so force this
   * onto its own line */
  const char message [] = "\nBail out! Test timed out (SIGALRM received)\n";

  if (write (STDOUT_FILENO, message, sizeof (message) - 1) <
      (ssize_t) sizeof (message) - 1)
    {
      /* ignore short write - what would we do about it? */
    }

  abort ();
}
#endif

static void
set_timeout (guint factor)
{
  static guint timeout = 0;

  /* Prevent tests from hanging forever. This is intended to be long enough
   * that any reasonable regression test on any reasonable hardware would
   * have finished. */
#define TIMEOUT 60

  if (timeout != 0)
    g_source_remove (timeout);

  timeout = g_timeout_add_seconds (TIMEOUT * factor, time_out, NULL);
#ifdef G_OS_UNIX
  /* The GLib main loop might not be running (we don't use it in every
   * test). Die with SIGALRM shortly after if necessary. */
  alarm ((TIMEOUT * factor) + 10);

  /* Get a log message and a core dump from the SIGALRM. */
    {
      struct sigaction act = { };

      act.sa_handler = wrap_abort;

      sigaction (SIGALRM, &act, NULL);
    }
#endif
}

void
test_init (int *argcp, char ***argvp)
{
  g_test_init (argcp, argvp, NULL);
  g_test_bug_base ("https://bugs.freedesktop.org/show_bug.cgi?id=");
  set_timeout (1);
}

static void
report_and_destroy (gpointer p)
{
  GTimer *timer = p;

  g_test_message ("Time since timeout reset %p: %.3f seconds",
      timer, g_timer_elapsed (timer, NULL));
  g_timer_destroy (timer);
}

void
test_timeout_reset (guint factor)
{
  GTimer *timer = g_timer_new ();

  g_test_message ("Resetting test timeout (reference: %p; factor: %u)",
      timer, factor);
  set_timeout (factor);

  g_test_queue_destroy (report_and_destroy, timer);
}

void
test_progress (char symbol)
{
  if (g_test_verbose () && isatty (1))
    g_print ("%c", symbol);
}

/*
 * Delete @path, with a retry loop if the system call is interrupted by
 * an async signal. If @path does not exist, ignore; otherwise, it is
 * required to be a non-directory.
 */
void
test_remove_if_exists (const gchar *path)
{
  while (g_remove (path) != 0)
    {
      int saved_errno = errno;

      if (saved_errno == ENOENT)
        return;

#ifdef G_OS_UNIX
      if (saved_errno == EINTR)
        continue;
#endif

      g_error ("Unable to remove file \"%s\": %s", path,
               g_strerror (saved_errno));
    }
}

/*
 * Delete empty directory @path, with a retry loop if the system call is
 * interrupted by an async signal. @path is required to exist.
 */
void
test_rmdir_must_exist (const gchar *path)
{
  while (g_remove (path) != 0)
    {
      int saved_errno = errno;

#ifdef G_OS_UNIX
      if (saved_errno == EINTR)
        continue;
#endif

      g_error ("Unable to remove directory \"%s\": %s", path,
               g_strerror (saved_errno));
    }
}

/*
 * Delete empty directory @path, with a retry loop if the system call is
 * interrupted by an async signal. If @path does not exist, ignore.
 */
void
test_rmdir_if_exists (const gchar *path)
{
  while (g_remove (path) != 0)
    {
      int saved_errno = errno;

      if (saved_errno == ENOENT)
        return;

#ifdef G_OS_UNIX
      if (saved_errno == EINTR)
        continue;
#endif

      g_error ("Unable to remove directory \"%s\": %s", path,
               g_strerror (saved_errno));
    }
}

/*
 * Create directory @path, with a retry loop if the system call is
 * interrupted by an async signal.
 */
void
test_mkdir (const gchar *path,
            gint mode)
{
  while (g_mkdir (path, mode) != 0)
    {
      int saved_errno = errno;

#ifdef G_OS_UNIX
      if (saved_errno == EINTR)
        continue;
#endif

      g_error ("Unable to create directory \"%s\": %s", path,
               g_strerror (saved_errno));
    }
}

void
test_oom (void)
{
  g_error ("Out of memory");
}

/*
 * Send the given method call and wait for a reply, spinning the main
 * context as necessary.
 */
DBusMessage *
test_main_context_call_and_wait (TestMainContext *ctx,
    DBusConnection *connection,
    DBusMessage *call,
    int timeout)
{
  DBusPendingCall *pc = NULL;
  DBusMessage *reply = NULL;

  if (!dbus_connection_send_with_reply (connection, call, &pc, timeout) ||
      pc == NULL)
    test_oom ();

  if (dbus_pending_call_get_completed (pc))
    test_pending_call_store_reply (pc, &reply);
  else if (!dbus_pending_call_set_notify (pc, test_pending_call_store_reply,
        &reply, NULL))
    test_oom ();

  while (reply == NULL)
    test_main_context_iterate (ctx, TRUE);

  dbus_clear_pending_call (&pc);
  return g_steal_pointer (&reply);
}
