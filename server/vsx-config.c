/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2019, 2021  Neil Roberts
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

#include "config.h"

#include "vsx-config.h"

#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "vsx-key-value.h"
#include "vsx-util.h"
#include "vsx-buffer.h"
#include "vsx-file-error.h"

typedef struct
{
  const char *filename;
  VsxConfig *config;
  bool had_error;
  struct vsx_buffer error_buffer;
  VsxConfigServer *server;
} LoadConfigData;

struct vsx_error_domain
vsx_config_error;

VSX_PRINTF_FORMAT (2, 3)
static void
load_config_error (LoadConfigData *data, const char *format, ...)
{
  data->had_error = true;

  if (data->error_buffer.length > 0)
    vsx_buffer_append_c (&data->error_buffer, '\n');

  vsx_buffer_append_printf (&data->error_buffer, "%s: ", data->filename);

  va_list ap;

  va_start (ap, format);
  vsx_buffer_append_vprintf (&data->error_buffer, format, ap);
  va_end (ap);
}

static void
load_config_error_func (const char *message, void *user_data)
{
  LoadConfigData *data = user_data;
  load_config_error (data, "%s", message);
}

typedef enum
{
  OPTION_TYPE_STRING,
  OPTION_TYPE_INT,
  OPTION_TYPE_BOOL,
} OptionType;

typedef struct
{
  const char *key;
  size_t offset;
  OptionType type;
} Option;

static const Option server_options[] = {
#define OPTION(name, type)                                      \
        {                                                       \
                #name,                                          \
                offsetof(VsxConfigServer, name),       \
                OPTION_TYPE_ ## type,                           \
        }
  OPTION (address, STRING),
  OPTION (port, INT),
  OPTION (certificate, STRING),
  OPTION (private_key, STRING),
  OPTION (private_key_password, STRING),
#undef OPTION
};

static const Option general_options[] = {
#define OPTION(name, type)                              \
        {                                               \
                #name,                                  \
                offsetof(VsxConfig, name),      \
                OPTION_TYPE_ ## type,                   \
        }
  OPTION (log_file, STRING),
  OPTION (user, STRING),
  OPTION (group, STRING),
#undef OPTION
};

static void
set_option (LoadConfigData *data,
            void *config_item,
            const Option *option,
            const char *value)
{
  switch (option->type)
    {
    case OPTION_TYPE_STRING:
      {
        char **ptr = (char **) ((uint8_t *) config_item + option->offset);
        if (*ptr)
          {
            load_config_error (data, "%s specified twice", option->key);
          }
        else
          {
            *ptr = vsx_strdup (value);
          }
        break;
      }
    case OPTION_TYPE_INT:
      {
        int64_t *ptr = (int64_t *) ((uint8_t *) config_item + option->offset);
        errno = 0;
        char *tail;
        *ptr = strtoll (value, &tail, 10);
        if (errno || *tail)
          {
            load_config_error (data, "invalid value for %s", option->key);
          }
        break;
      }
    case OPTION_TYPE_BOOL:
      {
        bool *ptr = (bool *) ((uint8_t *) config_item + option->offset);

        if (!strcmp (value, "true"))
          {
            *ptr = true;
          }
        else if (!strcmp (value, "false"))
          {
            *ptr = false;
          }
        else
          {
            load_config_error (data,
                               "value must be true or false for %s",
                               option->key);
          }
        break;
      }
    }
}

static void
set_from_options (LoadConfigData *data,
                  void *config_item,
                  size_t n_options,
                  const Option *options,
                  const char *key,
                  const char *value)
{
  for (unsigned i = 0; i < n_options; i++)
    {
      if (strcmp (key, options[i].key))
        continue;

      set_option (data, config_item, options + i, value);
      return;
    }

  load_config_error (data, "unknown config option: %s", key);
}

