//
//  MainViewController.m
//  Grabagram
//
//  Created by demo on 04/03/2022.
//

#import "MainViewController.h"

@implementation MainViewController {
        
}

-(void)prepareForSegue:(UIStoryboardSegue *)segue sender:(id)sender {
        UIViewController *dest = segue.destinationViewController;
        
        if (dest && [dest isKindOfClass:[GameViewController class]])
                self->_gameViewController = (GameViewController *) dest;
}

@end
