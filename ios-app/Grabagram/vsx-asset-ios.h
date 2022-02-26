//
//  vsx-asset-ios.h
//  Grabagram
//
//  Created by demo on 26/02/2022.
//

#ifndef vsx_asset_ios_h
#define vsx_asset_ios_h

#include "vsx-asset.h"

struct vsx_asset_manager *
vsx_asset_manager_new(void);

void
vsx_asset_manager_free(struct vsx_asset_manager *manager);

#endif /* vsx_asset_ios_h */
