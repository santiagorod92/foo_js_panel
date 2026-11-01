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
    NSCursor* cursor = nil;

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
    int popup_menu(int x, int y, const jsp::Menu& menu) override;
    void set_cursor(int idc) override;
    bool input_box(const std::string& prompt, const std::string& caption, std::string& value) override;
    int message_box(const std::string& text, const std::string& caption, int type) override;
    int window_state() override;
    void set_window_state(int state) override;
    bool fullscreen() override;
    void set_fullscreen(bool on) override;
    void set_window_limits(int minW, int minH, int maxW, int maxH) override;
    pui::gfx::Rect caption{};
    void set_caption(const pui::gfx::Rect& r) override { caption = r; }
    bool in_caption(NSEvent* e);
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

- (int)vk:(NSEvent*)e {
    NSString* chars = e.charactersIgnoringModifiers;
    const char32_t ch = chars.length ? (char32_t)[chars characterAtIndex:0] : 0;
    return jsp::mac_key_to_vk((int)e.keyCode, ch);
}

- (void)keyDown:(NSEvent*)e {
    const int vk = [self vk:e];
    bool handled = false;
    if (_panel && vk) jsp::log::guarded("js", "key", [&] { handled = _panel->key_down(vk); });
    const bool command = (e.modifierFlags & NSEventModifierFlagCommand) != 0;
    NSString* typed = e.characters;
    if (_panel && typed.length) {
        jsp::log::guarded("js", "char", [&] {
            [typed enumerateSubstringsInRange:NSMakeRange(0, typed.length)
                                      options:NSStringEnumerationByComposedCharacterSequences
                                   usingBlock:^(NSString* sub, NSRange, NSRange, BOOL*) {
                                       NSData* d = [sub dataUsingEncoding:NSUTF32LittleEndianStringEncoding];
                                       const auto* cps = static_cast<const uint32_t*>(d.bytes);
                                       for (NSUInteger i = 0; i < d.length / 4; ++i)
                                           if (_panel) _panel->char_input(jsp::mac_char_to_wm_char((char32_t)cps[i], command));
                                   }];
        });
    }
    if (!handled) [super keyDown:e];
}

- (void)keyUp:(NSEvent*)e {
    const int vk = [self vk:e];
    bool handled = false;
    if (_panel && vk) jsp::log::guarded("js", "key", [&] { handled = _panel->key_up(vk); });
    if (!handled) [super keyUp:e];
}

- (BOOL)becomeFirstResponder {
    if (_panel) jsp::log::guarded("js", "focus", [&] { _panel->focus(true); });
    return YES;
}

