#import <Cocoa/Cocoa.h>
#include "../../fb2k.h"
#include "platform/mac/mac_canvas.h"
#include "../../core/js_api_util.h"
#include "../../core/js_panel_api.h"
#include "../../core/log.h"
#include "../../core/panel.h"
#include "../../core/platform.h"
#include "../../core/settings.h"
#include <map>
#include <memory>

namespace {

NSString* ns(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()] ?: @""; }

}

@interface FooJsPanelMenuTarget : NSObject
@property (nonatomic, assign) NSInteger chosen;
- (void)picked:(NSMenuItem*)item;
@end

@implementation FooJsPanelMenuTarget
- (void)picked:(NSMenuItem*)item { self.chosen = item.tag; }
@end

@interface FooJsPanelEditorWindow : NSWindow
@property (nonatomic, copy) void (^onApply)(BOOL close);
@end

@implementation FooJsPanelEditorWindow
- (BOOL)performKeyEquivalent:(NSEvent*)e {
    const NSEventModifierFlags m = e.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask;
    if (m == NSEventModifierFlagCommand && [e.charactersIgnoringModifiers isEqualToString:@"s"]) {
        if (self.onApply) self.onApply(NO);
        return YES;
    }
    if (m == NSEventModifierFlagCommand && [e.charactersIgnoringModifiers isEqualToString:@"\r"]) {
        if (self.onApply) self.onApply(YES);
        return YES;
    }
    return [super performKeyEquivalent:e];
}
- (void)cancelOperation:(id)sender { [self close]; }
@end

@interface FooJsPanelEditor : NSObject <NSTextViewDelegate, NSWindowDelegate>
- (instancetype)initWithTitle:(NSString*)title text:(NSString*)text hooks:(jsp::EditorHooks)hooks;
- (void)show;
- (BOOL)isOpen;
- (void)close;
@end

@implementation FooJsPanelEditor {
    jsp::EditorHooks _hooks;
    NSString* _original;
    FooJsPanelEditorWindow* _window;
    NSTextView* _text;
    NSTextField* _status;
    std::string _syntax;
    std::string _applied;
    NSTimer* _check;
}

- (NSButton*)button:(NSString*)title action:(SEL)action {
    NSButton* b = [NSButton buttonWithTitle:title target:self action:action];
    b.translatesAutoresizingMaskIntoConstraints = NO;
    return b;
}

- (instancetype)initWithTitle:(NSString*)title text:(NSString*)text hooks:(jsp::EditorHooks)hooks {
    if (!(self = [super init])) return nil;
    _hooks = std::move(hooks);
    _original = [text copy];
    _window = [[FooJsPanelEditorWindow alloc] initWithContentRect:NSMakeRect(0, 0, 900, 640)
                                                        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                                  NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable
                                                          backing:NSBackingStoreBuffered
                                                            defer:NO];
    _window.title = title;
    _window.releasedWhenClosed = NO;
    _window.delegate = self;
    __weak FooJsPanelEditor* weakSelf = self;
    _window.onApply = ^(BOOL close) { [weakSelf apply:close]; };

    NSScrollView* scroll = [NSTextView scrollableTextView];
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    scroll.hasHorizontalScroller = YES;
    _text = (NSTextView*)scroll.documentView;
    _text.richText = NO;
    _text.allowsUndo = YES;
    _text.automaticQuoteSubstitutionEnabled = NO;
    _text.automaticDashSubstitutionEnabled = NO;
    _text.automaticTextReplacementEnabled = NO;
    _text.automaticSpellingCorrectionEnabled = NO;
    _text.continuousSpellCheckingEnabled = NO;
    _text.font = [NSFont monospacedSystemFontOfSize:13 weight:NSFontWeightRegular];
    _text.textContainer.widthTracksTextView = NO;
    _text.textContainer.containerSize = NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX);
    _text.horizontallyResizable = YES;
    _text.string = text;
    _text.delegate = self;

    _status = [NSTextField labelWithString:@""];
    _status.translatesAutoresizingMaskIntoConstraints = NO;
    _status.lineBreakMode = NSLineBreakByTruncatingTail;
    _status.textColor = NSColor.secondaryLabelColor;
    [_status setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];

    NSButton* revert = [self button:@"Revert" action:@selector(revert:)];
    NSButton* applyB = [self button:@"Apply" action:@selector(applyOnly:)];
    NSButton* ok = [self button:@"OK" action:@selector(applyClose:)];
    ok.keyEquivalent = @"";
    NSButton* cancel = [self button:@"Cancel" action:@selector(cancel:)];

    NSView* v = _window.contentView;
    for (NSView* c in @[ scroll, _status, revert, applyB, ok, cancel ]) [v addSubview:c];
    [NSLayoutConstraint activateConstraints:@[
        [scroll.topAnchor constraintEqualToAnchor:v.topAnchor constant:8],
        [scroll.leadingAnchor constraintEqualToAnchor:v.leadingAnchor constant:8],
        [scroll.trailingAnchor constraintEqualToAnchor:v.trailingAnchor constant:-8],
        [_status.topAnchor constraintEqualToAnchor:scroll.bottomAnchor constant:6],
        [_status.leadingAnchor constraintEqualToAnchor:v.leadingAnchor constant:10],
        [_status.trailingAnchor constraintEqualToAnchor:v.trailingAnchor constant:-10],
        [revert.topAnchor constraintEqualToAnchor:_status.bottomAnchor constant:8],
        [revert.leadingAnchor constraintEqualToAnchor:v.leadingAnchor constant:8],
        [revert.bottomAnchor constraintEqualToAnchor:v.bottomAnchor constant:-10],
        [cancel.trailingAnchor constraintEqualToAnchor:v.trailingAnchor constant:-8],
        [cancel.centerYAnchor constraintEqualToAnchor:revert.centerYAnchor],
        [ok.trailingAnchor constraintEqualToAnchor:cancel.leadingAnchor constant:-8],
        [ok.centerYAnchor constraintEqualToAnchor:revert.centerYAnchor],
        [applyB.trailingAnchor constraintEqualToAnchor:ok.leadingAnchor constant:-8],
        [applyB.centerYAnchor constraintEqualToAnchor:revert.centerYAnchor],
    ]];
    [self recheck];
    return self;
}

