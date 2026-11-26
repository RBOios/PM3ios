// UNTESTED (written without Xcode available). The screen that runs the game: Metal view + the worker thread started by pm3_bridge_start_game().
#import <UIKit/UIKit.h>
#include "pm3/bridge.h"

@interface PM3GameViewController : UIViewController
// The bridge must already have its paths set (pm3_bridge_set_paths). The game starts when the view has appeared.
- (instancetype)initWithBridge:(Pm3Bridge *)bridge;
// Called once on the main thread when the game stopped (the guest exited / could not start). reason is nil for a normal exit.
@property (nonatomic, copy) void (^onFinished)(NSString *reason);
@end
