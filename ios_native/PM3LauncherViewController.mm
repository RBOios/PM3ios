#import "PM3LauncherViewController.h"
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import "PM3Audio.h"
#import "PM3GameViewController.h"
#import "PM3Settings.h"
#include "pm3/bridge.h"

// Layout follows the mock-up: full-screen artwork, a dark translucent rounded panel left of centre with
//   "게임 시작하기 ▶" / "음량 설정" / "게임 폴더 선택…" / "폴더: <folder name>".
// "음량 설정" swaps the panel for three sliders (보이스 / BGM / 효과음).

@interface PM3LauncherViewController () <UIDocumentPickerDelegate>
@end

@implementation PM3LauncherViewController {
    Pm3Bridge *_bridge;
    PM3Audio *_audio;                       // runs from the first screen on, so the sliders can play a sample
    UIImageView *_background;
    UIVisualEffectView *_menuPanel, *_volumePanel;
    NSLayoutConstraint *_menuLeading, *_menuWidth, *_volumeLeading, *_volumeWidth;
    UILabel *_folderLabel;
    NSMutableArray<UILabel *> *_valueLabels;   // index == sound group (0 voice, 1 BGM, 2 SFX)
    NSString *_gameRoot;                    // folder that contains PM3WIN.EXE, nil if none found
    NSString *_folderName;                  // what the label shows
    BOOL _folderProblem;
    CGFloat _scale;
}

- (BOOL)prefersStatusBarHidden { return YES; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
- (UIInterfaceOrientationMask)supportedInterfaceOrientations { return UIInterfaceOrientationMaskLandscape; }

#pragma mark - setup

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = UIColor.blackColor;
    CGSize screen = UIScreen.mainScreen.bounds.size;
    _scale = MIN(1.4, MAX(1.0, MAX(screen.width, screen.height) / 844.0));   // bigger type on iPad

    _bridge = pm3_bridge_create();
    [PM3Settings applyVolumesToBridge:_bridge];
    _audio = [[PM3Audio alloc] initWithBridge:_bridge];
    [_audio start];

    [self buildUI];
    [self restoreFolder];
}

- (UIVisualEffectView *)makePanel {
    UIVisualEffectView *p = [[UIVisualEffectView alloc] initWithEffect:[UIBlurEffect effectWithStyle:UIBlurEffectStyleDark]];
    p.translatesAutoresizingMaskIntoConstraints = NO;
    p.layer.cornerRadius = 26;
    p.layer.masksToBounds = YES;
    UIView *tint = [[UIView alloc] initWithFrame:CGRectZero];
    tint.backgroundColor = [UIColor colorWithWhite:0.0 alpha:0.30];
    tint.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [p.contentView addSubview:tint];
    return p;
}