- (BOOL)resignFirstResponder {
    if (_panel) jsp::log::guarded("js", "focus", [&] { _panel->focus(false); });
    return YES;
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
                                             options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited | NSTrackingCursorUpdate |
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
    if (pressed & 4) m |= 0x10;
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
- (BOOL)mouseDownCanMoveWindow { return NO; }
- (void)mouseDown:(NSEvent*)e {
    if (_surface.in_caption(e)) {
        if (e.clickCount == 2) [self.window zoom:nil];
        else [self.window performWindowDragWithEvent:e];
        return;
    }
    [self.window makeFirstResponder:self];
    [self send:e.clickCount == 2 ? jsp::Mouse::LeftDouble : jsp::Mouse::LeftDown event:e];
}
- (void)mouseUp:(NSEvent*)e { [self send:jsp::Mouse::LeftUp event:e]; }
- (void)rightMouseDown:(NSEvent*)e {
    [self.window makeFirstResponder:self];
    [self send:jsp::Mouse::RightDown event:e];
}
- (void)rightMouseUp:(NSEvent*)e { [self send:jsp::Mouse::RightUp event:e]; }
- (void)otherMouseDown:(NSEvent*)e {
    if (e.buttonNumber != 2) return [super otherMouseDown:e];
    [self.window makeFirstResponder:self];
    [self send:e.clickCount == 2 ? jsp::Mouse::MiddleDouble : jsp::Mouse::MiddleDown event:e];
}
- (void)otherMouseUp:(NSEvent*)e {
    if (e.buttonNumber != 2) return [super otherMouseUp:e];
    [self send:jsp::Mouse::MiddleUp event:e];
}
- (void)cursorUpdate:(NSEvent*)e { [_surface.cursor ?: NSCursor.arrowCursor set]; }

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

NSMenu* build_menu(const jsp::Menu& items, FooJsPanelMenuTarget* target) {
    NSMenu* m = [[NSMenu alloc] initWithTitle:@""];
    m.autoenablesItems = NO;
    for (const jsp::MenuItem& it : items) {
        if (it.separator) {
            [m addItem:[NSMenuItem separatorItem]];
            continue;
        }
        const std::string title = it.text.substr(0, it.text.find('\t'));
        NSMenuItem* mi = [[NSMenuItem alloc] initWithTitle:ns(title) action:it.submenu ? nil : @selector(picked:) keyEquivalent:@""];
        mi.target = target;
        mi.tag = it.id;
        mi.enabled = !it.disabled;
        mi.state = it.checked ? NSControlStateValueOn : NSControlStateValueOff;
        if (it.submenu) mi.submenu = build_menu(it.children, target);
        [m addItem:mi];
    }
    return m;
}

int MacSurface::popup_menu(int x, int y, const jsp::Menu& menu) {
    FooJsPanelMenuTarget* target = [FooJsPanelMenuTarget new];
    NSMenu* m = build_menu(menu, target);
    target.chosen = 0;
    [m popUpMenuPositioningItem:nil atLocation:NSMakePoint(x, y) inView:view];
    return (int)target.chosen;
}

NSCursor* cursor_for(int idc) {
    switch (idc) {
    case 32513: return NSCursor.IBeamCursor;
    case 32515: return NSCursor.crosshairCursor;
    case 32644: return NSCursor.resizeLeftRightCursor;
    case 32645: return NSCursor.resizeUpDownCursor;
    case 32646: return NSCursor.openHandCursor;
    case 32648: return NSCursor.operationNotAllowedCursor;
    case 32649: return NSCursor.pointingHandCursor;
    default: return NSCursor.arrowCursor;
    }
}

void MacSurface::set_cursor(int idc) {
    cursor = cursor_for(idc);
    [cursor set];
}

int MacSurface::window_state() {
    NSWindow* w = view.window;
    return !w ? 0 : w.miniaturized ? 1 : w.zoomed ? 2 : 0;
}

void MacSurface::set_window_state(int state) {
    NSWindow* w = view.window;
    if (!w) return;
    if (state == 1) {
        [w miniaturize:nil];
        return;
    }
    if (w.miniaturized) [w deminiaturize:nil];
    if ((state == 2) != (bool)w.zoomed) [w zoom:nil];
}

bool MacSurface::in_caption(NSEvent* e) {
    NSWindow* w = view.window;
    NSView* content = w.contentView;
    if (!w || !content || caption.w <= 0 || caption.h <= 0 || fullscreen()) return false;
    NSPoint p = [content convertPoint:e.locationInWindow fromView:nil];
    if (!content.isFlipped) p.y = NSHeight(content.bounds) - p.y;
    return p.x >= caption.x && p.x < caption.x + caption.w && p.y >= caption.y && p.y < caption.y + caption.h;
}

bool MacSurface::fullscreen() {
    NSWindow* w = view.window;
    return w && (w.styleMask & NSWindowStyleMaskFullScreen) != 0;
}

void MacSurface::set_fullscreen(bool on) {
    NSWindow* w = view.window;
    if (w && on != fullscreen()) [w toggleFullScreen:nil];
}

void MacSurface::set_window_limits(int minW, int minH, int maxW, int maxH) {
    NSWindow* w = view.window;
    if (!w) return;
    w.contentMinSize = NSMakeSize(minW > 0 ? minW : 100, minH > 0 ? minH : 100);
    w.contentMaxSize = NSMakeSize(maxW > 0 ? maxW : FLT_MAX, maxH > 0 ? maxH : FLT_MAX);
    if (w.zoomed || fullscreen()) return;
    NSRect content = [w contentRectForFrameRect:w.frame];
    NSSize s = content.size;
    if (minW > 0) s.width = std::max<CGFloat>(s.width, minW);
    if (minH > 0) s.height = std::max<CGFloat>(s.height, minH);
    if (maxW > 0) s.width = std::min<CGFloat>(s.width, maxW);
    if (maxH > 0) s.height = std::min<CGFloat>(s.height, maxH);
    if (NSEqualSizes(s, content.size)) return;
    const CGFloat top = NSMaxY(content);
    content.size = s;
    content.origin.y = top - s.height;
    [w setFrame:[w frameRectForContentRect:content] display:YES];
}

int MacSurface::message_box(const std::string& text, const std::string& caption, int type) {
    static const std::vector<std::pair<NSString*, int>> sets[] = {
        { { @"OK", 1 } },
        { { @"OK", 1 }, { @"Cancel", 2 } },
        { { @"Abort", 3 }, { @"Retry", 4 }, { @"Ignore", 5 } },
        { { @"Yes", 6 }, { @"No", 7 }, { @"Cancel", 2 } },
        { { @"Yes", 6 }, { @"No", 7 } },
        { { @"Retry", 4 }, { @"Cancel", 2 } },
    };
    const auto& buttons = sets[(type & 0xF) < 6 ? (type & 0xF) : 0];
    NSAlert* alert = [NSAlert new];
    alert.messageText = ns(caption.empty() ? std::string("JS Panel") : caption);
    alert.informativeText = ns(text);
    if (type & 0x10) alert.alertStyle = NSAlertStyleCritical;
    for (const auto& b : buttons) [alert addButtonWithTitle:b.first];
    const NSModalResponse r = [alert runModal];
    const long index = (long)r - (long)NSAlertFirstButtonReturn;
    return index >= 0 && index < (long)buttons.size() ? buttons[(size_t)index].second : buttons.back().second;
}

bool MacSurface::input_box(const std::string& prompt, const std::string& caption, std::string& value) {
    NSAlert* alert = [NSAlert new];
    alert.messageText = ns(caption.empty() ? std::string("JS Panel") : caption);
    alert.informativeText = ns(prompt);
    [alert addButtonWithTitle:@"OK"];
    [alert addButtonWithTitle:@"Cancel"];
    NSTextField* field = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 320, 24)];
    field.stringValue = ns(value);
    alert.accessoryView = field;
    alert.window.initialFirstResponder = field;
    if ([alert runModal] != NSAlertFirstButtonReturn) return false;
    value = field.stringValue.UTF8String ?: "";
    return true;
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

