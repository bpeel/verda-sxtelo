//
//  SceneDelegate.m
//  Grabagram
//
//  Created by demo on 02/03/2022.
//

#import "SceneDelegate.h"

#import "ViewController.h"

@interface SceneDelegate ()

@end

@implementation SceneDelegate

- (ViewController *)findViewController:(UIScene *)scene {
        if (![scene isKindOfClass:[UIWindowScene class]])
                return nil;
        
        UIWindowScene *windowScene = (UIWindowScene *) scene;
        
        for (UIWindow *window in windowScene.windows) {
                UIViewController *viewController = window.rootViewController;
                
                if (viewController == nil)
                        continue;
                
                if ([viewController isKindOfClass:[ViewController class]])
                        return (ViewController *) viewController;
        }
        
        return nil;
}

- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)connectionOptions {
        // Use this method to optionally configure and attach the UIWindow `window` to the provided UIWindowScene `scene`.
        // If using a storyboard, the `window` property will automatically be initialized and attached to the scene.
        // This delegate does not imply the connecting scene or session are new (see `application:configurationForConnectingSceneSession` instead).
        
        [self loadInstanceState:session forScene:scene];
        [self loadInviteUrl:connectionOptions forScene:scene];
}

- (void)scene:(UIScene *)scene continueUserActivity:(NSUserActivity *)userActivity {
        [self loadInviteUrlFromActivity:userActivity forScene:scene];
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

        ViewController *controller = [self findViewController:scene];
        
        if (controller != nil)
                [controller enterForeground];
}


- (void)sceneDidEnterBackground:(UIScene *)scene {
        // Called as the scene transitions from the foreground to the background.
        // Use this method to save data, release shared resources, and store enough scene-specific state information
        // to restore the scene back to its current state.

        ViewController *controller = [self findViewController:scene];
        
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

        ViewController *controller = [self findViewController:scene];
        
        if (controller == nil)
                return nil;
        
        NSString *instanceState = [controller getInstanceState];
        
        NSUserActivity *activity = [[NSUserActivity alloc] initWithActivityType:activityType];
        
        [activity addUserInfoEntriesFromDictionary:@{ @"instanceState": instanceState }];
        
        return activity;
}

- (void)loadInstanceState:(UISceneSession *)session forScene:(UIScene *)scene {
        if (session == nil)
                return;
        
        NSUserActivity *activity = session.stateRestorationActivity;
        
        if (activity == nil)
                return;
        
        NSString *activityType = get_instance_state_activity_type();
        
        if (activityType == nil)
                return;
        
        if (![activityType isEqualToString:activity.activityType])
                return;
        
        id instanceState = [activity.userInfo objectForKey:@"instanceState"];
        
        if (instanceState == nil || ![instanceState isKindOfClass:[NSString class]])
                return;
        
        ViewController *controller = [self findViewController:scene];
        
        if (controller == nil)
                return;
        
        [controller setInstanceState:instanceState];
}

- (void)loadInviteUrl:(UISceneConnectionOptions *)connectionOptions forScene:(UIScene *)scene {
        if (connectionOptions == nil)
                return;
        
        for (NSUserActivity *activity in connectionOptions.userActivities) {
                if ([self loadInviteUrlFromActivity:activity forScene:scene])
                        break;
        }
}

-(BOOL)loadInviteUrlFromActivity:(NSUserActivity *)activity forScene:(UIScene *)scene {
        if (![NSUserActivityTypeBrowsingWeb isEqualToString:activity.activityType])
                return NO;
        
        ViewController *controller = [self findViewController:scene];
        
        if (controller == nil)
                return YES;

        [controller setInviteUrl:[activity.webpageURL absoluteString]];
        
        return YES;
}

@end
