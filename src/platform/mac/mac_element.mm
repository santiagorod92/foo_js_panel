#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#include "../../fb2k.h"
#include "platform/mac/mac_canvas.h"
#include "../../core/editor_console.h"
#include "../../core/js_api_util.h"
#include "../../core/js_highlight.h"
#include "../../core/js_panel_api.h"
#include "../../core/log.h"
#include "../../core/panel.h"
#include "../../core/windowless.h"
#include "../../core/platform.h"
#include "../../core/platform_tracks.h"
#include "../../core/settings.h"
#include <map>
#include <memory>

namespace {

NSString* ns(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()] ?: @""; }

NSString* const kTracksType = @(js_panel::drag_type_tracks);

NSArray<NSPasteboardType>* track_types() { return @[ kTracksType, NSPasteboardTypeFileURL ]; }

NSArray<NSPasteboardItem*>* pasteboard_items(metadb_handle_list_cref tracks) {
    NSMutableArray<NSString*>* lines = [NSMutableArray array];
    for (size_t i = 0; i < tracks.get_count(); ++i)
        [lines addObject:[NSString stringWithFormat:@"%u\t%@", tracks[i]->get_subsong_index(), ns(tracks[i]->get_path())]];
    NSMutableArray<NSPasteboardItem*>* items = [NSMutableArray array];
    for (size_t i = 0; i < tracks.get_count(); ++i) {
        NSPasteboardItem* item = [NSPasteboardItem new];
        if (i == 0) [item setString:[lines componentsJoinedByString:@"\n"] forType:kTracksType];
        const char* path = tracks[i]->get_path();
        if (strncmp(path, "file://", 7) == 0) {
            NSURL* url = [NSURL fileURLWithPath:ns(filesystem::g_get_native_path(path).c_str())];
            [item setString:url.absoluteString forType:NSPasteboardTypeFileURL];
        }
        if (item.types.count) [items addObject:item];
    }
    return items;
}

struct PasteboardTracks {
    metadb_handle_list tracks;
    std::vector<std::string> files;
};

PasteboardTracks pasteboard_tracks(NSPasteboard* pb) {
    PasteboardTracks out;
    if (NSString* joined = [pb stringForType:kTracksType]) {
        auto api = metadb::get();
        for (NSString* line in [joined componentsSeparatedByString:@"\n"]) {
            const NSRange tab = [line rangeOfString:@"\t"];
            if (tab.location == NSNotFound) continue;
            const char* path = [line substringFromIndex:tab.location + 1].UTF8String;
            if (path && *path) out.tracks.add_item(api->handle_create(path, (uint32_t)[line substringToIndex:tab.location].integerValue));
        }
        return out;
    }
    NSArray<NSURL*>* urls = [pb readObjectsForClasses:@[ NSURL.class ] options:@{ NSPasteboardURLReadingFileURLsOnlyKey: @YES }];
    for (NSURL* u in urls)
        if (const char* p = u.fileSystemRepresentation) out.files.emplace_back(p);
    return out;
}

NSImage* drag_image(size_t count) {
    NSString* text = count == 1 ? @"1 track" : [NSString stringWithFormat:@"%zu tracks", count];
    NSDictionary* attrs = @{ NSFontAttributeName: [NSFont boldSystemFontOfSize:12], NSForegroundColorAttributeName: NSColor.whiteColor };
    const NSSize ts = [text sizeWithAttributes:attrs];
    return [NSImage imageWithSize:NSMakeSize(std::ceil(ts.width) + 16, std::ceil(ts.height) + 8)
                          flipped:NO
                   drawingHandler:^BOOL(NSRect r) {
                       [[NSColor colorWithCalibratedWhite:0.15 alpha:0.85] setFill];
                       [[NSBezierPath bezierPathWithRoundedRect:r xRadius:6 yRadius:6] fill];
                       [text drawAtPoint:NSMakePoint(8, 4) withAttributes:attrs];
                       return YES;
                   }];
}

int drop_effects(NSDragOperation ops) {
    return ((ops & NSDragOperationCopy) ? 1 : 0) | ((ops & (NSDragOperationMove | NSDragOperationGeneric)) ? 2 : 0) |
           ((ops & NSDragOperationLink) ? 4 : 0);
}

NSDragOperation drag_operation(int effect) {
    return (effect & 2) ? NSDragOperationMove : (effect & 1) ? NSDragOperationCopy : (effect & 4) ? NSDragOperationLink : NSDragOperationNone;
}

unsigned key_mask(NSEventModifierFlags f) {
    return 0x1 | ((f & NSEventModifierFlagShift) ? 0x4 : 0) | ((f & (NSEventModifierFlagControl | NSEventModifierFlagCommand)) ? 0x8 : 0) |
           ((f & NSEventModifierFlagOption) ? 0x20 : 0);
}

}

@interface FooJsPanelMenuTarget : NSObject
@property (nonatomic, assign) NSInteger chosen;
- (void)picked:(NSMenuItem*)item;
@end

@implementation FooJsPanelMenuTarget
- (void)picked:(NSMenuItem*)item { self.chosen = item.tag; }
@end

@interface FooJsPanelColourDelegate : NSObject <NSWindowDelegate>
@end

@implementation FooJsPanelColourDelegate
- (void)windowWillClose:(NSNotification*)n { [NSApp stopModal]; }
@end