static void
load_config_func (VsxKeyValueEvent event,
                  int line_number,
                  const char *key,
                  const char *value,
                  void *user_data)
{
  LoadConfigData *data = user_data;

  switch (event)
    {
    case VSX_KEY_VALUE_EVENT_HEADER:
      if (!strcmp (value, "server"))
        {
          data->server = vsx_calloc (sizeof *data->server);
          data->server->port = -1;
          vsx_list_insert (data->config->servers.prev, &data->server->link);
        }
      else if (!strcmp (value, "general"))
        {
          data->server = NULL;
        }
      else
        {
          load_config_error (data, "unknown section: %s", value);
        }
      break;
    case VSX_KEY_VALUE_EVENT_PROPERTY:
      if (data->server)
        {
          set_from_options (data,
                            data->server,
                            VSX_N_ELEMENTS (server_options),
                            server_options, key, value);
        }
      else
        {
          set_from_options (data,
                            data->config,
                            VSX_N_ELEMENTS (general_options),
                            general_options, key, value);
        }
      break;
    }
}

static bool
validate_server (VsxConfigServer *server,
                 const char *filename,
                 struct vsx_error **error)
{
  if (server->certificate && server->private_key == NULL)
    {
      vsx_set_error (error,
                     &vsx_config_error,
                     VSX_CONFIG_ERROR_IO,
                     "%s: SSL certificate specified without "
                     "private key",
                     filename);
      return false;
    }

  if (server->private_key && server->certificate == NULL)
    {
      vsx_set_error (error,
                     &vsx_config_error,
                     VSX_CONFIG_ERROR_IO,
                     "%s: SSL private key speficied without "
                     "certificate",
                     filename);
      return false;
    }

  if (server->private_key_password && server->private_key == NULL)
    {
      vsx_set_error (error,
                     &vsx_config_error,
                     VSX_CONFIG_ERROR_IO,
                     "%s: SSL private key password speficied without "
                     "private key",
                     filename);
      return false;
    }

  return true;
}

static bool
validate_config (VsxConfig *config,
                 const char *filename,
                 struct vsx_error **error)
{
  bool found_something = false;

  VsxConfigServer *server;

  vsx_list_for_each (server, &config->servers, link)
  {
    if (!validate_server (server, filename, error))
      return false;
    found_something = true;
  }

  if (!found_something)
    {
      vsx_set_error (error,
                     &vsx_config_error,
                     VSX_CONFIG_ERROR_IO,
                     "%s: no servers configured",
                     filename);
      return false;
    }

  return true;
}

static bool
load_config (const char *fn, VsxConfig *config, struct vsx_error **error)
{
  bool ret = true;

  FILE *f = fopen (fn, "r");

  if (f == NULL)
    {
      vsx_file_error_set (error,
                          errno,
                          "%s: %s",
                          fn,
                          strerror (errno));
      ret = false;
    }
  else
    {
      LoadConfigData data = {
        .filename = fn,
        .config = config,
        .had_error = false,
        .server = NULL,
        .error_buffer = VSX_BUFFER_STATIC_INIT,
      };

      vsx_key_value_load (f, load_config_func, load_config_error_func, &data);

      if (data.had_error)
        {
          vsx_set_error (error,
                         &vsx_config_error,
                         VSX_CONFIG_ERROR_IO,
                         "%s",
                         data.error_buffer.data);
          ret = false;
        }
      else if (!validate_config (config, fn, error))
        {
          ret = false;
        }

      vsx_buffer_destroy (&data.error_buffer);

      fclose (f);
    }

  return ret;
}

VsxConfig *
vsx_config_load (const char *filename, struct vsx_error **error)
{
  VsxConfig *config = vsx_calloc (sizeof *config);

  vsx_list_init (&config->servers);

  if (!load_config (filename, config, error))
    goto error;

  return config;

error:
  vsx_config_free (config);
  return NULL;
}

static void
free_servers (VsxConfig *config)
{
  VsxConfigServer *server, *tmp;

  vsx_list_for_each_safe (server, tmp, &config->servers, link)
  {
    vsx_free (server->certificate);
    vsx_free (server->private_key);
    vsx_free (server->private_key_password);
    vsx_free (server->address);
    vsx_free (server);
  }

}

void
vsx_config_free (VsxConfig *config)
{
  free_servers (config);

  vsx_free (config->user);
  vsx_free (config->group);
  vsx_free (config->log_file);

  vsx_free (config);
}
