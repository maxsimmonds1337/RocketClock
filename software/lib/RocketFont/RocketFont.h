#pragma once
#include <RocketMatrix.h>
#include "rocketfont_glyphs.h"

// RocketFont - 8x8 glyph set (upper/lower/digits/punctuation/degree/pound)
// rendered through RocketMatrix. Built for scrolling: every draw takes an X
// offset, so horizontal scroll is just "advance xOffset" each frame.
//
// Glyph storage (from rocketfont_glyphs.h): FONT_GLYPHS[g] is bool[64] with
// index i -> col = i/8, row = i%8 (row 0 top, col 0 left). Glyphs are drawn in
// an 8-wide cell; ADVANCE controls inter-character spacing when scrolling text.
namespace RocketFont {

const int CELL = 8;       // glyph cell width
const int GAP  = 1;       // blank columns between glyphs when laying out text
const int SPACE_W = 3;    // width used for space / unknown glyphs

// Map a Latin-1 char to a glyph index, or -1 if not in the font.
inline int indexOf(char c) {
  unsigned char cp = (unsigned char)c;
  for (int g = 0; g < FONT_COUNT; g++)
    if (FONT_CP[g] == cp) return g;
  return -1;
}

// Actual drawn width of a glyph (rightmost lit column + 1) for proportional
// spacing, so e.g. "No" doesn't overlap. Empty glyphs (space) use SPACE_W.
inline int glyphWidth(int g) {
  if (g < 0 || g >= FONT_COUNT) return SPACE_W;
  int maxc = -1;
  for (int col = 0; col < CELL; col++)
    for (int row = 0; row < 8; row++)
      if (FONT_GLYPHS[g][col * 8 + row]) { maxc = col; break; }
  return maxc < 0 ? SPACE_W : maxc + 1;
}

// Draw glyph `g` with its left edge at column `xOffset` (may be off-screen).
// Columns landing anywhere on the canvas (0..width-1) are drawn, so glyphs
// span multiple chained panels. Pixels are OR-ed via setPixel.
inline void drawGlyphAt(RocketMatrix &m, int g, int xOffset, int yOffset = 0) {
  if (g < 0 || g >= FONT_COUNT) return;
  int w = m.width(), h = m.height();
  for (int col = 0; col < CELL; col++) {
    int dx = xOffset + col;
    if (dx < 0 || dx >= w) continue;
    for (int row = 0; row < 8; row++) {
      int dy = row + yOffset;
      if (dy < 0 || dy >= h) continue;
      if (FONT_GLYPHS[g][col * 8 + row]) m.setPixel(dx, dy, true);
    }
  }
}

// Draw a single char at xOffset (clears the panel first).
inline void drawChar(RocketMatrix &m, char c, int xOffset = 0) {
  m.clear();
  drawGlyphAt(m, indexOf(c), xOffset);
}

// Total pixel width a string occupies with proportional spacing.
inline int textWidth(const char *s) {
  int x = 0;
  for (const char *p = s; *p; p++) x += glyphWidth(indexOf(*p)) + GAP;
  return x > 0 ? x - GAP : 0;         // no trailing gap
}

// Render a string with its left edge at `xOffset`, top edge at `yOffset`
// (clears first). Horizontal scroll: decrease xOffset from +width to -textWidth.
inline void drawText(RocketMatrix &m, const char *s, int xOffset, int yOffset = 0) {
  m.clear();
  int x = xOffset, canvas = m.width();
  for (const char *p = s; *p; p++) {
    int g = indexOf(*p);
    int w = glyphWidth(g);
    if (x >= canvas) break;           // rest is off the right edge of the canvas
    if (x + w > 0) drawGlyphAt(m, g, x, yOffset);
    x += w + GAP;
  }
}

// Y offset that vertically centres an 8px-tall glyph row on the canvas
// (e.g. 4 on a 16-tall 2-row display, 0 on a single row).
inline int centreY(RocketMatrix &m) { return (m.height() - 8) / 2; }

// Vertical (top-down) scroll: text is laid out horizontally, left-aligned (or
// centred if it fits), and the whole line slides down as yOffset increases from
// -8 (above the top) to height (below the bottom).
inline void drawTextVertical(RocketMatrix &m, const char *s, int yOffset) {
  int tw = textWidth(s);
  int x = tw < m.width() ? (m.width() - tw) / 2 : 0;   // centre if it fits
  m.clear();
  int canvas = m.width();
  for (const char *p = s; *p; p++) {
    int g = indexOf(*p);
    int w = glyphWidth(g);
    if (x >= canvas) break;
    if (x + w > 0) drawGlyphAt(m, g, x, yOffset);
    x += w + GAP;
  }
}

// Width of a string laid out with a specific inter-glyph gap.
inline int textWidthGap(const char *s, int gap) {
  int x = 0;
  for (const char *p = s; *p; p++) x += glyphWidth(indexOf(*p)) + gap;
  return x > 0 ? x - gap : 0;
}

// Draw a short string STATICALLY, centred both axes (no scrolling). Tightens
// the inter-glyph gap (1 -> 0) if needed so it fits the canvas width, e.g. so
// "HH:MM" fits across a 4-panel-wide display. Good for clocks / short readouts.
inline void drawCentered(RocketMatrix &m, const char *s) {
  int gap = 1, tw = textWidthGap(s, gap);
  if (tw > m.width()) { gap = 0; tw = textWidthGap(s, gap); }
  m.clear();
  int x = (m.width() - tw) / 2;          // may be <0 if still too wide; clips
  int y = (m.height() - 8) / 2;
  for (const char *p = s; *p; p++) {
    int g = indexOf(*p), w = glyphWidth(g);
    drawGlyphAt(m, g, x, y);
    x += w + gap;
  }
}

}  // namespace RocketFont