- (void)show {
    [_window center];
    [_window makeKeyAndOrderFront:nil];
    [_window makeFirstResponder:_text];
}

- (void)close {
    [_check invalidate];
    _check = nil;
    _hooks = {};
    [_window close];
}

- (BOOL)isOpen { return _window.isVisible; }

- (std::string)source { return _text.string.UTF8String ?: ""; }

- (void)updateStatus {
    NSString* all = _text.string;
    const NSUInteger loc = MIN(_text.selectedRange.location, all.length);
    const char* before = [all substringToIndex:loc].UTF8String ?: "";
    const std::string b(before);
    const std::string s = jsp::editor_status(jsp::text_pos(b, b.size()), _syntax, _applied) +
                          "   \u00b7   \u2318S: apply   \u2318\u21a9: apply and close   Esc: close";
    _status.stringValue = ns(s);
}

- (void)recheck {
    if (_hooks.check) _syntax = _hooks.check([self source]);
    [self updateStatus];
}

- (BOOL)apply:(BOOL)close {
    if (!_hooks.apply) return NO;
    _applied = _hooks.apply([self source]);
    [self recheck];
    const BOOL ok = _applied.rfind("Applied", 0) == 0;
    if (ok && close) [self close];
    return ok;
}

- (void)applyOnly:(id)sender { [self apply:NO]; }
- (void)applyClose:(id)sender { [self apply:YES]; }
- (void)cancel:(id)sender { [self close]; }
- (void)revert:(id)sender {
    _text.string = _original;
    [self apply:NO];
}

- (void)textDidChange:(NSNotification*)n {
    [_check invalidate];
    __weak FooJsPanelEditor* weakSelf = self;
    _check = [NSTimer scheduledTimerWithTimeInterval:0.4 repeats:NO block:^(NSTimer*) { [weakSelf recheck]; }];
}

- (void)textViewDidChangeSelection:(NSNotification*)n { [self updateStatus]; }

- (void)windowWillClose:(NSNotification*)n {
    [_check invalidate];
    _check = nil;
}
@end

@class FooJsPanelView;

namespace {

class MacSurface : public jsp::Surface {
public:
    __weak FooJsPanelView* view = nil;
    std::map<int, NSTimer*> timers;
    FooJsPanelEditor* editor = nil;

    ~MacSurface() override {
        for (auto& [id, t] : timers) [t invalidate];
        [editor close];
    }
    void open_editor(const std::string& title, const std::string& text, jsp::EditorHooks hooks) override {
        if (editor && [editor isOpen]) {
            [editor show];
            return;
        }
        [editor close];
        editor = [[FooJsPanelEditor alloc] initWithTitle:ns(title) text:ns(text) hooks:std::move(hooks)];
        [editor show];
    }
    void invalidate() override;
    void invalidate_rect(const pui::gfx::Rect& r) override;
    void start_timer(int id, int ms) override;
    void stop_timer(int id) override {
        auto it = timers.find(id);
        if (it == timers.end()) return;
        [it->second invalidate];
        timers.erase(it);
    }
    std::string choose_script_file() override {
        NSOpenPanel* p = [NSOpenPanel openPanel];
        p.canChooseFiles = YES;
        p.canChooseDirectories = NO;
        p.allowsMultipleSelection = NO;
        p.allowedFileTypes = @[ @"js" ];
        p.title = @"Choose a JS Panel script";
        if ([p runModal] != NSModalResponseOK || !p.URL.fileSystemRepresentation) return {};
        return p.URL.fileSystemRepresentation;
    }
    int popup_menu(int x, int y, const std::vector<std::string>& items) override;
    bool key_pressed(int vk) override {
        const NSEventModifierFlags m = NSEvent.modifierFlags;
        switch (vk) {
        case 0x10: return (m & NSEventModifierFlagShift) != 0;
        case 0x11: return (m & (NSEventModifierFlagControl | NSEventModifierFlagCommand)) != 0;
        case 0x12: return (m & NSEventModifierFlagOption) != 0;
        default: return false;
        }
    }
};

}

