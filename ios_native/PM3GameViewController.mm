#import "PM3GameViewController.h"
#import "PM3MetalView.h"

@implementation PM3GameViewController {
    Pm3Bridge *_bridge;
    PM3MetalView *_metal;
    NSTimer *_watch;
    BOOL _started, _finished;
}

- (instancetype)initWithBridge:(Pm3Bridge *)bridge {
    if ((self = [super initWithNibName:nil bundle:nil])) { _bridge = bridge; }
    return self;
}

- (BOOL)prefersStatusBarHidden { return YES; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
- (UIInterfaceOrientationMask)supportedInterfaceOrientations { return UIInterfaceOrientationMaskLandscape; }

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = UIColor.blackColor;
    _metal = [[PM3MetalView alloc] initWithFrame:self.view.bounds bridge:_bridge];
    _metal.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [self.view addSubview:_metal];
}

// The game starts after the screen is on display, so that "could not start" can dismiss it cleanly.
- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    if (_started) return;
    _started = YES;
    if (!pm3_bridge_start_game(_bridge)) { [self finishWithReason:@"게임을 시작하지 못했습니다.\nPM3WIN.EXE가 들어 있는 폴더인지 확인하세요."]; return; }
    // the guest's clock must not run while the app is inactive / in the background
    NSNotificationCenter *nc = NSNotificationCenter.defaultCenter;
    [nc addObserver:self selector:@selector(pauseGame) name:UIApplicationWillResignActiveNotification object:nil];
    [nc addObserver:self selector:@selector(resumeGame) name:UIApplicationDidBecomeActiveNotification object:nil];
    _watch = [NSTimer scheduledTimerWithTimeInterval:0.5 target:self selector:@selector(checkRunning) userInfo:nil repeats:YES];
}

- (void)pauseGame { pm3_bridge_set_paused(_bridge, 1); }
- (void)resumeGame { pm3_bridge_set_paused(_bridge, 0); }

- (void)checkRunning {
    if (!pm3_bridge_game_running(_bridge)) [self finishWithReason:nil];   // the game exited by itself ("게임 종료")
}

- (void)finishWithReason:(NSString *)reason {
    if (_finished) return;
    _finished = YES;
    [_watch invalidate]; _watch = nil;
    [NSNotificationCenter.defaultCenter removeObserver:self];
    pm3_bridge_set_paused(_bridge, 0);
    pm3_bridge_stop_game(_bridge);                       // joins the worker; safe when it already ended
    if (self.onFinished) self.onFinished(reason);
}

- (void)dealloc {
    [_watch invalidate];
    [NSNotificationCenter.defaultCenter removeObserver:self];
}

@end