namespace {

jsp::DialogValue dialog_value(id v, int depth) {
    jsp::DialogValue d;
    if (depth > 16) return d;
    if ([v isKindOfClass:NSString.class]) {
        d.kind = jsp::DialogValue::Kind::String;
        d.text = [(NSString*)v UTF8String] ?: "";
    } else if ([v isKindOfClass:NSNumber.class]) {
        const bool boolean = CFGetTypeID((__bridge CFTypeRef)v) == CFBooleanGetTypeID();
        d.kind = boolean ? jsp::DialogValue::Kind::Bool : jsp::DialogValue::Kind::Number;
        d.number = [(NSNumber*)v doubleValue];
    } else if ([v isKindOfClass:NSArray.class]) {
        d.kind = jsp::DialogValue::Kind::Array;
        for (id item in (NSArray*)v) d.items.push_back(dialog_value(item, depth + 1));
    } else if ([v isKindOfClass:NSDictionary.class]) {
        d.kind = jsp::DialogValue::Kind::Object;
        for (id key in (NSDictionary*)v) {
            d.keys.push_back([[key description] UTF8String] ?: "");
            d.items.push_back(dialog_value(((NSDictionary*)v)[key], depth + 1));
        }
    }
    return d;
}

std::string js_literal(const jsp::DialogValue& d) {
    switch (d.kind) {
    case jsp::DialogValue::Kind::Bool: return d.number != 0 ? "true" : "false";
    case jsp::DialogValue::Kind::Number: {
        if (!std::isfinite(d.number)) return "null";
        char buf[40];
        snprintf(buf, sizeof buf, "%.17g", d.number);
        return buf;
    }
    case jsp::DialogValue::Kind::String: {
        std::string out = "\"";
        for (size_t i = 0; i < d.text.size(); ++i) {
            const unsigned char c = (unsigned char)d.text[i];
            if (c == '"' || c == '\\') out += std::string("\\") + (char)c;
            else if (c < 0x20) {
                char buf[8];
                snprintf(buf, sizeof buf, "\\u%04x", c);
                out += buf;
            } else if (c == 0xE2 && i + 2 < d.text.size() && (unsigned char)d.text[i + 1] == 0x80 &&
                       ((unsigned char)d.text[i + 2] == 0xA8 || (unsigned char)d.text[i + 2] == 0xA9)) {
                out += (unsigned char)d.text[i + 2] == 0xA8 ? "\\u2028" : "\\u2029";
                i += 2;
            } else {
                out += (char)c;
            }
        }
        return out + "\"";
    }
    case jsp::DialogValue::Kind::Array: {
        std::string out = "[";
        for (size_t i = 0; i < d.items.size(); ++i) out += (i ? "," : "") + js_literal(d.items[i]);
        return out + "]";
    }
    case jsp::DialogValue::Kind::Object: {
        std::string out = "{";
        for (size_t i = 0; i < d.items.size() && i < d.keys.size(); ++i) {
            jsp::DialogValue key;
            key.kind = jsp::DialogValue::Kind::String;
            key.text = d.keys[i];
            out += (i ? "," : "") + js_literal(key) + ":" + js_literal(d.items[i]);
        }
        return out + "}";
    }
    case jsp::DialogValue::Kind::Function: return "__jsp_fn(" + std::to_string(d.fn) + ")";
    default: return "null";
    }
}

}

@interface FooJsPanelHtmlDialog : NSObject <WKScriptMessageHandler, WKNavigationDelegate, NSWindowDelegate>
@property (nonatomic, strong) NSPanel* panel;
@property (nonatomic, assign) const jsp::DialogCall* call;
@property (nonatomic, assign) BOOL done;
@end

@implementation FooJsPanelHtmlDialog
- (void)userContentController:(WKUserContentController*)c didReceiveScriptMessage:(WKScriptMessage*)m {
    NSDictionary* b = [m.body isKindOfClass:NSDictionary.class] ? m.body : nil;
    NSString* op = b[@"op"];
    if ([op isEqualToString:@"close"]) {
        [self.panel close];
    } else if ([op isEqualToString:@"resize"]) {
        const NSRect f = self.panel.frame;
        NSRect r = [self.panel frameRectForContentRect:NSMakeRect(0, 0, std::max(100.0, [b[@"w"] doubleValue]), std::max(60.0, [b[@"h"] doubleValue]))];
        r.origin = NSMakePoint(f.origin.x, NSMaxY(f) - r.size.height);
        [self.panel setFrame:r display:YES];
    } else if ([op isEqualToString:@"call"] && self.call && *self.call) {
        std::vector<jsp::DialogValue> args;
        if ([b[@"args"] isKindOfClass:NSArray.class])
            for (id a in (NSArray*)b[@"args"]) args.push_back(dialog_value(a, 0));
        (*self.call)([b[@"fn"] intValue], args);
    }
}
- (void)webView:(WKWebView*)w didFinishNavigation:(WKNavigation*)n {
    if (w.title.length) self.panel.title = w.title;
}
- (void)windowWillClose:(NSNotification*)n { self.done = YES; }
@end

@interface FooJsPanelLineRuler : NSRulerView
@property (nonatomic, assign) NSInteger errorLine;
@end

@implementation FooJsPanelLineRuler
- (CGFloat)requiredThickness { return 46; }
- (void)drawHashMarksAndLabelsInRect:(NSRect)rect {
    NSTextView* tv = (NSTextView*)self.clientView;
    if (![tv isKindOfClass:[NSTextView class]]) return;
    [NSColor.windowBackgroundColor setFill];
    NSRectFill(self.bounds);
    NSLayoutManager* lm = tv.layoutManager;
    NSString* text = tv.string;
    const NSRect visible = tv.visibleRect;
    const NSRange glyphs = [lm glyphRangeForBoundingRect:visible inTextContainer:tv.textContainer];
    const NSRange chars = [lm characterRangeForGlyphRange:glyphs actualGlyphRange:nil];
    NSInteger line = 1;
    for (NSUInteger i = 0; i < chars.location && i < text.length; ++i)
        if ([text characterAtIndex:i] == '\n') ++line;
    NSFont* font = [NSFont monospacedDigitSystemFontOfSize:11 weight:NSFontWeightRegular];
    NSUInteger at = chars.location;
    while (at <= NSMaxRange(chars) && at <= text.length) {
        const NSUInteger glyph = at < text.length ? [lm glyphIndexForCharacterAtIndex:at] : lm.numberOfGlyphs;
        NSRect r = at < text.length || text.length == 0 ? [lm lineFragmentRectForGlyphAtIndex:MIN(glyph, MAX(lm.numberOfGlyphs, 1) - 1) effectiveRange:nil]
                                                     : lm.extraLineFragmentRect;
        r.origin.y += tv.textContainerOrigin.y;
        const NSPoint p = [self convertPoint:r.origin fromView:tv];
        NSDictionary* attrs = @{ NSFontAttributeName: font,
                                 NSForegroundColorAttributeName: line == self.errorLine ? NSColor.systemRedColor : NSColor.secondaryLabelColor };
        NSString* label = [NSString stringWithFormat:@"%ld", (long)line];
        const NSSize size = [label sizeWithAttributes:attrs];
        [label drawAtPoint:NSMakePoint(self.ruleThickness - size.width - 6, p.y + (NSHeight(r) - size.height) / 2) withAttributes:attrs];
        const NSRange next = [text rangeOfString:@"\n" options:0 range:NSMakeRange(at, text.length - at)];
        if (next.location == NSNotFound) break;
        at = next.location + 1;
        ++line;
    }
}
@end

