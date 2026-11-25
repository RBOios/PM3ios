// UNTESTED (written without Xcode/Metal available). Objective-C++ so the C++ core can be called directly.
#import "PM3MetalView.h"

static NSString *const kShader = @
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct VOut { float4 pos [[position]]; float2 uv; };\n"
    "vertex VOut pm3_vs(uint vid [[vertex_id]]) {\n"
    "  const float2 p[4] = { float2(-1,-1), float2(1,-1), float2(-1,1), float2(1,1) };\n"
    "  const float2 t[4] = { float2(0,1), float2(1,1), float2(0,0), float2(1,0) };\n"
    "  VOut o; o.pos = float4(p[vid], 0, 1); o.uv = t[vid]; return o; }\n"
    "fragment float4 pm3_fs(VOut in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler s [[sampler(0)]]) {\n"
    "  return tex.sample(s, in.uv); }\n";

@interface PM3MetalView () <MTKViewDelegate, UIGestureRecognizerDelegate> {
    id<MTLCommandQueue> _queue; id<MTLRenderPipelineState> _pso; id<MTLTexture> _tex;
    id<MTLSamplerState> _nearest, _linear;
    UITouch *_tracked; BOOL _multi;   // the finger that drives the left button; _multi = a second finger joined (two-finger tap = right click, no left click)
}
@end

@implementation PM3MetalView

- (instancetype)initWithFrame:(CGRect)frame bridge:(Pm3Bridge *)bridge {
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    if ((self = [super initWithFrame:frame device:dev])) {
        _bridge = bridge;
        self.delegate = self;
        self.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
        self.clearColor = MTLClearColorMake(0, 0, 0, 1);              // letterbox bars
        self.preferredFramesPerSecond = 30;                            // the original ran at ~30 fps
        self.framebufferOnly = YES;
        self.multipleTouchEnabled = YES;                               // needed to notice the 2nd finger of a two-finger tap (only the first finger drives the cursor)
        // Right mouse button (the PC game's "back / cancel": leaves CONFIG, closes menus ...):
        //   two-finger tap  or  long press (~0.5 s, finger kept still)
        UITapGestureRecognizer *two = [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(onTwoFingerTap:)];
        two.numberOfTouchesRequired = 2; two.cancelsTouchesInView = NO; two.delegate = self; [self addGestureRecognizer:two];
        UILongPressGestureRecognizer *lp = [[UILongPressGestureRecognizer alloc] initWithTarget:self action:@selector(onLongPress:)];
        lp.minimumPressDuration = 0.5; lp.allowableMovement = 12; lp.numberOfTouchesRequired = 1; lp.cancelsTouchesInView = YES; lp.delegate = self; [self addGestureRecognizer:lp];
        _queue = [dev newCommandQueue];
        // frame buffer texture: 640x480 BGRA8 == the core's 0xAARRGGBB uint32 layout
        MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                      width:pm3_bridge_width() height:pm3_bridge_height() mipmapped:NO];
        _tex = [dev newTextureWithDescriptor:td];
        NSError *err = nil;
        id<MTLLibrary> lib = [dev newLibraryWithSource:kShader options:nil error:&err];
        MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
        pd.vertexFunction = [lib newFunctionWithName:@"pm3_vs"]; pd.fragmentFunction = [lib newFunctionWithName:@"pm3_fs"];
        pd.colorAttachments[0].pixelFormat = self.colorPixelFormat;
        _pso = [dev newRenderPipelineStateWithDescriptor:pd error:&err];
        if (!_pso) NSLog(@"PM3: Metal pipeline failed: %@", err);
        MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
        sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterNearest; _nearest = [dev newSamplerStateWithDescriptor:sd];
        sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterLinear;  _linear  = [dev newSamplerStateWithDescriptor:sd];
        [self updateViewSize];
    }
    return self;
}

- (void)updateViewSize {   // view size in POINTS: touches arrive in points, so the core's touch inverse-mapping uses the same unit
    pm3_bridge_set_view_size(_bridge, (float)self.bounds.size.width, (float)self.bounds.size.height, self.integerScale ? 1 : 0);
}
- (void)layoutSubviews { [super layoutSubviews]; [self updateViewSize]; }
- (void)setIntegerScale:(BOOL)v { _integerScale = v; [self updateViewSize]; }
- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size {}

