//
//  GameViewController.h
//  Grabagram
//
//  Created by demo on 04/03/2022.
//

#import <UIKit/UIKit.h>
#import <GLKit/GLKit.h>

@interface GameViewController : GLKViewController <GLKViewDelegate>

-(void)enterBackground;
-(void)enterForeground;
-(NSString *)getInstanceState;
-(void)setInstanceState:(NSString *)state;
-(void)setInviteUrl:(NSString *)url;
-(void)enterName;

@end
