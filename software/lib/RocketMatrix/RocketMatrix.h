#pragma once
#include <Arduino.h>

// RocketMatrix - driver for one or more chained MAX7219 8x8 modules.
//
// Single module (default): a virtual 8x8 canvas. setPixel(x,y) with x=col
// (DIG), y=row (SEG). The no-decode segment-bit remap (rowBit) is handled so
// logical (col,row) maps to the right LED - never poke raw bits.
//
// Multiple modules: call setLayout(cols, rows, serpentine, flipReverse) before
// begin(). The canvas becomes (cols*8) x (rows*8). Panels form ONE daisy-chain
// (DOUT->DIN). RocketClock's board-to-board connectors are directional (DIN in
// on the left, DOUT out on the right), so a multi-ROW display must SNAKE:
// even rows run left->right, odd rows right->left, and - because odd rows are
// physically rotated 180 to mate the connectors - their pixels are flipped.
// That is exactly what serpentine + flipReverse do. Chain module 0 is the panel
// wired to the ESP (top-left); the farthest module is clocked out first.
class RocketMatrix {
public:
  static const int MAX_PANELS = 16;

  // Default pins match every RocketClock sketch (D5/D6/D7 on the ESP-12F).
  RocketMatrix(uint8_t din = 14, uint8_t clk = 12, uint8_t cs = 13)
    : _din(din), _clk(clk), _cs(cs) {}

  // Configure the panel grid. cols x rows modules, <= MAX_PANELS. serpentine =
  // snake wiring (odd rows reversed); flipReverse = odd-row panels rotated 180.
  void setLayout(uint8_t cols, uint8_t rows,
                 bool serpentine = true, bool flipReverse = true) {
    _cols = cols ? cols : 1;
    _rows = rows ? rows : 1;
    _n = _cols * _rows;
    if (_n > MAX_PANELS) { _n = MAX_PANELS; }
    _serpentine = serpentine;
    _flipReverse = flipReverse;
  }

  int width()  const { return _cols * 8; }
  int height() const { return _rows * 8; }
  int panels() const { return _n; }

  // Init pins + every MAX7219 (no-decode, scan all 8 digits) and clear.
  //
  // Order matters: the chip powers up with RANDOM display-data registers, so we
  // configure AND clear while still in shutdown, then enable normal operation
  // LAST. Otherwise the random power-up data flashes on screen before clear()
  // runs (the "random all-lit at boot" bug). Display-test is forced off too.
  void begin(uint8_t brightness = 5) {
    pinMode(_din, OUTPUT); pinMode(_clk, OUTPUT); pinMode(_cs, OUTPUT);
    digitalWrite(_cs, HIGH); digitalWrite(_clk, LOW);
    broadcast(REG_SHUTDOWN,    0x00);   // stay shut down while we set up
    broadcast(REG_DISPLAYTEST, 0x00);   // display-test off (else all LEDs on)
    broadcast(REG_DECODE_MODE, 0x00);   // no decode - raw segment bits
    broadcast(REG_SCAN_LIMIT,  0x07);   // scan digits 0..7
    setBrightness(brightness);
    clear();                            // zero every digit register (blank)
    broadcast(REG_SHUTDOWN,    0x01);   // now enable the display (already blank)
  }

  // Physical row (0..7) -> MAX7219 no-decode data bit (datasheet Table 6).
  static uint8_t rowBit(uint8_t row) {
    return (row == 7) ? 0x80 : (uint8_t)(0x40 >> row);
  }

  // Per-panel orientation calibration (applied to every module's local coords).
  void setOrientation(bool flipCols, bool flipRows, bool transpose = false) {
    _flipC = flipCols; _flipR = flipRows; _transpose = transpose;
  }

  // Rotate the WHOLE display 180 deg (e.g. mounting it USB-down instead of up).
  // Applied at the canvas level before panel mapping, so the image stays upright
  // and the chain-0 (master) panel effectively becomes bottom-right.
  void setFlip180(bool f) { _flip180 = f; }

