#include "animator.hpp"
#include <algorithm>
#include <cmath>

namespace lunaredu::pet::render {
namespace {
float clamp(float value, float low, float high) {
  return std::clamp(value, low, high);
}
float minf(float left, float right) { return std::min(left, right); }
float maxf(float left, float right) { return std::max(left, right); }
float K2(float value) {
  return value < .5f ? 4 * value * value * value
                     : 1 - std::pow(-2 * value + 2, 3) / 2;
}
} // namespace

// Pet's native pose equations.
Pose Animator::pose(float mt, float dtState, float now) {
    const Extra extra{eye_to, morph.x, blink.x};
    float pt = 0;
    float lid = 1;
    float eyeBoost = 1;
    float spin = pt, tx = 0, ty = 0, squash = 1;

    switch (state) {
      case sleeping: {
        float En = minf(dtState / 2, 1);
        float Zt = std::sin(clamp(dtState / 0.5f, 0, 1) * pi);
        spin = pt + 4 * En + std::sin(mt * 0.25f) * 2;
        tx = -2 * En;
        ty = 8 * En + std::sin(mt * 0.55f) * 3 - Zt * 5;
        squash = 1 + std::sin(mt * 0.55f) * 0.016f + Zt * 0.05f;
        if ((extra.eyeTo==13 || extra.eyeTo==22 || extra.eyeTo==4)) {
          lid = extra.eyeMorphX > 0.85f ? 1 : 0.08f;
        } else if (dtState < 1.2f) {
          float dn = minf(1, dtState / 1);
          lid = maxf(0.08f, 1 - dn * (1 + 0.15f * std::sin(dtState * 6.5f)));
        } else {
          lid = 0.08f;
          if (extra.blinkX < 0.18f) ctx.forceSleepEye = true;
        }
        break;
      }
      case waking: {
        if (dtState < 0.5f) {
          lid = 0.07f;
          ty = 6;
          ctx.wakeEye = {3, 12};
        } else if (dtState < 1.2f) {
          lid = 1;
          eyeBoost = 1.12f;
          ty = -5;
          tx = 0;
          spin = pt;
          squash = 1.04f;
          if (!ctx.wakingBurst) ctx.wantBurst = {rand(9, 13), 0.8f};
        } else if (dtState < 2.2f) {
          ty = 0;
          squash = 1;
          ctx.wakeEye = {0, 7};
          if (dtState < 1.4f) ctx.wakeBlink = true;
        } else {
          float Et = minf((dtState - 2.2f) / 0.8f, 1);
          ctx.wakeEye = {0, 7};
          spin = pt + std::sin(Et * pi * 3) * 6 * (1 - Et);
          ty = std::sin(mt * 0.9f) * 2;
        }
        break;
      }
      case idle:
        spin = pt + std::sin(mt * 0.5f) * 1.5f + std::sin(mt * 0.17f) * 0.6f;
        tx = std::sin(mt * 0.27f) * 1;
        ty = std::sin(mt * 0.85f) * 1.2f;
        squash = 1 + std::sin(mt * 0.85f) * 0.007f;
        break;
      case listening:
        spin = pt + 8 + std::sin(mt * 0.5f) * 1.5f;
        tx = 2;
        ty = -2 + std::sin(mt * 0.8f) * 0.8f;
        squash = 1.015f;
        if (now >= ctx.nodUntil) {
          ctx.nodUntil = now + rand(1800, 3200);
          ctx.nodEnd = now + 380;
        }
        if (now < ctx.nodEnd) {
          float Et = 1 - (ctx.nodEnd - now) / 380;
          ty += std::sin(Et * pi) * 4.5f;
          spin += std::sin(Et * pi) * 2;
        }
        break;
      case thinking:
        spin = pt - 9 + std::sin(mt * 0.35f) * 5;
        tx = std::sin(mt * 0.3f) * 5;
        ty = std::sin(mt * 0.6f) * 2.5f;
        squash = 1;
        break;
      case searching: {
        float Et = std::sin(mt * 1.3f);
        spin = pt + Et * 13;
        tx = Et * 7;
        ty = std::sin(mt * 1.7f) * 3;
        squash = 1;
        break;
      }
      case working: {
        float Et = std::sin(mt * pi * 2 * 1.6f);
        spin = pt + 4 + Et * 2.5f;
        tx = 3;
        ty = 1.5f + maxf(0, Et) * 3;
        squash = 1 - maxf(0, Et) * 0.02f;
        break;
      }
      case excited: {
        float Et = std::fmod(mt * 2.2f, 1.0f);
        float En = std::sin(Et * pi);
        ty = -En * 10 + 2;
        squash = Et < 0.1f ? 0.92f : Et < 0.3f ? 1.05f : 1;
        tx = std::sin(mt * 1.1f) * 4;
        eyeBoost = 1.06f;
        spin = pt + std::sin(mt * pi * 2 * 1.1f) * 7;
        break;
      }
      case surprised: {
        float Et = minf(dtState / 1.2f, 1);
        tx = -4 * (1 - Et);
        ty = -8 * (1 - Et);
        squash = dtState < 0.2f ? 1.08f : 1;
        eyeBoost = 1.15f - Et * 0.08f;
        spin = pt + std::sin(mt * 11) * 1.5f * (1 - Et);
        break;
      }
      case suspicious:
        spin = pt - 6 + std::sin(mt * 0.3f) * 3;
        tx = std::sin(mt * 0.25f) * -4;
        ty = 1 + std::sin(mt * 0.45f) * 1.2f;
        squash = 1;
        lid = 0.85f;
        if (now >= ctx.impulseAt) {
          ctx.spinKick = 30;
          ctx.impulseAt = now + rand(4000, 7000);
        }
        break;
      case angry:
        if (now >= ctx.impulseAt) {
          ctx.angryShakeUntil = now + 420;
          ctx.tyKick = 70;
          ctx.impulseAt = now + rand(1800, 3200);
        }
        spin = pt + (now < ctx.angryShakeUntil ? std::sin(now * 0.05f) * 4.5f : 0);
        tx = 0;
        ty = 3.5f;
        squash = 0.975f;
        break;
      case drowsy: {
        spin = pt + std::sin(mt * 0.32f) * 2.5f;
        tx = std::sin(mt * 0.2f) * 1.5f;
        ty = 6 + std::sin(mt * 0.36f) * 2.2f;
        squash = 1 + std::sin(mt * 0.36f) * 0.022f;
        lid = 0.34f + std::sin(mt * 0.8f) * 0.07f;
        if (now >= ctx.nodUntil && !ctx.slumpAt) ctx.slumpAt = now;
        if (ctx.slumpAt) {
          float En = (now - ctx.slumpAt) / 1000;
          float Zt = 1.7f, dn = 0.3f, on = 1.5f;
          if (En < Zt) {
            float bn = En / Zt, Cn = bn * bn;
            float bi = std::sin(bn * pi * 2.5f) * 2.2f * (1 - bn);
            ty = 6 + Cn * 19 + bi;
            spin = pt + Cn * 10;
            lid = 0.34f - Cn * (0.34f - 0.04f);
            squash = 1 - Cn * 0.045f;
          } else if (En < Zt + dn) {
            float bn = (En - Zt) / dn, Cn = std::sin(bn * pi);
            ty = 25 - Cn * 7;
            spin = pt + 10 - Cn * 4;
            lid = 0.04f + Cn * 0.42f;
          } else if (En < Zt + dn + on) {
            float bn = (En - Zt - dn) / on;
            float Cn = 1 - std::pow(1 - bn, 2.2f);
            ty = 25 - 19 * Cn;
            spin = pt + 10 * (1 - Cn);
            lid = 0.46f + (0.34f - 0.46f) * Cn;
            if (bn > 0.32f && bn < 0.46f) lid = 0.05f;
          } else {
            ctx.slumpAt = 0;
            ctx.nodUntil = now + rand(1500, 3500);
          }
        }
        break;
      }
      case happy: {
        float Et = std::sin(mt * 2.4f);
        spin = pt + std::sin(mt * 1.2f) * 3;
        tx = std::sin(mt * 1.1f) * 2.5f;
        ty = -std::abs(Et) * 3;
        squash = 1 + Et * 0.02f;
        eyeBoost = 1.05f;
        break;
      }
      case curious:
        spin = pt + 10 + std::sin(mt * 0.7f) * 6;
        tx = std::sin(mt * 0.6f) * 5;
        ty = -2 + std::sin(mt * 0.9f) * 1.5f;
        squash = 1.01f;
        eyeBoost = 1.08f;
        if (now >= ctx.nodUntil) {
          ctx.nodUntil = now + rand(1600, 2800);
          ctx.nodEnd = now + 440;
        }
        if (now < ctx.nodEnd) {
          float Et = 1 - (ctx.nodEnd - now) / 440;
          tx += std::sin(Et * pi) * 8;
          spin += std::sin(Et * pi) * 5;
        }
        break;
      case confused: {
        float Et = std::sin(mt * 0.8f);
        spin = pt + Et * 12;
        tx = Et * 3;
        ty = std::sin(mt * 0.5f) * 2;
        squash = 1;
        lid = 0.9f;
        if (now >= ctx.impulseAt) {
          ctx.spinKick = 22;
          ctx.impulseAt = now + rand(2600, 4200);
        }
        break;
      }
      case bored:
        spin = pt - 3 + std::sin(mt * 0.25f) * 4;
        tx = std::sin(mt * 0.2f) * 4;
        ty = 5 + std::sin(mt * 0.35f) * 1.5f;
        squash = 0.99f;
        lid = 0.6f;
        eyeBoost = 0.98f;
        if (now >= ctx.impulseAt) {
          ctx.nodEnd = now + 600;
          ctx.impulseAt = now + rand(4000, 7000);
        }
        if (now < ctx.nodEnd) {
          float Et = 1 - (ctx.nodEnd - now) / 600;
          squash = 1 + std::sin(Et * pi) * 0.05f;
          ty += std::sin(Et * pi) * 3;
        }
        break;
      case proud:
        spin = pt + std::sin(mt * 0.4f) * 2.5f;
        tx = std::sin(mt * 0.35f) * 2;
        ty = -4 + std::sin(mt * 0.6f);
        squash = 1.03f;
        eyeBoost = 1.02f;
        lid = 0.9f;
        break;
      case shy:
        spin = pt - 8 + std::sin(mt * 0.5f) * 3;
        tx = -3 + std::sin(mt * 0.4f) * 2;
        ty = 3;
        squash = 0.98f;
        eyeBoost = 0.95f;
        lid = 0.85f;
        break;
      case sad:
        spin = pt + 3 + std::sin(mt * 0.3f) * 2;
        tx = std::sin(mt * 0.25f) * 1.5f;
        ty = 7 + std::sin(mt * 0.4f);
        squash = 0.97f;
        lid = 0.7f;
        eyeBoost = 0.97f;
        break;
      case laughing: {
        float Et = std::sin(mt * pi * 2 * 3.2f);
        spin = pt + Et * 4;
        tx = std::sin(mt * 2) * 2;
        ty = -std::abs(Et) * 5;
        squash = 1 + Et * 0.03f;
        lid = 0.7f;
        break;
      }
      case scared:
        spin = pt + std::sin(now * 0.04f) * 2;
        tx = -2 + std::sin(now * 0.05f) * 1.5f;
        ty = 2 + std::sin(mt * 1.5f);
        squash = 0.97f;
        eyeBoost = 1.12f;
        lid = 1.05f;
        break;
      case playful:
        spin = pt + std::sin(mt * 1.4f) * 8;
        tx = std::sin(mt * 1.1f) * 4;
        ty = -std::abs(std::sin(mt * 2.2f)) * 3;
        squash = 1 + std::sin(mt * 2.2f) * 0.015f;
        eyeBoost = 1.06f;
        break;
      case celebrate:
        spin = pt;
        tx = 0;
        ty = -std::abs(std::sin(mt * 1.6f)) * 2.5f;
        squash = 1;
        eyeBoost = 1.1f;
        lid = 1.1f;
        break;
      case dragging: {
        float En = std::fmod(dtState, 3.4f) / 3.4f;
        if (En < 0.12f) {
          tx = -16;
          ty = -22;
          spin = pt - 5;
        } else if (En < 0.62f) {
          float dn = (En - 0.12f) / 0.5f;
          tx = -16 + 32 * K2(dn);
          ty = -22 + std::sin(mt * 1.4f) * 2;
          spin = pt + std::sin(mt * 2.6f) * 6;
          eyeBoost = 1.06f;
        } else {
          float cycle = std::floor(dtState / 3.4f);
          if (cycle != ctx.dragCycle) {
            ctx.dragCycle = cycle;
            ctx.tyKick = 90;
          }
          tx = 16;
          ty = 0;
          spin = pt;
        }
        squash = 1;
        break;
      }
      case humming:
        spin = pt + std::sin(mt * 0.4f) * 2;
        tx = std::sin(mt * 0.3f) * 1.5f;
        ty = std::sin(mt * 0.7f) * 1.5f;
        squash = 1;
        break;
      case notifying:
        if (!ctx.notifyPop && dtState > 0.12f) {
          ctx.notifyPop = true;
          ctx.tyKick = -26;
          ctx.wantBlink = true;
        }
        eyeBoost = 1 + 0.05f * std::exp(-dtState * 3);
        spin = pt + 3;
        tx = 2;
        ty = -1;
        squash = 1;
        break;
      default:
        spin = pt;
        tx = 0;
        ty = 0;
        squash = 1;
        break;
    }
    return { spin, tx, ty, squash, lid, eyeBoost };
  }
} // namespace lunaredu::pet::render
