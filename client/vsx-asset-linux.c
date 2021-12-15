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

#include "config.h"

#include "vsx-asset-linux.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "vsx-util.h"

struct vsx_asset_manager {
        int stub;
};

struct vsx_asset {
        char *filename;
        FILE *file;
};

struct vsx_error_domain
vsx_asset_error;

struct vsx_asset_manager *
vsx_asset_manager_new(void)
{
        return vsx_alloc(sizeof (struct vsx_asset_manager));
}

static void
set_file_error(struct vsx_asset *asset,
               struct vsx_error **error)
{
        vsx_set_error(error,
                      &vsx_asset_error,
                      VSX_ASSET_ERROR_FILE,
                      "%s: %s",
                      asset->filename,
                      strerror(errno));
}

struct vsx_asset *
vsx_asset_manager_open(struct vsx_asset_manager *manager,
                       const char *filename,
                       struct vsx_error **error)
{
        struct vsx_asset *asset = vsx_calloc(sizeof *asset);

        asset->filename = vsx_strconcat("app/src/main/assets/", filename, NULL);
        asset->file = fopen(asset->filename, "rb");

        if (asset->file == NULL) {
                set_file_error(asset, error);
                vsx_asset_close(asset);
                return NULL;
        }

        return asset;
}

bool
vsx_asset_read(struct vsx_asset *asset,
               void *buf,
               size_t amount,
               struct vsx_error **error)
{
        size_t got = fread(buf, 1, amount, asset->file);

        if (got < amount) {
                if (feof(asset->file)) {
                        vsx_set_error(error,
                                      &vsx_asset_error,
                                      VSX_ASSET_ERROR_FILE,
                                      "%s: Unexpected EOF",
                                      asset->filename);
                } else {
                        set_file_error(asset, error);
                }

                return false;
        }

        return true;
}

bool
vsx_asset_remaining(struct vsx_asset *asset,
                    size_t *amount,
                    struct vsx_error **error)
{
        long old_pos, end_pos;

        if ((old_pos = ftell(asset->file)) < 0 ||
            fseek(asset->file, 0, SEEK_END) == -1 ||
            (end_pos = ftell(asset->file)) < 0 ||
            fseek(asset->file, old_pos, SEEK_SET) == -1) {
                set_file_error(asset, error);
                return false;
        }

        *amount = end_pos - old_pos;

        return true;
}

void
vsx_asset_close(struct vsx_asset *asset)
{
        if (asset->file)
                fclose(asset->file);
        vsx_free(asset->filename);
        vsx_free(asset);
}

void
vsx_asset_manager_free(struct vsx_asset_manager *manager)
{
        vsx_free(manager);
}
