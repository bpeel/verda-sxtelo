/*
 * Gemelo - A server for chatting with strangers in a foreign language
 * Copyright (C) 2011  Neil Roberts
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

#ifndef __GML_LOG_H__
#define __GML_LOG_H__

#include <glib.h>

G_BEGIN_DECLS

gboolean
gml_log_available (void);

void
gml_log (const char *format,
         ...) G_GNUC_PRINTF (1, 2);

gboolean
gml_log_set_file (const char *filename,
                  GError **error);

gboolean
gml_log_start (GError **error);

void
gml_log_close (void);

G_END_DECLS

#endif /* __GML_LOG_H__ */
