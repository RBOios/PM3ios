// UNTESTED (written without Xcode/Metal available): iOS front end for the C++ core. See README.md in this folder.
#import <MetalKit/MetalKit.h>
#include "pm3/bridge.h"

// Draws the core's 640x480 BGRA frame buffer aspect-fit (letterboxed) and forwards touches to the core's touch->mouse layer.
@interface PM3MetalView : MTKView
@property (nonatomic, assign) Pm3Bridge *bridge;
// Called once per display frame on the main thread, before the frame buffer is uploaded: step the core here (single-step API, no blocking loop).
@property (nonatomic, copy) void (^frameCallback)(void);
@property (nonatomic, assign) BOOL smooth;           // NO = nearest (crisp pixels), YES = linear
@property (nonatomic, assign) BOOL integerScale;     // snap the scale to whole numbers when >= 1
- (instancetype)initWithFrame:(CGRect)frame bridge:(Pm3Bridge *)bridge;
@end
