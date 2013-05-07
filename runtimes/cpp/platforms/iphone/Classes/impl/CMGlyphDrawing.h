//
//  CMGlyphDrawing.h
//

//#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>

extern int CMFontGetGlyphsForUnichars(CGFontRef font, const UTF32Char buffer[], CGGlyph glyphs[], size_t numGlyphs);