- (UIButton *)menuButtonWithTitle:(NSString *)title size:(CGFloat)size action:(SEL)action {
    UIButton *b = [UIButton buttonWithType:UIButtonTypeSystem];
    [b setTitle:title forState:UIControlStateNormal];
    [b setTitleColor:UIColor.whiteColor forState:UIControlStateNormal];
    b.titleLabel.font = [UIFont systemFontOfSize:size weight:UIFontWeightSemibold];
    b.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeft;
    [b addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
    return b;
}

- (UIStackView *)stackWithViews:(NSArray<UIView *> *)views in:(UIVisualEffectView *)panel {
    UIStackView *stack = [[UIStackView alloc] initWithArrangedSubviews:views];
    stack.axis = UILayoutConstraintAxisVertical;
    stack.spacing = 14 * _scale;
    stack.alignment = UIStackViewAlignmentFill;
    stack.layoutMarginsRelativeArrangement = YES;
    stack.layoutMargins = UIEdgeInsetsMake(22, 24, 22, 24);
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    UIView *content = panel.contentView;
    [content addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[
        [stack.topAnchor constraintEqualToAnchor:content.topAnchor],
        [stack.leadingAnchor constraintEqualToAnchor:content.leadingAnchor],
        [stack.trailingAnchor constraintEqualToAnchor:content.trailingAnchor],
        [stack.bottomAnchor constraintEqualToAnchor:content.bottomAnchor]]];
    return stack;
}

- (NSString *)percentText:(float)v { return [NSString stringWithFormat:@"%d%%", (int)lroundf(v * 100.0f)]; }

- (UIView *)volumeRowWithTitle:(NSString *)title channel:(int)channel {
    UILabel *name = [[UILabel alloc] init];
    name.text = title;
    name.font = [UIFont systemFontOfSize:17 * _scale weight:UIFontWeightMedium];
    name.textColor = UIColor.whiteColor;
    [name.widthAnchor constraintEqualToConstant:70 * _scale].active = YES;

    UISlider *slider = [[UISlider alloc] init];
    slider.minimumValue = 0.0f;
    slider.maximumValue = 1.5f;                       // 0..150 %, 100 % = the loudness the game always had
    slider.value = [PM3Settings volumeForChannel:channel];
    slider.tag = channel;
    slider.minimumTrackTintColor = UIColor.whiteColor;
    slider.maximumTrackTintColor = [UIColor colorWithWhite:1.0 alpha:0.30];
    [slider addTarget:self action:@selector(volumeChanged:) forControlEvents:UIControlEventValueChanged];
    [slider addTarget:self action:@selector(volumeReleased:) forControlEvents:(UIControlEventTouchUpInside | UIControlEventTouchUpOutside)];

    UILabel *value = [[UILabel alloc] init];
    value.font = [UIFont monospacedDigitSystemFontOfSize:15 * _scale weight:UIFontWeightMedium];
    value.textColor = [UIColor colorWithWhite:1.0 alpha:0.9];
    value.textAlignment = NSTextAlignmentRight;
    value.text = [self percentText:slider.value];
    [value.widthAnchor constraintEqualToConstant:54 * _scale].active = YES;
    [_valueLabels addObject:value];                   // rows are created in channel order

    UIStackView *row = [[UIStackView alloc] initWithArrangedSubviews:@[name, slider, value]];
    row.axis = UILayoutConstraintAxisHorizontal;
    row.spacing = 12;
    row.alignment = UIStackViewAlignmentCenter;
    return row;
}

- (void)buildUI {
    _background = [[UIImageView alloc] initWithFrame:self.view.bounds];
    _background.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    _background.contentMode = UIViewContentModeScaleAspectFill;
    _background.clipsToBounds = YES;
    _background.image = [UIImage imageNamed:@"launch_bg.jpg"];
    if (!_background.image) _background.backgroundColor = [UIColor colorWithRed:0.35 green:0.08 blue:0.15 alpha:1.0];   // artwork missing from the bundle
    [self.view addSubview:_background];

    // ---- main menu panel
    _menuPanel = [self makePanel];
    UIButton *start = [self menuButtonWithTitle:@"게임 시작하기  ▶" size:25 * _scale action:@selector(startGame)];
    UIButton *volume = [self menuButtonWithTitle:@"음량 설정" size:20 * _scale action:@selector(showVolumePanel)];
    UIButton *folder = [self menuButtonWithTitle:@"게임 폴더 선택…" size:20 * _scale action:@selector(chooseFolder)];
    _folderLabel = [[UILabel alloc] init];
    _folderLabel.font = [UIFont systemFontOfSize:14 * _scale weight:UIFontWeightRegular];
    _folderLabel.textColor = [UIColor colorWithWhite:1.0 alpha:0.85];
    _folderLabel.numberOfLines = 2;
    _folderLabel.lineBreakMode = NSLineBreakByTruncatingMiddle;
    UIStackView *menu = [self stackWithViews:@[start, volume, folder, _folderLabel] in:_menuPanel];
    [menu setCustomSpacing:20 * _scale afterView:start];
    [menu setCustomSpacing:20 * _scale afterView:folder];
    [self.view addSubview:_menuPanel];
    _menuLeading = [_menuPanel.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:80];
    _menuWidth = [_menuPanel.widthAnchor constraintEqualToConstant:300];
    [NSLayoutConstraint activateConstraints:@[_menuLeading, _menuWidth,
        [_menuPanel.bottomAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.bottomAnchor constant:-32]]];

    // ---- volume panel (same spot, hidden until "음량 설정")
    _volumePanel = [self makePanel];
    _volumePanel.hidden = YES;
    _volumePanel.alpha = 0.0;
    UILabel *title = [[UILabel alloc] init];
    title.text = @"음량 설정";
    title.font = [UIFont systemFontOfSize:22 * _scale weight:UIFontWeightBold];
    title.textColor = UIColor.whiteColor;
    _valueLabels = [NSMutableArray array];
    NSArray<NSString *> *names = @[@"보이스", @"BGM", @"효과음"];      // order == PM3_VOL_VOICE, PM3_VOL_BGM, PM3_VOL_SFX
    NSMutableArray<UIView *> *rows = [NSMutableArray arrayWithObject:title];
    for (int ch = 0; ch < 3; ch++) [rows addObject:[self volumeRowWithTitle:names[(NSUInteger)ch] channel:ch]];
    [rows addObject:[self menuButtonWithTitle:@"완료" size:20 * _scale action:@selector(hideVolumePanel)]];
    [self stackWithViews:rows in:_volumePanel];
    [self.view addSubview:_volumePanel];
    _volumeLeading = [_volumePanel.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:80];
    _volumeWidth = [_volumePanel.widthAnchor constraintEqualToConstant:380];
    [NSLayoutConstraint activateConstraints:@[_volumeLeading, _volumeWidth,
        [_volumePanel.bottomAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.bottomAnchor constant:-32]]];
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    CGFloat w = self.view.bounds.size.width;
    CGFloat lead = MAX(self.view.safeAreaInsets.left + 24, w * 0.14);
    _menuLeading.constant = lead;
    _volumeLeading.constant = lead;
    _menuWidth.constant = MIN(420, MAX(260, w * 0.30));
    _volumeWidth.constant = MIN(520, MAX(340, w * 0.46));
}

#pragma mark - folder

- (void)refreshFolderLabel {
    NSString *name = _folderName ?: @"선택 안 됨";
    _folderLabel.text = _folderProblem ? [NSString stringWithFormat:@"폴더: %@ (PM3WIN.EXE 없음)", name] : [NSString stringWithFormat:@"폴더: %@", name];
}

- (void)restoreFolder {
    _folderProblem = NO;
    NSURL *url = [PM3Settings restoreGameFolderURL];
    if (url) {
        _folderName = url.lastPathComponent;
        _gameRoot = [PM3Settings findGameRootInDirectory:url.path];
        _folderProblem = (_gameRoot == nil);
    }
    if (!_gameRoot) {   // nothing chosen yet: files copied into the app's Documents with the Files app still work
        NSString *docs = [PM3Settings documentsPath];
        NSString *root = [PM3Settings findGameRootInDirectory:docs];
        if (root) { _gameRoot = root; _folderName = [root isEqualToString:docs] ? @"Documents" : root.lastPathComponent; _folderProblem = NO; }
    }
    [self refreshFolderLabel];
}

- (void)chooseFolder {
    UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[UTTypeFolder]];
    picker.delegate = self;
    picker.allowsMultipleSelection = NO;
    [self presentViewController:picker animated:YES completion:nil];
}

- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    NSURL *url = urls.firstObject;
    if (!url) return;
    [PM3Settings beginAccessToURL:url];                                   // must stay open while the game reads its files
    NSString *root = [PM3Settings findGameRootInDirectory:url.path];
    if (!root) {
        [PM3Settings restoreGameFolderURL];                               // the previous choice (if any) keeps working
        dispatch_async(dispatch_get_main_queue(), ^{
            [self alertTitle:@"게임 파일을 찾지 못했습니다" message:@"선택한 폴더(또는 그 안의 폴더)에\nPM3WIN.EXE가 있어야 합니다."];
        });
        return;
    }
    [PM3Settings rememberGameFolderURL:url];
    _gameRoot = root;
    _folderName = url.lastPathComponent;
    _folderProblem = NO;
    [self refreshFolderLabel];
}

#pragma mark - start

- (void)alertTitle:(NSString *)title message:(NSString *)message {
    UIAlertController *a = [UIAlertController alertControllerWithTitle:title message:message preferredStyle:UIAlertControllerStyleAlert];
    [a addAction:[UIAlertAction actionWithTitle:@"확인" style:UIAlertActionStyleDefault handler:nil]];
    [self presentViewController:a animated:YES completion:nil];
}

- (void)startGame {
    if (!_gameRoot) {
        [self alertTitle:@"게임 폴더를 선택하세요" message:@"PM3WIN.EXE가 들어 있는\n원본 게임 폴더를 '게임 폴더 선택…'으로 지정해 주세요."];
        return;
    }
    pm3_bridge_set_paths(_bridge, _gameRoot.fileSystemRepresentation, [PM3Settings savesPath].fileSystemRepresentation);
    if (!pm3_bridge_file_exists(_bridge, "PM3WIN.EXE")) {
        [self alertTitle:@"게임 파일을 읽을 수 없습니다" message:@"폴더를 다시 선택해 주세요.\n(iCloud 파일은 먼저 기기에 다운로드해야 합니다.)"];
        return;
    }
    PM3GameViewController *game = [[PM3GameViewController alloc] initWithBridge:_bridge];
    game.modalPresentationStyle = UIModalPresentationFullScreen;
    __weak PM3LauncherViewController *weakSelf = self;
    game.onFinished = ^(NSString *reason) {
        PM3LauncherViewController *s = weakSelf;
        if (!s) return;
        [s dismissViewControllerAnimated:YES completion:^{ if (reason) [s alertTitle:@"게임을 실행할 수 없습니다" message:reason]; }];
    };
    [self presentViewController:game animated:YES completion:nil];
}

#pragma mark - volume

- (void)showVolumePanel {
    _volumePanel.hidden = NO;
    [UIView animateWithDuration:0.2 animations:^{ self->_menuPanel.alpha = 0.0; self->_volumePanel.alpha = 1.0; }
                     completion:^(BOOL finished) { self->_menuPanel.hidden = YES; }];
}

- (void)hideVolumePanel {
    _menuPanel.hidden = NO;
    [UIView animateWithDuration:0.2 animations:^{ self->_menuPanel.alpha = 1.0; self->_volumePanel.alpha = 0.0; }
                     completion:^(BOOL finished) { self->_volumePanel.hidden = YES; }];
}

- (void)volumeChanged:(UISlider *)slider {
    float v = roundf(slider.value * 20.0f) / 20.0f;                      // 5 % steps
    slider.value = v;
    int ch = (int)slider.tag;
    [PM3Settings setVolume:v forChannel:ch];
    pm3_bridge_set_volume(_bridge, ch, v);
    if ((NSUInteger)ch < _valueLabels.count) _valueLabels[(NSUInteger)ch].text = [self percentText:v];
}

- (void)volumeReleased:(UISlider *)slider {
    pm3_bridge_audio_preview(_bridge, (int)slider.tag);                  // let the user hear the level
}

- (void)dealloc {
    [_audio stop];
    if (_bridge) pm3_bridge_destroy(_bridge);
}

@end
