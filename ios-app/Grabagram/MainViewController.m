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

- (BOOL)textFieldShouldReturn:(UITextField *)textField {
        if (self->_gameViewController)
                [self->_gameViewController enterName];
        return NO;
}

- (BOOL)textField:(UITextField *)textField
shouldChangeCharactersInRange:(NSRange)range
replacementString:(NSString *)string {
        NSUInteger originalLength = textField.text.length;
        
        return originalLength - range.length + string.length <= 30;
}

@end