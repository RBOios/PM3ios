#import "PM3Audio.h"
#import <AVFoundation/AVFoundation.h>

@implementation PM3Audio {
    Pm3Bridge *_bridge;
    AVAudioEngine *_engine;
    AVAudioSourceNode *_node;
}

- (instancetype)initWithBridge:(Pm3Bridge *)bridge {
    if ((self = [super init])) {
        _bridge = bridge;
        [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(interrupted:) name:AVAudioSessionInterruptionNotification object:nil];
    }
    return self;
}

- (void)start {
    if (_engine) return;
    NSError *err = nil;
    AVAudioSession *session = AVAudioSession.sharedInstance;
    [session setCategory:AVAudioSessionCategoryPlayback withOptions:AVAudioSessionCategoryOptionMixWithOthers error:&err];   // Playback: plays even with the silent switch on (like a media player); mixes with other apps' audio
    [session setActive:YES error:&err];

    AVAudioFormat *fmt = [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatFloat32 sampleRate:PM3_AUDIO_RATE channels:2 interleaved:NO];
    Pm3Bridge *bridge = _bridge;
    _node = [[AVAudioSourceNode alloc] initWithFormat:fmt renderBlock:^OSStatus(BOOL *isSilence, const AudioTimeStamp *timestamp, AVAudioFrameCount frames, AudioBufferList *abl) {
        float *left = (float *)abl->mBuffers[0].mData;
        float *right = abl->mNumberBuffers > 1 ? (float *)abl->mBuffers[1].mData : left;
        int16_t tmp[512 * 2];                       // no allocation on the render thread: work in chunks
        BOOL any = NO;
        AVAudioFrameCount done = 0;
        while (done < frames) {
            int n = (int)MIN((AVAudioFrameCount)512, frames - done);
            if (pm3_bridge_pull_audio(bridge, tmp, n) > 0) any = YES;
            for (int i = 0; i < n; i++) { left[done + i] = tmp[2 * i] / 32768.0f; right[done + i] = tmp[2 * i + 1] / 32768.0f; }
            done += (AVAudioFrameCount)n;
        }
        *isSilence = !any;
        return noErr;
    }];

    _engine = [[AVAudioEngine alloc] init];
    [_engine attachNode:_node];
    [_engine connect:_node to:_engine.mainMixerNode format:fmt];
    if (![_engine startAndReturnError:&err]) NSLog(@"PM3: audio engine failed to start: %@", err);
}

- (void)stop {
    [_engine stop];
    _engine = nil;
    _node = nil;
}

- (void)interrupted:(NSNotification *)note {
    AVAudioSessionInterruptionType type = (AVAudioSessionInterruptionType)[note.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue];
    if (type == AVAudioSessionInterruptionTypeEnded && _engine) {   // call / Siri finished: restart output
        NSError *err = nil;
        [AVAudioSession.sharedInstance setActive:YES error:&err];
        [_engine startAndReturnError:&err];
    }
}

- (void)dealloc {
    [NSNotificationCenter.defaultCenter removeObserver:self];
    [self stop];
}

@end
