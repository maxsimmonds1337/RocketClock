// Rotating frame - RocketClock
// Tumbles a hollow rectangle about an arbitrary axis with a real 3D rotation
// matrix (axis-angle / Rodrigues), then projects onto the panel. It shows each
// axis TWICE: once orthographic, once perspective, so you can see the diff.
//
//   Orthographic:  (x,y,z) -> (x,y).           z is DROPPED. No foreshortening,
//                                              so forward/back look identical.
//   Perspective:   (x,y,z) -> (x,y) * d/(d+z). z is USED. Nearer (z<0) grows,
//                                              farther (z>0) shrinks -> you can
//                                              see which edge tips toward you.
//
// For a unit axis u and angle t:  R = I cos t + (1-cos t) u u^T + sin t [u]_x.
// Source points have z=0, so we need R's first two columns of all three rows:
//   rx = m00 px + m01 py     ry = m10 px + m11 py     rz = m20 px + m21 py
#include <RocketMatrix.h>
#include <math.h>

RocketMatrix matrix;

const float CENTER = 3.5f;
const float EYE    = 7.0f;   // viewer distance for perspective (bigger = flatter)

struct Axis { float ux, uy, uz; const char *name; };
const Axis AXES[] = {
  {1.0f, 0.0f, 0.0f, "forward/back (X axis)"},
  {0.0f, 1.0f, 0.0f, "left/right (Y axis)"},
  {0.70710678f, 0.70710678f, 0.0f, "diagonal (x=y axis)"},
};
const int NUM_AXES = sizeof(AXES) / sizeof(AXES[0]);

static inline bool framePattern(int x, int y) {
  return (x == 0) || (x == 7) || (y == 0) || (y == 7);
}

// Rotate the frame about unit axis by angle t; project ortho or perspective.
void drawFrameRot(const Axis &a, float t, bool perspective) {
  float c = cosf(t), s = sinf(t), k = 1.0f - c;
  float ux = a.ux, uy = a.uy, uz = a.uz;
  float m00 = c + ux * ux * k, m01 = ux * uy * k - uz * s;
  float m10 = uy * ux * k + uz * s, m11 = c + uy * uy * k;
  float m20 = uz * ux * k - uy * s, m21 = uz * uy * k + ux * s;  // z row

  matrix.clear();
  for (int x = 0; x < 8; x++) {
    for (int y = 0; y < 8; y++) {
      if (!framePattern(x, y)) continue;
      float px = x - CENTER, py = y - CENTER;
      float rx = m00 * px + m01 * py;
      float ry = m10 * px + m11 * py;
      float rz = m20 * px + m21 * py;                 // depth after rotation
      float scale = perspective ? (EYE / (EYE + rz)) : 1.0f;
      int dx = (int)lroundf(rx * scale + CENTER);
      int dy = (int)lroundf(ry * scale + CENTER);
      if (dx >= 0 && dx < 8 && dy >= 0 && dy < 8) matrix.setPixel(dx, dy, true);
    }
  }
}

void tumble(const Axis &a, bool perspective) {
  Serial.printf("%s - %s\n", a.name, perspective ? "PERSPECTIVE" : "orthographic");
  drawFrameRot(a, 0.0f, perspective);
  delay(1000);
  for (float t = 0.0f; t <= TWO_PI; t += 0.13f) {
    drawFrameRot(a, t, perspective);
    delay(45);
  }
}

void setup() {
  Serial.begin(115200);
  matrix.begin(4);
}

void loop() {
  for (int i = 0; i < NUM_AXES; i++) {
    tumble(AXES[i], false);   // orthographic
    tumble(AXES[i], true);    // perspective
  }
}
