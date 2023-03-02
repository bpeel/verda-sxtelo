/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2021  Neil Roberts
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

#ifndef VSX_ASSET_H
#define VSX_ASSET_H

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>

#include "vsx-error.h"

struct vsx_asset_manager;
struct vsx_asset;

extern struct vsx_error_domain
vsx_asset_error;

enum vsx_asset_error {
        VSX_ASSET_ERROR_FILE,
};

struct vsx_asset *
vsx_asset_manager_open(struct vsx_asset_manager *manager,
                       const char *filename,
                       struct vsx_error **error);

/* Reads the given amount of the file. If an error or EOF is
 * encountered, returns false and sets error.
 */
bool
vsx_asset_read(struct vsx_asset *asset,
               void *buf,
               size_t amount,
               struct vsx_error **error);

/* Retrieves the number of bytes remaining to the end of the file. Can
 * fail in which case it returns false and sets error.
 */
bool
vsx_asset_remaining(struct vsx_asset *asset,
                    size_t *amount,
                    struct vsx_error **error);

void
vsx_asset_close(struct vsx_asset *asset);

#endif /* VSX_ASSET_H */
