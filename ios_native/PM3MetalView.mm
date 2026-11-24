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

@interface PM3MetalView () <MTKViewDelegate> {
    id<MTLCommandQueue> _queue; id<MTLRenderPipelineState> _pso; id<MTLTexture> _tex;
    id<MTLSamplerState> _nearest, _linear;
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
        self.multipleTouchEnabled = NO;
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
- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)e { CGPoint p = [[touches anyObject] locationInView:self]; pm3_bridge_touch_begin(_bridge, 1, (float)p.x, (float)p.y); }
- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)e { CGPoint p = [[touches anyObject] locationInView:self]; pm3_bridge_touch_move(_bridge, 1, (float)p.x, (float)p.y); }
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)e { CGPoint p = [[touches anyObject] locationInView:self]; pm3_bridge_touch_end(_bridge, 1, (float)p.x, (float)p.y); }
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)e { pm3_bridge_touch_cancel(_bridge, 1); }

@end
