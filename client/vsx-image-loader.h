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

#ifndef VSX_IMAGE_LOADER_H
#define VSX_IMAGE_LOADER_H

#include "vsx-image.h"
#include "vsx-main-thread.h"
#include "vsx-asset.h"
#include "vsx-error.h"

struct vsx_image_loader;
struct vsx_image_loader_token;

typedef void
(* vsx_image_loader_callback)(const struct vsx_image *image,
                              struct vsx_error *error,
                              void *data);

struct vsx_image_loader *
vsx_image_loader_new(struct vsx_main_thread *main_thread,
                     struct vsx_asset_manager *asset_manager);

struct vsx_image_loader_token *
vsx_image_loader_load(struct vsx_image_loader *loader,
                      const char *filename,
                      vsx_image_loader_callback func,
                      void *user_data);

void
vsx_image_loader_cancel(struct vsx_image_loader_token *token);

void
vsx_image_loader_free(struct vsx_image_loader *loader);

#endif /* VSX_IMAGE_LOADER_H */