@interface FooJsPanelEditorWindow : NSWindow
@property (nonatomic, copy) void (^onApply)(BOOL close);
@property (nonatomic, copy) void (^onGotoError)(void);
@property (nonatomic, copy) void (^onSwitchPane)(void);
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
    if (e.type == NSEventTypeKeyDown && e.charactersIgnoringModifiers.length &&
        [e.charactersIgnoringModifiers characterAtIndex:0] == NSF8FunctionKey) {
        if (self.onGotoError) self.onGotoError();
        return YES;
    }
    if (e.type == NSEventTypeKeyDown && e.charactersIgnoringModifiers.length &&
        [e.charactersIgnoringModifiers characterAtIndex:0] == NSF6FunctionKey && self.onSwitchPane) {
        self.onSwitchPane();
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
- (NSString*)title;
@end

@implementation FooJsPanelEditor {
    jsp::EditorHooks _hooks;
    NSString* _original;
    FooJsPanelEditorWindow* _window;
    NSTextView* _text;
    NSTextField* _status;
    FooJsPanelLineRuler* _ruler;
    std::string _syntax;
    std::string _applied;
    NSTimer* _check;
    jsp::EditorConsole _console;
    NSTextView* _output;
    NSTextField* _input;
    NSTextField* _watches;
    NSTimer* _poll;
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
    _window.onGotoError = ^{ [weakSelf gotoError]; };
    _window.onSwitchPane = ^{ [weakSelf switchPane]; };

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
    _ruler = [[FooJsPanelLineRuler alloc] initWithScrollView:scroll orientation:NSVerticalRuler];
    _ruler.clientView = _text;
    scroll.verticalRulerView = _ruler;
    scroll.hasVerticalRuler = YES;
    scroll.rulersVisible = YES;
    scroll.contentView.postsBoundsChangedNotifications = YES;
    __weak FooJsPanelLineRuler* weakRuler = _ruler;
    [[NSNotificationCenter defaultCenter] addObserverForName:NSViewBoundsDidChangeNotification object:scroll.contentView queue:nil
                                                  usingBlock:^(NSNotification*) { weakRuler.needsDisplay = YES; }];

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
    NSView* above = scroll;
    if (_hooks.evaluate) {
        _watches = [NSTextField wrappingLabelWithString:@""];
        _watches.translatesAutoresizingMaskIntoConstraints = NO;
        _watches.font = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];
        _watches.textColor = NSColor.secondaryLabelColor;
        _watches.selectable = YES;
        NSScrollView* out = [NSTextView scrollableTextView];
        out.translatesAutoresizingMaskIntoConstraints = NO;
        _output = (NSTextView*)out.documentView;
        _output.editable = NO;
        _output.richText = NO;
        _output.font = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];
        _output.string = @"Console: type an expression and press Enter (:help for the commands).\n";
        _input = [NSTextField textFieldWithString:@""];
        _input.translatesAutoresizingMaskIntoConstraints = NO;
        _input.font = [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];
        _input.placeholderString = @"expression, :watch EXPR, :unwatch N, :clear";
        _input.target = self;
        _input.action = @selector(submit:);
        for (NSView* c in @[ _watches, out, _input ]) [v addSubview:c];
        [NSLayoutConstraint activateConstraints:@[
            [_watches.topAnchor constraintEqualToAnchor:scroll.bottomAnchor constant:6],
            [_watches.leadingAnchor constraintEqualToAnchor:v.leadingAnchor constant:10],
            [_watches.trailingAnchor constraintEqualToAnchor:v.trailingAnchor constant:-10],
            [out.topAnchor constraintEqualToAnchor:_watches.bottomAnchor constant:4],
            [out.leadingAnchor constraintEqualToAnchor:v.leadingAnchor constant:8],
            [out.trailingAnchor constraintEqualToAnchor:v.trailingAnchor constant:-8],
            [out.heightAnchor constraintEqualToConstant:140],
            [_input.topAnchor constraintEqualToAnchor:out.bottomAnchor constant:4],
            [_input.leadingAnchor constraintEqualToAnchor:v.leadingAnchor constant:8],
            [_input.trailingAnchor constraintEqualToAnchor:v.trailingAnchor constant:-8],
        ]];
        above = _input;
        _poll = [NSTimer scheduledTimerWithTimeInterval:0.5 repeats:YES block:^(NSTimer*) { [weakSelf poll]; }];
        [self poll];
    }
    [NSLayoutConstraint activateConstraints:@[
        [scroll.topAnchor constraintEqualToAnchor:v.topAnchor constant:8],
        [scroll.leadingAnchor constraintEqualToAnchor:v.leadingAnchor constant:8],
        [scroll.trailingAnchor constraintEqualToAnchor:v.trailingAnchor constant:-8],
        [_status.topAnchor constraintEqualToAnchor:above.bottomAnchor constant:6],
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
    if (_hooks.line > 0) [self gotoLine:_hooks.line];
}

- (void)gotoLine:(NSInteger)line {
    const std::string all = [self source];
    const std::size_t byte = jsp::line_start(all, (int)line);
    NSString* before = [[NSString alloc] initWithBytes:all.data() length:byte encoding:NSUTF8StringEncoding] ?: @"";
    const NSRange at = NSMakeRange(MIN(before.length, _text.string.length), 0);
    _text.selectedRange = at;
    [_text scrollRangeToVisible:at];
    [_window makeFirstResponder:_text];
    [self updateStatus];
}

- (void)gotoError {
    const int line = _syntax.empty() ? _hooks.line : jsp::error_line(_syntax, "");
    if (line > 0) [self gotoLine:line];
}

- (void)appendOutput:(const std::string&)text {
    if (text.empty()) return;
    [_output.textStorage appendAttributedString:[[NSAttributedString alloc] initWithString:ns(text) attributes:@{
        NSFontAttributeName : _output.font, NSForegroundColorAttributeName : NSColor.textColor }]];
    [_output scrollRangeToVisible:NSMakeRange(_output.string.length, 0)];
}

- (void)poll {
    if (!_hooks.evaluate) return;
    if (_hooks.console_lines)
        for (const std::string& line : _hooks.console_lines()) [self appendOutput:line + "\n"];
    const auto eval = _hooks.evaluate;
    _watches.stringValue = ns(_console.watches(eval));
}

- (void)switchPane {
    if (!_input) return;
    NSResponder* r = _window.firstResponder;
    const BOOL inConsole = r == _input || r == _input.currentEditor;
    [_window makeFirstResponder:inConsole ? (NSResponder*)_text : (NSResponder*)_input];
}

- (void)submit:(id)sender {
    if (!_hooks.evaluate) return;
    [self poll];
    const jsp::ConsoleReply reply = _console.submit(_input.stringValue.UTF8String ?: "", _hooks.run ? _hooks.run : _hooks.evaluate);
    if (reply.clear) _output.string = @"";
    _input.stringValue = @"";
    [self appendOutput:reply.echo];
    [self poll];
    [self appendOutput:reply.text];
}

- (void)close {
    [_check invalidate];
    _check = nil;
    [_poll invalidate];
    _poll = nil;
    _hooks = {};
    [_window close];
}

- (BOOL)isOpen { return _window.isVisible; }

- (NSString*)title { return _window.title; }

- (std::string)source { return _text.string.UTF8String ?: ""; }

- (void)updateStatus {
    NSString* all = _text.string;
    const NSUInteger loc = MIN(_text.selectedRange.location, all.length);
    const char* before = [all substringToIndex:loc].UTF8String ?: "";
    const std::string b(before);
    const std::string s = jsp::editor_status(jsp::text_pos(b, b.size()), _syntax, _applied) +
                          "   \u00b7   \u2318S: apply   \u2318\u21a9: apply and close   F8: go to the error   Esc: close";
    _status.stringValue = ns(s);
}

- (void)highlight {
    NSLayoutManager* lm = _text.layoutManager;
    const NSUInteger length = _text.string.length;
    [lm removeTemporaryAttribute:NSForegroundColorAttributeName forCharacterRange:NSMakeRange(0, length)];
    NSColor* const colours[] = { NSColor.systemPinkColor, NSColor.systemPurpleColor, NSColor.systemRedColor,
                                 NSColor.systemBlueColor, NSColor.secondaryLabelColor, NSColor.systemOrangeColor };
    for (const jsp::Token& t : jsp::highlight_js([self source]))
        if (t.start + t.length <= length)
            [lm addTemporaryAttribute:NSForegroundColorAttributeName value:colours[(int)t.kind] forCharacterRange:NSMakeRange(t.start, t.length)];
}

- (void)recheck {
    [self highlight];
    if (_hooks.check) _syntax = _hooks.check([self source]);
    _ruler.errorLine = _syntax.empty() ? 0 : jsp::error_line(_syntax, "");
    _ruler.needsDisplay = YES;
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
    _ruler.needsDisplay = YES;
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
    __weak NSView* view = nil;
    std::map<int, NSTimer*> timers;
    FooJsPanelEditor* editor = nil;
    NSCursor* cursor = nil;

    ~MacSurface() override {
        for (auto& [id, t] : timers) [t invalidate];
        [editor close];
    }
    void open_editor(const std::string& title, const std::string& text, jsp::EditorHooks hooks) override {
        if (editor && [editor isOpen] && [[editor title] isEqualToString:ns(title)]) {
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
    bool visible() override;
    bool fullscreen() override;
    void set_fullscreen(bool on) override;
    bool always_on_top() override;
    void set_always_on_top(bool on) override;
    int drag_tracks(metadb_handle_list_cref tracks, int effects) override;
    void set_tooltip(const std::string& text, bool active, int maxWidth) override;
    bool ui_colour(jsp::UiColour kind, uint32_t& argb) override;
    bool ui_font(int kind, jsp::UiFont& font) override;
    bool sys_colour(int index, uint32_t& argb) override;
    bool pick_colour(uint32_t& argb) override;
    void set_window_limits(int minW, int minH, int maxW, int maxH) override;
    bool window_rect(pui::gfx::Rect& content, pui::gfx::Rect& work) override;
    void set_window_rect(const pui::gfx::Rect& content) override;
    pui::gfx::Rect caption{};
    void set_caption(const pui::gfx::Rect& r) override { caption = r; }
    bool in_caption(NSEvent* e);
    bool caption_drag(bool double_click) override;
    bool html_dialog(const jsp::HtmlDialog& dialog, const jsp::DialogCall& call) override;
    void* native() override { return (__bridge void*)view; }
    bool key_pressed(int vk) override {
        const NSEventModifierFlags m = NSEvent.modifierFlags;
        return jsp::mac_modifier_pressed(vk, { (m & NSEventModifierFlagShift) != 0, (m & NSEventModifierFlagControl) != 0,
                                               (m & NSEventModifierFlagOption) != 0, (m & NSEventModifierFlagCommand) != 0 });
    }
};

}

static bool mac_key(jsp::Panel& panel, NSEvent* e) {
    NSString* chars = e.charactersIgnoringModifiers;
    const char32_t ch = chars.length ? (char32_t)[chars characterAtIndex:0] : 0;
    const int vk = jsp::mac_key_to_vk((int)e.keyCode, ch);
    bool handled = false;
    if (e.type == NSEventTypeKeyUp) {
        if (vk) jsp::log::guarded("js", "key", [&] { handled = panel.key_up(vk); });
        return handled;
    }
    if (e.type != NSEventTypeKeyDown) return false;
    if (vk) jsp::log::guarded("js", "key", [&] { handled = panel.key_down(vk); });
    const bool command = (e.modifierFlags & NSEventModifierFlagCommand) != 0;
    NSString* typed = e.characters;
    if (typed.length) {
        jsp::Panel* target = &panel;
        jsp::log::guarded("js", "char", [&] {
            [typed enumerateSubstringsInRange:NSMakeRange(0, typed.length)
                                      options:NSStringEnumerationByComposedCharacterSequences
                                   usingBlock:^(NSString* sub, NSRange, NSRange, BOOL*) {
                                       NSData* d = [sub dataUsingEncoding:NSUTF32LittleEndianStringEncoding];
                                       const auto* cps = static_cast<const uint32_t*>(d.bytes);
                                       for (NSUInteger i = 0; i < d.length / 4; ++i)
                                           target->char_input(jsp::mac_char_to_wm_char((char32_t)cps[i], command));
                                   }];
        });
    }
    return handled;
}

@interface FooJsPanelDragSource : NSObject <NSDraggingSource>
@property (nonatomic, assign) NSDragOperation ops;
@property (nonatomic, assign) BOOL ended;
@property (nonatomic, assign) NSDragOperation result;
@end

@implementation FooJsPanelDragSource
- (NSDragOperation)draggingSession:(NSDraggingSession*)s sourceOperationMaskForDraggingContext:(NSDraggingContext)c { return self.ops; }
- (void)draggingSession:(NSDraggingSession*)s endedAtPoint:(NSPoint)p operation:(NSDragOperation)op {
    self.result = op;
    self.ended = YES;
}
@end

namespace {

int start_drag(NSView* view, metadb_handle_list_cref tracks, int effects) {
    static FooJsPanelDragSource* source = [FooJsPanelDragSource new];
    NSEvent* e = NSApp.currentEvent;
    if (!view || !e || (e.type != NSEventTypeLeftMouseDragged && e.type != NSEventTypeLeftMouseDown)) return 0;
    NSArray<NSPasteboardItem*>* items = pasteboard_items(tracks);
    if (!items.count) return 0;
    source.ops = drag_operation(effects & 2) | drag_operation(effects & 1) | drag_operation(effects & 4);
    NSImage* image = drag_image(tracks.get_count());
    const NSPoint p = [view convertPoint:e.locationInWindow fromView:nil];
    const NSRect frame = NSMakeRect(p.x - 8, p.y - 8, image.size.width, image.size.height);
    NSMutableArray<NSDraggingItem*>* drag = [NSMutableArray array];
    for (NSPasteboardItem* item in items) {
        NSDraggingItem* d = [[NSDraggingItem alloc] initWithPasteboardWriter:item];
        [d setDraggingFrame:frame contents:drag.count ? nil : image];
        [drag addObject:d];
    }
    source.ended = NO;
    source.result = NSDragOperationNone;
    @try {
        [view beginDraggingSessionWithItems:drag event:e source:source];
    } @catch (NSException* ex) {
        jsp::log::warn("js", std::string("DoDragDrop: ") + (ex.reason.UTF8String ?: "can't start the drag"));
        return 0;
    }
    NSDate* limit = [NSDate dateWithTimeIntervalSinceNow:120];
    while (!source.ended && [limit timeIntervalSinceNow] > 0) {
        NSEvent* next = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]
                                              inMode:NSEventTrackingRunLoopMode dequeue:YES];
        if (next) [NSApp sendEvent:next];
    }
    const NSDragOperation op = source.result;
    return (op & NSDragOperationMove) ? 2 : (op & NSDragOperationCopy) ? 1 : (op & NSDragOperationLink) ? 4 : 0;
}

int drag_event(jsp::Panel* panel, int& allowed, jsp::Drag kind, int x, int y, unsigned mask, id<NSDraggingInfo> s) {
    if (kind == jsp::Drag::Enter)
        allowed = [s.draggingPasteboard availableTypeFromArray:track_types()] ? drop_effects(s.draggingSourceOperationMask) : 0;
    jsp::DropAction a;
    if (kind != jsp::Drag::Leave) a.effect = allowed;
    if (panel) jsp::log::guarded("js", "drag", [&] { panel->drag(kind, x, y, mask, a); });
    else a.effect = 0;
    if (kind == jsp::Drag::Leave) return 0;
    a.effect &= allowed;
    if (kind == jsp::Drag::Drop && panel && a.effect && a.playlist >= 0) {
        jsp::log::guarded("js", "drop", [&] {
            PasteboardTracks dropped = pasteboard_tracks(s.draggingPasteboard);
            if (dropped.tracks.get_count()) panel->drop_notify(a)->on_completion(dropped.tracks);
            else panel->drop_locations(dropped.files, a);
        });
    }
    return a.effect;
}

}

