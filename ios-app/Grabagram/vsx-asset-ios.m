//
//  vsx-asset-ios.m
//  Grabagram
//
//  Created by demo on 28/02/2022.
//

#include "vsx-asset-ios.h"

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

#include <errno.h>
#include <string.h>

#include "vsx-util.h"

struct vsx_asset_manager {
        int stub;
};

struct vsx_asset {
        char *filename;
        size_t offset;
        void *asset_ptr;
};

struct vsx_error_domain
vsx_asset_error;

struct vsx_asset_manager *
vsx_asset_manager_new(void)
{
        return vsx_alloc(sizeof (struct vsx_asset_manager));
}

static void
strip_extension(char *filename)
{
        char *end = filename + strlen(filename);
        
        while (end > filename) {
                end--;

                if (*end == '.') {
                        *end = '\0';
                        break;
                }
        }
}

struct vsx_asset *
vsx_asset_manager_open(struct vsx_asset_manager *manager,
                       const char *filename,
                       struct vsx_error **error)
{
        struct vsx_asset *asset = vsx_calloc(sizeof *asset);

        asset->filename = vsx_strdup(filename);
        
        strip_extension(asset->filename);
        
        NSString *filenameString = [NSString stringWithUTF8String:asset->filename];
        
        NSDataAsset *asset_ns = [[NSDataAsset alloc] initWithName:filenameString];
        
        if (asset_ns == nil) {
                vsx_set_error(error,
                              &vsx_asset_error,
                              VSX_ASSET_ERROR_FILE,
                              "Error loading: %s",
                              filename);
                vsx_asset_close(asset);
                
                return NULL;
        }
        
        asset->asset_ptr = (void *) CFBridgingRetain(asset_ns);
        
        return asset;
}

bool
vsx_asset_read(struct vsx_asset *asset,
               void *buf,
               size_t amount,
               struct vsx_error **error)
{
        NSDataAsset *asset_ns = (__bridge NSDataAsset *) asset->asset_ptr;

        [asset_ns.data getBytes:buf range:NSMakeRange(asset->offset, amount)];
        asset->offset += amount;

        return true;
}

bool
vsx_asset_remaining(struct vsx_asset *asset,
                    size_t *amount,
                    struct vsx_error **error)
{
        NSDataAsset *asset_ns = (__bridge NSDataAsset *) asset->asset_ptr;

        *amount = [asset_ns.data length] - asset->offset;

        return true;
}

void
vsx_asset_close(struct vsx_asset *asset)
{
        vsx_free(asset->filename);
        
        if (asset->asset_ptr == NULL)
                CFBridgingRelease(asset->asset_ptr);

        vsx_free(asset);
}

void
vsx_asset_manager_free(struct vsx_asset_manager *manager)
{
        vsx_free(manager);
}