- (void)drawInMTKView:(MTKView *)view {
    if (self.frameCallback) self.frameCallback();
    MTLRegion region = MTLRegionMake2D(0, 0, pm3_bridge_width(), pm3_bridge_height());
    static uint32_t frameCopy[640 * 480];                           // the game runs on a worker thread (pm3_bridge_start_game) -> take a consistent copy
    pm3_bridge_copy_frame(_bridge, frameCopy);
    [_tex replaceRegion:region mipmapLevel:0 withBytes:frameCopy bytesPerRow:(NSUInteger)pm3_bridge_width() * 4];
    MTLRenderPassDescriptor *rp = view.currentRenderPassDescriptor; id<CAMetalDrawable> drawable = view.currentDrawable;
    if (!rp || !drawable) return;
    id<MTLCommandBuffer> cb = [_queue commandBuffer];
    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];     // clears to black = letterbox bars
    Pm3Viewport vp = pm3_bridge_viewport(_bridge); CGFloat sc = self.contentScaleFactor;
    MTLViewport mv = { vp.x * sc, vp.y * sc, vp.w * sc, vp.h * sc, 0.0, 1.0 };       // points -> pixels
    [enc setViewport:mv];
    [enc setRenderPipelineState:_pso];
    [enc setFragmentTexture:_tex atIndex:0];
    [enc setFragmentSamplerState:(self.smooth ? _linear : _nearest) atIndex:0];
    [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    [enc endEncoding];
    [cb presentDrawable:drawable]; [cb commit];
}

// Touch -> mouse (hover while moving, click on release) is implemented in the core (pm3/touch.cpp); the view only forwards points.
// Right button: two-finger tap or long press -> pm3_bridge_right_click (WM_RBUTTONDOWN/UP for the game).
- (void)onTwoFingerTap:(UITapGestureRecognizer *)g {
    if (g.state != UIGestureRecognizerStateRecognized) return;
    CGPoint p = [g locationInView:self]; pm3_bridge_touch_cancel(_bridge, 1); pm3_bridge_right_click(_bridge, (float)p.x, (float)p.y);
}
- (void)onLongPress:(UILongPressGestureRecognizer *)g {
    if (g.state != UIGestureRecognizerStateBegan) return;
    CGPoint p = [g locationInView:self]; pm3_bridge_touch_cancel(_bridge, 1); pm3_bridge_right_click(_bridge, (float)p.x, (float)p.y);
    if (@available(iOS 10.0, *)) { UIImpactFeedbackGenerator *h = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight]; [h impactOccurred]; }
}
- (BOOL)gestureRecognizer:(UIGestureRecognizer *)a shouldRecognizeSimultaneouslyWithGestureRecognizer:(UIGestureRecognizer *)b { return NO; }

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)e {
    if (e.allTouches.count > 1) { _multi = YES; if (_tracked) { pm3_bridge_touch_cancel(_bridge, 1); } return; }   // second finger: this is a two-finger gesture, not a click
    _multi = NO; _tracked = [touches anyObject]; CGPoint p = [_tracked locationInView:self]; pm3_bridge_touch_begin(_bridge, 1, (float)p.x, (float)p.y);
}
- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)e {
    if (_multi || !_tracked || ![touches containsObject:_tracked]) return;
    CGPoint p = [_tracked locationInView:self]; pm3_bridge_touch_move(_bridge, 1, (float)p.x, (float)p.y);
}
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)e {
    if (_tracked && [touches containsObject:_tracked]) {
        if (_multi) pm3_bridge_touch_cancel(_bridge, 1);
        else { CGPoint p = [_tracked locationInView:self]; pm3_bridge_touch_end(_bridge, 1, (float)p.x, (float)p.y); }
        _tracked = nil;
    }
    if (e.allTouches.count <= touches.count) _multi = NO;   // all fingers are up
}
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)e {
    if (_tracked && [touches containsObject:_tracked]) { pm3_bridge_touch_cancel(_bridge, 1); _tracked = nil; }
    if (e.allTouches.count <= touches.count) _multi = NO;
}

@end