  // Set/clear one pixel on the virtual canvas and push it to the chain.
  void setPixel(int x, int y, bool on = true) {
    int module, lx, ly;
    if (!map(x, y, module, lx, ly)) return;
    if (on) _fb[module][lx] |=  rowBit(ly);
    else    _fb[module][lx] &= ~rowBit(ly);
    pushRegister(module, lx);
  }

  bool getPixel(int x, int y) const {
    int module, lx, ly;
    if (!map(x, y, module, lx, ly)) return false;
    return _fb[module][lx] & rowBit(ly);
  }

  // Turn every LED on or off across all modules.
  void fill(bool on) {
    for (int m = 0; m < _n; m++)
      for (int c = 0; c < 8; c++) _fb[m][c] = on ? 0xFF : 0x00;
    show();
  }
  void clear() { fill(false); }

  // Push the whole framebuffer to the chain (farthest module first).
  void show() {
    for (int dig = 0; dig < 8; dig++) {
      frameBegin();
      for (int m = _n - 1; m >= 0; m--) shiftWord(dig + 1, _fb[m][dig]);
      frameEnd();
    }
  }

  void setBrightness(uint8_t b) { broadcast(REG_INTENSITY, b & 0x0F); }

  // Send one word to EVERY module (config registers, brightness, raw pokes).
  void send(uint8_t address, uint8_t data) { broadcast(address, data); }

private:
  // Map virtual (x,y) -> (chain module, local x, local y). false if off-canvas.
  bool map(int x, int y, int &module, int &lx, int &ly) const {
    if (x < 0 || y < 0 || x >= width() || y >= height()) return false;
    if (_flip180) { x = width() - 1 - x; y = height() - 1 - y; }   // whole-display 180
    int pc = x / 8, pr = y / 8;
    lx = x % 8; ly = y % 8;
    int chainCol = pc;
    if (_serpentine && (pr & 1)) {
      chainCol = _cols - 1 - pc;
      if (_flipReverse) { lx = 7 - lx; ly = 7 - ly; }   // panel rotated 180
    }
    module = pr * _cols + chainCol;
    applyOrientation(lx, ly);
    return true;
  }

  void applyOrientation(int &col, int &row) const {
    if (_transpose) { int t = col; col = row; row = t; }
    if (_flipC) col = 7 - col;
    if (_flipR) row = 7 - row;
  }

  // --- low-level SPI framing ---
  void frameBegin() { digitalWrite(_cs, LOW); }
  void frameEnd()   { digitalWrite(_cs, HIGH); }

  void shiftWord(uint8_t address, uint8_t data) {   // clock 16 bits, no CS
    uint16_t word = ((uint16_t)address << 8) | data;
    for (int i = 15; i >= 0; i--) {
      digitalWrite(_clk, LOW);
      digitalWrite(_din, (word >> i) & 1);
      digitalWrite(_clk, HIGH);
    }
  }

  // Write one register on one module; NO-OP (reg 0) to the others in the frame.
  void pushRegister(int module, int dig) {
    frameBegin();
    for (int m = _n - 1; m >= 0; m--)
      shiftWord(m == module ? dig + 1 : 0x00, m == module ? _fb[m][dig] : 0x00);
    frameEnd();
  }

  // Write the same (addr,data) to every module in one frame.
  void broadcast(uint8_t address, uint8_t data) {
    frameBegin();
    for (int m = 0; m < _n; m++) shiftWord(address, data);
    frameEnd();
  }

  static const uint8_t REG_DECODE_MODE = 0x09;
  static const uint8_t REG_INTENSITY   = 0x0A;
  static const uint8_t REG_SCAN_LIMIT  = 0x0B;
  static const uint8_t REG_SHUTDOWN    = 0x0C;
  static const uint8_t REG_DISPLAYTEST = 0x0F;

  uint8_t _din, _clk, _cs;
  uint8_t _cols = 1, _rows = 1;
  int _n = 1;
  bool _serpentine = true, _flipReverse = true;
  uint8_t _fb[MAX_PANELS][8] = {{0}};   // per-module framebuffer, indexed by DIG
  bool _flipC = false, _flipR = false, _transpose = false, _flip180 = false;
};