std::string app_dir() {
    NSString* b = [NSBundle mainBundle].bundlePath;
    return b.fileSystemRepresentation ? std::string(b.fileSystemRepresentation) + "/" : std::string();
}

std::string clipboard_text() {
    NSString* s = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
    return s.UTF8String ? s.UTF8String : "";
}

bool shell_run(const std::string& command) {
    std::string cmd = command;
    while (!cmd.empty() && (cmd.front() == ' ' || cmd.front() == '"')) cmd.erase(0, 1);
    const auto q = cmd.find('"');
    if (q != std::string::npos) cmd.erase(q);
    while (!cmd.empty() && cmd.back() == ' ') cmd.pop_back();
    if (cmd.empty()) return false;
    if (cmd.find("://") != std::string::npos || cmd.rfind("mailto:", 0) == 0) {
        NSURL* url = [NSURL URLWithString:ns(cmd)];
        return url && [[NSWorkspace sharedWorkspace] openURL:url];
    }
    NSString* path = ns(cmd);
    if (![[NSFileManager defaultManager] fileExistsAtPath:path]) return false;
    return [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:path]];
}

bool copy_to_clipboard(const std::string& text) {
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    [pb clearContents];
    return [pb setString:ns(text) forType:NSPasteboardTypeString];
}

}