@interface FooJsPanelView : NSView
@end

@implementation FooJsPanelView {
    MacSurface _surface;
    std::unique_ptr<jsp::Panel> _panel;
    NSTrackingArea* _tracking;
    BOOL _fixedScript;
    int _dropAllowed;
    id _keyUpMonitor;
    jsp::MacFocus _focus;
    BOOL _responder;
    NSArray* _keyObservers;
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
        [self registerForDraggedTypes:track_types()];
        __weak FooJsPanelView* weakSelf = self;
        _keyUpMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyUp handler:^NSEvent*(NSEvent* e) {
            FooJsPanelView* me = weakSelf;
            if (!me || !(e.modifierFlags & NSEventModifierFlagCommand) || e.window != me.window ||
                me.window.firstResponder != me)
                return e;
            [me keyUp:e];
            return nil;
        }];
    }
    return self;
}

- (NSDragOperation)drag:(jsp::Drag)kind info:(id<NSDraggingInfo>)s {
    const NSPoint p = [self convertPoint:s.draggingLocation fromView:nil];
    return drag_operation(drag_event(_panel.get(), _dropAllowed, kind, (int)p.x, (int)p.y, key_mask(NSEvent.modifierFlags), s));
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)s { return [self drag:jsp::Drag::Enter info:s]; }
- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)s { return [self drag:jsp::Drag::Over info:s]; }
- (void)draggingExited:(id<NSDraggingInfo>)s { [self drag:jsp::Drag::Leave info:s]; }
- (BOOL)performDragOperation:(id<NSDraggingInfo>)s { return [self drag:jsp::Drag::Drop info:s] != NSDragOperationNone; }

