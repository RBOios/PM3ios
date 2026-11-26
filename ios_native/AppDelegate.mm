// UNTESTED (written without Xcode available; the GitHub Actions job `ipa-native` is the first real compile). Pure Objective-C++, no storyboard, no SDL.
// Start screen (PM3LauncherViewController): artwork, "게임 시작하기 / 음량 설정 / 게임 폴더 선택…". The game itself runs in PM3GameViewController.
#import <UIKit/UIKit.h>
#import "PM3LauncherViewController.h"

@interface PM3AppDelegate : UIResponder <UIApplicationDelegate>
@property (nonatomic, strong) UIWindow *window;
@end

@implementation PM3AppDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = [PM3LauncherViewController new];
    [self.window makeKeyAndVisible];
    return YES;
}
@end

int main(int argc, char *argv[]) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass([PM3AppDelegate class])); }
}
