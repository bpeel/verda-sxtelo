/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2022  Neil Roberts
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

#import "SceneDelegate.h"

#import "MainViewController.h"

@interface SceneDelegate ()

@end

@implementation SceneDelegate {
        /* Queued activities received from the connect method we will use later
         * one the scene is first activated and all of the views are set up.
         */
        NSUserActivity *stateRestorationActivity;
        NSUserActivity *inviteActivity;
}

- (GameViewController *)findViewController:(UIScene *)scene {
        if (![scene isKindOfClass:[UIWindowScene class]])
                return nil;
        
        UIWindowScene *windowScene = (UIWindowScene *) scene;
        
        for (UIWindow *window in windowScene.windows) {
                UIViewController *viewController = window.rootViewController;
                
                if (viewController == nil)
                        continue;
                
                if ([viewController isKindOfClass:[MainViewController class]])
                        return ((MainViewController *) viewController).gameViewController;
        }
        
        return nil;
}

static bool
is_invite_activity(NSUserActivity *activity)
{
        return [NSUserActivityTypeBrowsingWeb isEqualToString:activity.activityType];
}

- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)connectionOptions {
        // Use this method to optionally configure and attach the UIWindow `window` to the provided UIWindowScene `scene`.
        // If using a storyboard, the `window` property will automatically be initialized and attached to the scene.
        // This delegate does not imply the connecting scene or session are new (see `application:configurationForConnectingSceneSession` instead).
        
        if (connectionOptions != nil) {
                for (NSUserActivity *activity in connectionOptions.userActivities) {
                        if (is_invite_activity(activity))
                                self->inviteActivity = activity;
                        break;
                }
        }
        
        if (self->inviteActivity == nil && session != nil)
                self->stateRestorationActivity = session.stateRestorationActivity;
}

- (void)scene:(UIScene *)scene continueUserActivity:(NSUserActivity *)userActivity {
        [self loadInviteUrl:userActivity forScene:scene];
}

- (void)sceneDidDisconnect:(UIScene *)scene {
        // Called as the scene is being released by the system.
        // This occurs shortly after the scene enters the background, or when its session is discarded.
        // Release any resources associated with this scene that can be re-created the next time the scene connects.
        // The scene may re-connect later, as its session was not necessarily discarded (see `application:didDiscardSceneSessions` instead).
}


- (void)sceneDidBecomeActive:(UIScene *)scene {
        // Called when the scene has moved from an inactive state to an active state.
        // Use this method to restart any tasks that were paused (or not yet started) when the scene was inactive.
}


- (void)sceneWillResignActive:(UIScene *)scene {
        // Called when the scene will move from an active state to an inactive state.
        // This may occur due to temporary interruptions (ex. an incoming phone call).
}


- (void)sceneWillEnterForeground:(UIScene *)scene {
        // Called as the scene transitions from the background to the foreground.
        // Use this method to undo the changes made on entering the background.

        if (self->stateRestorationActivity) {
                [self loadInstanceState:self->stateRestorationActivity forScene:scene];
                self->stateRestorationActivity = nil;
        }
        
        if (self->inviteActivity) {
                [self loadInviteUrl:self->inviteActivity forScene:scene];
                self->inviteActivity = nil;
        }

        GameViewController *controller = [self findViewController:scene];
        
        if (controller != nil)
                [controller enterForeground];
}


- (void)sceneDidEnterBackground:(UIScene *)scene {
        // Called as the scene transitions from the foreground to the background.
        // Use this method to save data, release shared resources, and store enough scene-specific state information
        // to restore the scene back to its current state.

        GameViewController *controller = [self findViewController:scene];
        
        if (controller != nil)
                [controller enterBackground];
}

static NSString *
get_instance_state_activity_type(void)
{
        NSBundle *bundle = [NSBundle mainBundle];
        
        if (bundle == nil)
                return nil;
        
        NSDictionary<NSString *, id> *info = [bundle infoDictionary];
        
        if (info == nil)
                return nil;
        
        id activities = info[@"NSUserActivityTypes"];
        
        if (activities == nil || ![activities isKindOfClass:[NSArray class]])
                return nil;
        
        NSArray *activitiesArray = (NSArray *) activities;
        
        if ([activitiesArray count] < 1)
                return nil;
        
        return activitiesArray[0];
}

- (NSUserActivity *)stateRestorationActivityForScene:(UIScene *)scene {
        NSString *activityType = get_instance_state_activity_type();
        
        if (activityType == nil)
                return nil;

        GameViewController *controller = [self findViewController:scene];
        
        if (controller == nil)
                return nil;
        
        NSString *instanceState = [controller getInstanceState];
        
        NSUserActivity *activity = [[NSUserActivity alloc] initWithActivityType:activityType];
        
        [activity addUserInfoEntriesFromDictionary:@{ @"instanceState": instanceState }];
        
        return activity;
}

- (void)loadInstanceState:(NSUserActivity *)activity forScene:(UIScene *)scene {
        NSString *activityType = get_instance_state_activity_type();
        
        if (activityType == nil)
                return;
        
        if (![activityType isEqualToString:activity.activityType])
                return;
        
        id instanceState = [activity.userInfo objectForKey:@"instanceState"];
        
        if (instanceState == nil || ![instanceState isKindOfClass:[NSString class]])
                return;
        
        GameViewController *controller = [self findViewController:scene];
        
        if (controller == nil)
                return;
        
        [controller setInstanceState:instanceState];
}

-(BOOL)loadInviteUrl:(NSUserActivity *)activity forScene:(UIScene *)scene {

        
        GameViewController *controller = [self findViewController:scene];
        
        if (controller == nil)
                return YES;

        [controller setInviteUrl:[activity.webpageURL absoluteString]];
        
        return YES;
}

@end