- (void)dealloc {
    if (_keyUpMonitor) [NSEvent removeMonitor:_keyUpMonitor];
    [self stopObservingKey];
    [_surface.editor close];
    _surface.editor = nil;
    _panel.reset();
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)keyDown:(NSEvent*)e {
    if (!(_panel && mac_key(*_panel, e))) [super keyDown:e];
}

- (void)keyUp:(NSEvent*)e {
    if (!(_panel && mac_key(*_panel, e))) [super keyUp:e];
}

- (void)syncFocus {
    const int change = _focus.update(_responder, self.window.isKeyWindow);
    if (change >= 0 && _panel) jsp::log::guarded("js", "focus", [&] { _panel->focus(change == 1); });
}

- (void)stopObservingKey {
    for (id o in _keyObservers) [[NSNotificationCenter defaultCenter] removeObserver:o];
    _keyObservers = nil;
}

- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];
    [self stopObservingKey];
    if (self.window) {
        __weak FooJsPanelView* weakSelf = self;
        NSNotificationCenter* nc = [NSNotificationCenter defaultCenter];
        void (^sync)(NSNotification*) = ^(NSNotification*) { [weakSelf syncFocus]; };
        _keyObservers = @[
            [nc addObserverForName:NSWindowDidBecomeKeyNotification object:self.window queue:nil usingBlock:sync],
            [nc addObserverForName:NSWindowDidResignKeyNotification object:self.window queue:nil usingBlock:sync],
        ];
    }
    [self syncFocus];
}