@interface FooJsPanelView : NSView
@end

@implementation FooJsPanelView {
    MacSurface _surface;
    std::unique_ptr<jsp::Panel> _panel;
    NSTrackingArea* _tracking;
    BOOL _fixedScript;
}

- (instancetype)initWithScript:(NSString*)script {
    if ((self = [super initWithFrame:NSMakeRect(0, 0, 200, 120)])) {
        _surface.view = self;
        _fixedScript = script.length > 0;
        const std::string path = _fixedScript ? std::string(script.UTF8String) : jsp::default_script();
        const BOOL fixed = _fixedScript;
        _panel = std::make_unique<jsp::Panel>(_surface, path, [fixed](const std::string& p) {
            if (!fixed) jsp::set_default_script(p);
        });
    }
    return self;
}

- (void)dealloc {
    [_surface.editor close];
    _surface.editor = nil;
    _panel.reset();
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)keyDown:(NSEvent*)e {
    NSString* chars = e.charactersIgnoringModifiers;
    const char32_t ch = chars.length ? (char32_t)[chars characterAtIndex:0] : 0;
    const int vk = jsp::mac_key_to_vk((int)e.keyCode, ch);
    bool handled = false;
    if (_panel && vk) jsp::log::guarded("js", "key", [&] { handled = _panel->key_down(vk); });
    if (!handled) [super keyDown:e];
}
- (BOOL)acceptsFirstMouse:(NSEvent*)e { return YES; }

- (void)setFrameSize:(NSSize)s {
    [super setFrameSize:s];
    if (_panel) _panel->resize((int)s.width, (int)s.height);
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirty {
    const NSSize b = self.bounds.size;
    const int w = (int)b.width, h = (int)b.height;
    if (w <= 0 || h <= 0 || !_panel) return;
    double scale = [self convertSizeToBacking:NSMakeSize(1, 1)].width;
    if (!(scale > 0)) scale = self.window ? self.window.backingScaleFactor : 1.0;
    pui::gfx::CGCanvas cv(w, h, scale);
    jsp::log::guarded("js", "paint", [&] { _panel->paint(cv); });
    CGImageRef img = cv.copy_image();
    if (!img) return;
    pui::gfx::draw_cgimage(NSGraphicsContext.currentContext.CGContext, img, CGRectMake(0, 0, w, h), false);
    CGImageRelease(img);
}

- (void)viewDidChangeBackingProperties {
    [super viewDidChangeBackingProperties];
    [self setNeedsDisplay:YES];
}

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (_tracking) [self removeTrackingArea:_tracking];
    _tracking = [[NSTrackingArea alloc] initWithRect:NSZeroRect
                                             options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited |
                                                     NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect
                                               owner:self
                                            userInfo:nil];
    [self addTrackingArea:_tracking];
}

- (NSPoint)local:(NSEvent*)e { return [self convertPoint:e.locationInWindow fromView:nil]; }

- (unsigned)mask:(NSEvent*)e {
    const NSUInteger pressed = NSEvent.pressedMouseButtons;
    unsigned m = 0;
    if (pressed & 1) m |= 0x1;
    if (pressed & 2) m |= 0x2;
    if (e.modifierFlags & NSEventModifierFlagShift) m |= 0x4;
    if (e.modifierFlags & (NSEventModifierFlagControl | NSEventModifierFlagCommand)) m |= 0x8;
    return m;
}

- (void)send:(jsp::Mouse)kind event:(NSEvent*)e {
    if (!_panel) return;
    const NSPoint p = [self local:e];
    const unsigned m = [self mask:e];
    jsp::log::guarded("js", "mouse", [&] { _panel->mouse(kind, (int)p.x, (int)p.y, m); });
}

