// UNTESTED (written without Xcode available; the GitHub Actions job `ipa-native` is the first real compile). Minimal UIKit host for the native path:
//   Documents/ (or one sub folder, e.g. Documents/PM3) holds the user's game files (PM3WIN.EXE, *.FPA, ...) -> pm3_bridge_start_game() runs them
//   Documents/Saves holds everything the game writes. The app is pure Objective-C++: no storyboard, no SDL.
#import <UIKit/UIKit.h>
#import "PM3MetalView.h"
#include "pm3/bridge.h"

@interface PM3ViewController : UIViewController
@end

@implementation PM3ViewController {
    Pm3Bridge *_bridge;
    PM3MetalView *_metal;
    UILabel *_label;
    NSTimer *_watch;
}

- (BOOL)prefersStatusBarHidden { return YES; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
- (UIInterfaceOrientationMask)supportedInterfaceOrientations { return UIInterfaceOrientationMaskLandscape; }

// Looks for PM3WIN.EXE in Documents and in its direct sub folders (case-insensitive lookup is done by the core's Vfs). Returns the folder or nil.
- (NSString *)findGameFolderIn:(NSString *)docs saves:(NSString *)saves {
    NSMutableArray<NSString *> *candidates = [NSMutableArray arrayWithObject:docs];
    for (NSString *name in [[NSFileManager defaultManager] contentsOfDirectoryAtPath:docs error:nil] ?: @[]) {
        BOOL isDir = NO; NSString *full = [docs stringByAppendingPathComponent:name];
        if ([[NSFileManager defaultManager] fileExistsAtPath:full isDirectory:&isDir] && isDir && ![name isEqualToString:@"Saves"]) [candidates addObject:full];
    }
    for (NSString *dir in candidates) {
        pm3_bridge_set_paths(_bridge, dir.fileSystemRepresentation, saves.fileSystemRepresentation);
        if (pm3_bridge_file_exists(_bridge, "PM3WIN.EXE")) return dir;
    }
    return nil;
}

- (void)showMessage:(NSString *)text {
    if (!_label) {
        _label = [[UILabel alloc] initWithFrame:CGRectInset(self.view.bounds, 24, 24)];
        _label.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
        _label.numberOfLines = 0; _label.textAlignment = NSTextAlignmentCenter; _label.textColor = UIColor.whiteColor;
        _label.font = [UIFont systemFontOfSize:17];
        [self.view addSubview:_label];
    }
    _label.text = text; _label.hidden = NO;
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = UIColor.blackColor;
    _bridge = pm3_bridge_create();

    NSString *docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    NSString *saves = [docs stringByAppendingPathComponent:@"Saves"];
    [[NSFileManager defaultManager] createDirectoryAtPath:saves withIntermediateDirectories:YES attributes:nil error:nil];

    NSString *game = [self findGameFolderIn:docs saves:saves];
    if (!game) {
        [self showMessage:@"게임 파일을 찾지 못했습니다.\n\n파일 앱 → 이 iPhone/iPad → PM3 폴더에\n원본 게임 폴더(PM3WIN.EXE, *.FPA …)를 복사한 뒤\n앱을 다시 실행하세요."];
        return;
    }

    _metal = [[PM3MetalView alloc] initWithFrame:self.view.bounds bridge:_bridge];
    _metal.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [self.view addSubview:_metal];

    if (!pm3_bridge_start_game(_bridge)) { [self showMessage:@"게임을 시작하지 못했습니다."]; return; }

    // the guest's clock must not run while the app is inactive / in the background
    NSNotificationCenter *nc = NSNotificationCenter.defaultCenter;
    [nc addObserver:self selector:@selector(pause) name:UIApplicationWillResignActiveNotification object:nil];
    [nc addObserver:self selector:@selector(resume) name:UIApplicationDidBecomeActiveNotification object:nil];
    _watch = [NSTimer scheduledTimerWithTimeInterval:1.0 target:self selector:@selector(checkRunning) userInfo:nil repeats:YES];
}

- (void)pause { if (_bridge) pm3_bridge_set_paused(_bridge, 1); }
- (void)resume { if (_bridge) pm3_bridge_set_paused(_bridge, 0); }

- (void)checkRunning {
    if (_bridge && !pm3_bridge_game_running(_bridge)) { [_watch invalidate]; _watch = nil; [self showMessage:@"게임이 종료되었습니다.\n앱을 다시 실행하세요."]; }
}

- (void)dealloc {
    [NSNotificationCenter.defaultCenter removeObserver:self];
    [_watch invalidate];
    if (_bridge) pm3_bridge_destroy(_bridge);   // stops and joins the worker
}

@end

@interface PM3AppDelegate : UIResponder <UIApplicationDelegate>
@property (nonatomic, strong) UIWindow *window;
@end

@implementation PM3AppDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = [PM3ViewController new];
    [self.window makeKeyAndVisible];
    return YES;
}
@end

int main(int argc, char *argv[]) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass([PM3AppDelegate class])); }
}
