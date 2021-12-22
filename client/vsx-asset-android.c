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

#include "vsx-asset-android.h"

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

#include "vsx-util.h"

struct vsx_asset_manager {
        JNIEnv *env;
        jobject manager_jni;
        struct AAssetManager *manager;
};

struct vsx_asset {
        char *filename;
        struct AAsset *asset;
};

struct vsx_error_domain
vsx_asset_error;

struct vsx_asset_manager *
vsx_asset_manager_new(JNIEnv *env, jobject manager)
{
        struct vsx_asset_manager *asset_manager =
                vsx_calloc(sizeof *asset_manager);

        asset_manager->manager_jni = (*env)->NewGlobalRef(env, manager);
        asset_manager->env = env;
        asset_manager->manager =
                AAssetManager_fromJava(env, asset_manager->manager_jni);

        return asset_manager;
}

static void
set_file_error(struct vsx_asset *asset,
               struct vsx_error **error)
{
        vsx_set_error(error,
                      &vsx_asset_error,
                      VSX_ASSET_ERROR_FILE,
                      "Error reading %s",
                      asset->filename);
}

struct vsx_asset *
vsx_asset_manager_open(struct vsx_asset_manager *manager,
                       const char *filename,
                       struct vsx_error **error)
{
        struct vsx_asset *asset = vsx_calloc(sizeof *asset);

        asset->filename = vsx_strdup(filename);
        asset->asset = AAssetManager_open(manager->manager,
                                          filename,
                                          AASSET_MODE_STREAMING);

        if (asset->asset == NULL) {
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
        size_t got = AAsset_read(asset->asset, buf, amount);

        if (got < amount) {
                set_file_error(asset, error);
                return false;
        }

        return true;
}

bool
vsx_asset_remaining(struct vsx_asset *asset,
                    size_t *amount,
                    struct vsx_error **error)
{
        off_t got = AAsset_getRemainingLength(asset->asset);

        if (got < 0) {
                set_file_error(asset, error);
                return false;
        }

        *amount = got;

        return true;
}

void
vsx_asset_close(struct vsx_asset *asset)
{
        if (asset->asset)
                AAsset_close(asset->asset);
        vsx_free(asset->filename);
        vsx_free(asset);
}

void
vsx_asset_manager_free(struct vsx_asset_manager *manager)
{
        (*manager->env)->DeleteGlobalRef(manager->env, manager->manager_jni);
        vsx_free(manager);
}