- (BOOL)becomeFirstResponder {
    _responder = YES;
    [self syncFocus];
    return YES;
}

- (BOOL)resignFirstResponder {
    _responder = NO;
    [self syncFocus];
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

- (void)viewDidChangeEffectiveAppearance {
    [super viewDidChangeEffectiveAppearance];
    if (_panel) _panel->post("on_colours_changed");
    [self setNeedsDisplay:YES];
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
    if ((pressed & 1) || e.type == NSEventTypeLeftMouseDragged) m |= 0x1;
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
    if (_surface.caption_drag(e.clickCount == 2)) return;
    [self.window makeFirstResponder:self];
    [self send:e.clickCount == 2 ? jsp::Mouse::LeftDouble : jsp::Mouse::LeftDown event:e];
}
- (void)mouseUp:(NSEvent*)e { [self send:jsp::Mouse::LeftUp event:e]; }
- (void)rightMouseDown:(NSEvent*)e {
    [self.window makeFirstResponder:self];
    [self send:e.clickCount == 2 ? jsp::Mouse::RightDouble : jsp::Mouse::RightDown event:e];
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
    const CGFloat scale = e.hasPreciseScrollingDeltas ? 10.0 : 1.0;
    const auto step = [](CGFloat d) { return d > 0 ? (int)std::ceil(d) : (int)std::floor(d); };
    const int dy = step(e.scrollingDeltaY / scale), dx = step(-e.scrollingDeltaX / scale);
    if (dy != 0) jsp::log::guarded("js", "wheel", [&] { _panel->wheel(dy); });
    if (dx != 0 && _panel) jsp::log::guarded("js", "wheel", [&] { _panel->wheel(dx, true); });
}

- (void)fireTimer:(int)tid {
    if (_panel) jsp::log::guarded("js", "timer", [&] { _panel->timer(tid); });
}

@end

namespace {

void MacSurface::invalidate() { [view setNeedsDisplay:YES]; }

int MacSurface::drag_tracks(metadb_handle_list_cref tracks, int effects) { return start_drag(view, tracks, effects); }

void MacSurface::set_tooltip(const std::string& text, bool active, int) { view.toolTip = active && !text.empty() ? ns(text) : nil; }

static bool argb_of(NSView* v, NSColor* c, uint32_t& out) {
    __block NSColor* rgb = nil;
    NSAppearance* a = v ? v.effectiveAppearance : NSApp.effectiveAppearance;
    [a performAsCurrentDrawingAppearance:^{ rgb = [c colorUsingColorSpace:NSColorSpace.sRGBColorSpace]; }];
    if (!rgb) return false;
    auto byte = [](CGFloat x) { return (uint32_t)std::lround(std::clamp<CGFloat>(x, 0, 1) * 255); };
    out = 0xff000000u | (byte(rgb.redComponent) << 16) | (byte(rgb.greenComponent) << 8) | byte(rgb.blueComponent);
    return true;
}

bool MacSurface::ui_colour(jsp::UiColour kind, uint32_t& argb) {
    NSColor* const colours[] = { NSColor.textColor, NSColor.textBackgroundColor, NSColor.controlAccentColor,
                                 NSColor.selectedContentBackgroundColor, NSColor.alternateSelectedControlTextColor,
                                 NSColor.unemphasizedSelectedContentBackgroundColor };
    return argb_of(view, colours[(int)kind], argb);
}

bool MacSurface::ui_font(int kind, jsp::UiFont& font) {
    if (kind == 5) {
        font = { "Menlo", 12, 0 };
        return true;
    }
    font = { "Segoe UI", (float)(kind == 1 || kind == 4 ? NSFont.smallSystemFontSize : NSFont.systemFontSize), 0 };
    return true;
}

bool MacSurface::sys_colour(int index, uint32_t& argb) {
    NSColor* const colours[] = {
        NSColor.controlColor, NSColor.windowBackgroundColor, NSColor.controlAccentColor, NSColor.windowBackgroundColor,
        NSColor.windowBackgroundColor, NSColor.textBackgroundColor, NSColor.separatorColor, NSColor.textColor,
        NSColor.textColor, NSColor.textColor, NSColor.separatorColor, NSColor.separatorColor,
        NSColor.windowBackgroundColor, NSColor.selectedContentBackgroundColor, NSColor.alternateSelectedControlTextColor,
        NSColor.controlColor, NSColor.separatorColor, NSColor.disabledControlTextColor, NSColor.controlTextColor,
        NSColor.secondaryLabelColor, NSColor.controlBackgroundColor, NSColor.shadowColor, NSColor.highlightColor,
        NSColor.textColor, NSColor.controlBackgroundColor, nil, NSColor.linkColor, NSColor.windowBackgroundColor,
        NSColor.windowBackgroundColor, NSColor.selectedContentBackgroundColor, NSColor.windowBackgroundColor,
    };
    if (index < 0 || index >= (int)std::size(colours) || !colours[index]) return false;
    return argb_of(view, colours[index], argb);
}

bool MacSurface::pick_colour(uint32_t& argb) {
    NSColorPanel* p = NSColorPanel.sharedColorPanel;
    p.showsAlpha = NO;
    p.color = [NSColor colorWithSRGBRed:((argb >> 16) & 0xff) / 255.0 green:((argb >> 8) & 0xff) / 255.0 blue:(argb & 0xff) / 255.0 alpha:1];
    FooJsPanelColourDelegate* d = [FooJsPanelColourDelegate new];
    id<NSWindowDelegate> old = p.delegate;
    p.delegate = d;
    [p makeKeyAndOrderFront:nil];
    [NSApp runModalForWindow:p];
    p.delegate = old;
    return argb_of(nil, p.color, argb);
}

void MacSurface::invalidate_rect(const pui::gfx::Rect& r) {
    [view setNeedsDisplayInRect:NSMakeRect(r.x, r.y, r.w, r.h)];
}

void MacSurface::start_timer(int id, int ms) {
    stop_timer(id);
    __weak FooJsPanelView* v = (FooJsPanelView*)view;
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
        NSMenuItem* mi = [[NSMenuItem alloc] initWithTitle:ns(jsp::menu_label(it.text)) action:it.submenu ? nil : @selector(picked:) keyEquivalent:@""];
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
    NSPoint at = NSMakePoint(x, y);
    if (NSWindow* w = view.window; w && w.screen) {
        const CGFloat margin = 24;
        const NSRect screen = w.screen.frame;
        const NSPoint in_window = [view convertPoint:at toView:nil];
        NSRect r = [w convertRectToScreen:NSMakeRect(in_window.x, in_window.y, 0, 0)];
        r.origin.x = std::max(r.origin.x, NSMinX(screen) + margin);
        r.origin.y = std::min(r.origin.y, NSMaxY(screen) - margin);
        at = [view convertPoint:[w convertRectFromScreen:r].origin fromView:nil];
    }
    [m popUpMenuPositioningItem:nil atLocation:at inView:view];
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

static bool maximized(NSWindow* w) {
    const NSRect screen = w.screen ? w.screen.visibleFrame : NSZeroRect;
    return w.zoomed && NSWidth(w.frame) >= NSWidth(screen) - 1 && NSHeight(w.frame) >= NSHeight(screen) - 1;
}

int MacSurface::window_state() {
    NSWindow* w = view.window;
    return !w ? 0 : w.miniaturized ? 1 : maximized(w) ? 2 : 0;
}

void MacSurface::set_window_state(int state) {
    NSWindow* w = view.window;
    if (!w) return;
    if (state == 1) {
        [w miniaturize:nil];
        return;
    }
    if (w.miniaturized) [w deminiaturize:nil];
    if ((state == 2) != maximized(w)) [w zoom:nil];
}

bool MacSurface::in_caption(NSEvent* e) {
    NSWindow* w = view.window;
    NSView* content = w.contentView;
    if (!w || !content || caption.w <= 0 || caption.h <= 0 || fullscreen()) return false;
    NSPoint p = [content convertPoint:e.locationInWindow fromView:nil];
    if (!content.isFlipped) p.y = NSHeight(content.bounds) - p.y;
    return p.x >= caption.x && p.x < caption.x + caption.w && p.y >= caption.y && p.y < caption.y + caption.h;
}

bool MacSurface::html_dialog(const jsp::HtmlDialog& d, const jsp::DialogCall& call) {
    NSString* file = [NSTemporaryDirectory() stringByAppendingPathComponent:[NSString stringWithFormat:@"foo_js_panel_dialog_%d.html", getpid()]];
    const std::string html = jsp::dialog_html(d.html, d.baseDir, false);
    if (![[NSData dataWithBytes:html.data() length:html.size()] writeToFile:file atomically:YES]) return false;
    std::string script = "(function(){var post=function(m){window.webkit.messageHandlers.jsp.postMessage(m);};"
                         "var plain=function(a){return a===undefined||typeof a==='function'?null:a!==null&&typeof a==='object'?JSON.parse(JSON.stringify(a)):a;};"
                         "var __jsp_fn=function(n){return function(){post({op:'call',fn:n,args:Array.prototype.slice.call(arguments).map(plain)});};};"
                         "var data=" + js_literal(d.data) + ";"
                         "if(Array.isArray(data))Object.defineProperty(data,'toArray',{value:function(){return this.slice();}});"
                         "try{Object.defineProperty(window,'external',{value:{dialogArguments:data},configurable:true,writable:true});}catch(e){}"
                         "window.dialogArguments=data;window.close=function(){post({op:'close'});};"
                         "window.resizeTo=function(w,h){post({op:'resize',w:w,h:h});};";
    if (!d.contextMenu) script += "document.addEventListener('contextmenu',function(e){e.preventDefault();});";
    if (!d.selection) script += "document.addEventListener('DOMContentLoaded',function(){document.body.style.webkitUserSelect='none';});";
    script += "})();";
    FooJsPanelHtmlDialog* h = [FooJsPanelHtmlDialog new];
    h.call = &call;
    WKWebViewConfiguration* cfg = [WKWebViewConfiguration new];
    [cfg.userContentController addUserScript:[[WKUserScript alloc] initWithSource:ns(script)
                                                                    injectionTime:WKUserScriptInjectionTimeAtDocumentStart
                                                                 forMainFrameOnly:YES]];
    [cfg.userContentController addScriptMessageHandler:h name:@"jsp"];
    const NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | (d.resizable ? NSWindowStyleMaskResizable : 0);
    NSPanel* panel = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, std::max(d.width, 100), std::max(d.height, 60))
                                                styleMask:style
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
    panel.releasedWhenClosed = NO;
    panel.title = @"";
    panel.delegate = h;
    h.panel = panel;
    WKWebView* web = [[WKWebView alloc] initWithFrame:panel.contentView.bounds configuration:cfg];
    web.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    web.navigationDelegate = h;
    [panel.contentView addSubview:web];
    if (d.center || !NSScreen.screens.count) [panel center];
    else [panel setFrameTopLeftPoint:NSMakePoint(d.x, NSMaxY(NSScreen.screens[0].frame) - d.y)];
    [web loadFileURL:[NSURL fileURLWithPath:file] allowingReadAccessToURL:[NSURL fileURLWithPath:@"/"]];
    NSWindow* previous = NSApp.keyWindow;
    NSModalSession session = [NSApp beginModalSessionForWindow:panel];
    while (!h.done && [NSApp runModalSession:session] == NSModalResponseContinue)
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
    [NSApp endModalSession:session];
    [cfg.userContentController removeScriptMessageHandlerForName:@"jsp"];
    web.navigationDelegate = nil;
    panel.delegate = nil;
    [panel orderOut:nil];
    if (previous.isVisible && !previous.isKeyWindow) [previous makeKeyWindow];
    [[NSFileManager defaultManager] removeItemAtPath:file error:nil];
    return true;
}

bool MacSurface::caption_drag(bool double_click) {
    NSEvent* e = NSApp.currentEvent;
    if (!e || e.type != NSEventTypeLeftMouseDown || !in_caption(e)) return false;
    if (double_click) [view.window zoom:nil];
    else [view.window performWindowDragWithEvent:e];
    return true;
}

bool MacSurface::visible() {
    return view.window && !view.isHiddenOrHasHiddenAncestor;
}

bool MacSurface::fullscreen() {
    NSWindow* w = view.window;
    return w && (w.styleMask & NSWindowStyleMaskFullScreen) != 0;
}

void MacSurface::set_fullscreen(bool on) {
    NSWindow* w = view.window;
    if (w && on != fullscreen()) [w toggleFullScreen:nil];
}

bool MacSurface::always_on_top() {
    NSWindow* w = view.window;
    return w && w.level >= NSFloatingWindowLevel;
}

void MacSurface::set_always_on_top(bool on) {
    if (NSWindow* w = view.window) w.level = on ? NSFloatingWindowLevel : NSNormalWindowLevel;
}

static pui::gfx::Rect top_left(NSRect r) {
    const CGFloat top = NSScreen.screens.count ? NSMaxY(NSScreen.screens[0].frame) : NSMaxY(r);
    return { (int)std::lround(r.origin.x), (int)std::lround(top - NSMaxY(r)), (int)std::lround(r.size.width), (int)std::lround(r.size.height) };
}

bool MacSurface::window_rect(pui::gfx::Rect& content, pui::gfx::Rect& work) {
    NSWindow* w = view.window;
    if (!w) return false;
    content = top_left([w contentRectForFrameRect:w.frame]);
    work = top_left((w.screen ?: NSScreen.mainScreen).visibleFrame);
    return true;
}

void MacSurface::set_window_rect(const pui::gfx::Rect& content) {
    NSWindow* w = view.window;
    if (!w || w.miniaturized || fullscreen()) return;
    if (maximized(w)) [w zoom:nil];
    const CGFloat top = NSScreen.screens.count ? NSMaxY(NSScreen.screens[0].frame) : 0;
    const NSRect r = NSMakeRect(content.x, top - content.y - content.h, content.w, content.h);
    [w setFrame:[w frameRectForContentRect:r] display:YES];
}

void MacSurface::set_window_limits(int minW, int minH, int maxW, int maxH) {
    NSWindow* w = view.window;
    if (!w) return;
    w.contentMinSize = NSMakeSize(minW > 0 ? minW : 100, minH > 0 ? minH : 100);
    w.contentMaxSize = NSMakeSize(maxW > 0 ? maxW : FLT_MAX, maxH > 0 ? maxH : FLT_MAX);
    if (maximized(w) || fullscreen()) return;
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
            if ([s isKindOfClass:[NSString class]] && [s length])
                c.script = ns(jsp::resolve_script_path([s UTF8String], jsp::ui::home_dir(),
                                                       filesystem::g_get_native_path(core_api::get_profile_path()).c_str()));
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

namespace jsp {

std::unique_ptr<Surface> make_owner_surface(void* native_window) {
    auto s = std::make_unique<MacSurface>();
    s->view = (__bridge NSView*)native_window;
    return s;
}

bool windowless_key(Panel& panel, const void* native_event) { return mac_key(panel, (__bridge NSEvent*)native_event); }

int windowless_drag(Panel& panel, int& allowed, Drag kind, int x, int y, unsigned keys, int, const void* native_data, void*) {
    id<NSDraggingInfo> info = (__bridge id<NSDraggingInfo>)native_data;
    return info ? drag_event(&panel, allowed, kind, x, y, keys, info) : 0;
}

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

int system_metric(int index) {
    if (index == 0 || index == 1) {
        const NSRect f = NSScreen.mainScreen.frame;
        return (int)(index == 0 ? f.size.width : f.size.height);
    }
    return default_system_metric(index);
}

bool move_to_trash(const std::string& path) {
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    return url && [[NSFileManager defaultManager] trashItemAtURL:url resultingItemURL:nil error:nil];
}

std::string map_chinese(const std::string& text, bool traditional) {
    NSString* s = [NSString stringWithUTF8String:text.c_str()];
    NSString* out = [s stringByApplyingTransform:traditional ? @"Hans-Hant" : @"Hant-Hans" reverse:NO];
    return out.UTF8String ? out.UTF8String : text;
}

bool register_font(const std::string& path) {
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    return url && CTFontManagerRegisterFontsForURL((__bridge CFURLRef)url, kCTFontManagerScopeProcess, nullptr);
}

std::string clipboard_text() {
    NSString* s = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
    return s.UTF8String ? s.UTF8String : "";
}

bool shell_run(const std::string& command) {
    static const char* const power_scripts[] = { nullptr, "shut down", "restart", "sleep" };
    if (const char* verb = power_scripts[(int)jsp::windows_power_command(command)]) {
        NSString* source = [NSString stringWithFormat:@"tell application \"System Events\" to %s", verb];
        NSDictionary* error = nil;
        [[[NSAppleScript alloc] initWithSource:source] executeAndReturnError:&error];
        return error == nil;
    }
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

bool copy_tracks(metadb_handle_list_cref tracks) {
    NSArray<NSPasteboardItem*>* items = pasteboard_items(tracks);
    if (!items.count) return false;
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    [pb clearContents];
    return [pb writeObjects:items];
}

bool clipboard_has_tracks() { return [[NSPasteboard generalPasteboard] availableTypeFromArray:track_types()] != nil; }

bool clipboard_tracks(metadb_handle_list_ref out) {
    PasteboardTracks pasted = pasteboard_tracks([NSPasteboard generalPasteboard]);
    auto api = metadb::get();
    for (const std::string& f : pasted.files) {
        pfc::string8 path;
        filesystem::g_get_canonical_path(f.c_str(), path);
        pasted.tracks.add_item(api->handle_create(path, 0));
    }
    out = pasted.tracks;
    return out.get_count() > 0;
}

bool copy_to_clipboard(const std::string& text) {
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    [pb clearContents];
    return [pb setString:ns(text) forType:NSPasteboardTypeString];
}

}