- (void)mouseMoved:(NSEvent*)e { [self send:jsp::Mouse::Move event:e]; }
- (void)mouseDragged:(NSEvent*)e { [self send:jsp::Mouse::Move event:e]; }
- (void)mouseExited:(NSEvent*)e { [self send:jsp::Mouse::Leave event:e]; }
- (void)mouseDown:(NSEvent*)e {
    [self.window makeFirstResponder:self];
    [self send:e.clickCount == 2 ? jsp::Mouse::LeftDouble : jsp::Mouse::LeftDown event:e];
}
- (void)mouseUp:(NSEvent*)e { [self send:jsp::Mouse::LeftUp event:e]; }
- (void)rightMouseDown:(NSEvent*)e {
    [self.window makeFirstResponder:self];
    [self send:jsp::Mouse::RightDown event:e];
}
- (void)rightMouseUp:(NSEvent*)e { [self send:jsp::Mouse::RightUp event:e]; }

- (void)scrollWheel:(NSEvent*)e {
    if (!_panel) return;
    const CGFloat dy = e.hasPreciseScrollingDeltas ? e.scrollingDeltaY / 10.0 : e.scrollingDeltaY;
    const int step = dy > 0 ? (int)std::ceil(dy) : (int)std::floor(dy);
    if (step != 0) jsp::log::guarded("js", "wheel", [&] { _panel->wheel(step); });
}

- (void)fireTimer:(int)tid {
    if (_panel) jsp::log::guarded("js", "timer", [&] { _panel->timer(tid); });
}

@end

namespace {

void MacSurface::invalidate() { [view setNeedsDisplay:YES]; }

void MacSurface::invalidate_rect(const pui::gfx::Rect& r) {
    [view setNeedsDisplayInRect:NSMakeRect(r.x, r.y, r.w, r.h)];
}

void MacSurface::start_timer(int id, int ms) {
    stop_timer(id);
    __weak FooJsPanelView* v = view;
    NSTimer* t = [NSTimer timerWithTimeInterval:ms / 1000.0 repeats:YES block:^(NSTimer*) { [v fireTimer:id]; }];
    [[NSRunLoop mainRunLoop] addTimer:t forMode:NSRunLoopCommonModes];
    timers[id] = t;
}

int MacSurface::popup_menu(int x, int y, const std::vector<std::string>& items) {
    FooJsPanelMenuTarget* target = [FooJsPanelMenuTarget new];
    NSMenu* m = [[NSMenu alloc] initWithTitle:@""];
    m.autoenablesItems = NO;
    for (size_t i = 0; i < items.size(); ++i) {
        NSMenuItem* mi = [[NSMenuItem alloc] initWithTitle:ns(items[i]) action:@selector(picked:) keyEquivalent:@""];
        mi.target = target;
        mi.tag = (NSInteger)(i + 1);
        [m addItem:mi];
    }
    target.chosen = 0;
    [m popUpMenuPositioningItem:nil atLocation:NSMakePoint(x, y) inView:view];
    return (int)target.chosen - 1;
}

}

@interface FooJsPanelController : NSViewController
@property (nonatomic, copy) NSString* script;
@end

@implementation FooJsPanelController
- (void)loadView { self.view = [[FooJsPanelView alloc] initWithScript:self.script]; }
@end

namespace {

class ui_element_mac_js : public ui_element_mac {
public:
    service_ptr instantiate(service_ptr arg) override {
        FooJsPanelController* c = [FooJsPanelController new];
        id dict = arg.is_valid() ? fb2k::unwrapNSObject(arg) : nil;
        if ([dict isKindOfClass:[NSDictionary class]]) {
            id s = ((NSDictionary*)dict)[@(js_panel::script_key)];
            if ([s isKindOfClass:[NSString class]]) c.script = s;
        }
        return fb2k::wrapNSObject(c);
    }
    bool match_name(const char* name) override {
        return name && (strcasecmp(name, js_panel::element_name) == 0 || strcasecmp(name, "foo_js_panel") == 0);
    }
    fb2k::stringRef get_name() override { return fb2k::makeString(js_panel::element_name); }
    GUID get_guid() override { return js_panel::element_guid; }
};
FB2K_SERVICE_FACTORY(ui_element_mac_js);

}

namespace jsp::ui {

std::string os_version() {
    NSOperatingSystemVersion v = [NSProcessInfo processInfo].operatingSystemVersion;
    return std::to_string(v.majorVersion) + "." + std::to_string(v.minorVersion) + "." + std::to_string(v.patchVersion);
}

std::string wine_version() { return {}; }

std::string home_dir() {
    NSString* h = NSHomeDirectory();
    return h.fileSystemRepresentation ? h.fileSystemRepresentation : "";
}

bool copy_to_clipboard(const std::string& text) {
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    [pb clearContents];
    return [pb setString:ns(text) forType:NSPasteboardTypeString];
}

}
