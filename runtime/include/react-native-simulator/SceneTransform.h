#pragma once

#include <react-native-simulator/Scene.h>

#include <cmath>
#include <cstring>
#include <optional>

namespace ReactNativeSimulator {

inline bool transformMatrixIsIdentity(const float* matrix) {
  static constexpr float kIdentity[16]{
      1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  for (int index = 0; index < 16; ++index) {
    if (std::fabs(matrix[index] - kIdentity[index]) > 1.0e-6f) {
      return false;
    }
  }
  return true;
}

// 3x3 homography for the z=0 plane of a column-major 4x4:
//   [ a  c  tx ]     [ m0 m4 m12 ]
//   [ b  d  ty ]  =  [ m1 m5 m13 ]
//   [ p0 p1 p2 ]     [ m3 m7 m15 ]
struct Homography2D {
  float a{1};
  float b{0};
  float c{0};
  float d{1};
  float tx{0};
  float ty{0};
  float p0{0};
  float p1{0};
  float p2{1};

  static Homography2D translate(float x, float y) {
    return {1, 0, 0, 1, x, y, 0, 0, 1};
  }

  Homography2D operator*(const Homography2D& other) const {
    return {
        a * other.a + c * other.b + tx * other.p0,
        b * other.a + d * other.b + ty * other.p0,
        a * other.c + c * other.d + tx * other.p1,
        b * other.c + d * other.d + ty * other.p1,
        a * other.tx + c * other.ty + tx * other.p2,
        b * other.tx + d * other.ty + ty * other.p2,
        p0 * other.a + p1 * other.b + p2 * other.p0,
        p0 * other.c + p1 * other.d + p2 * other.p1,
        p0 * other.tx + p1 * other.ty + p2 * other.p2};
  }

  void map(float x, float y, float& outX, float& outY) const {
    const auto xw = a * x + c * y + tx;
    const auto yw = b * x + d * y + ty;
    const auto w = p0 * x + p1 * y + p2;
    if (!std::isfinite(w) || std::fabs(w) < 1.0e-8f) {
      outX = xw;
      outY = yw;
      return;
    }
    outX = xw / w;
    outY = yw / w;
  }

  std::optional<Homography2D> inverted() const {
    const float c00 = d * p2 - ty * p1;
    const float c01 = ty * p0 - b * p2;
    const float c02 = b * p1 - d * p0;
    const float det = a * c00 + c * c01 + tx * c02;
    if (!std::isfinite(det) || std::fabs(det) < 1.0e-8f) {
      return std::nullopt;
    }
    const float inv = 1.0f / det;
    Homography2D out;
    out.a = c00 * inv;
    out.c = (tx * p1 - c * p2) * inv;
    out.tx = (c * ty - tx * d) * inv;
    out.b = c01 * inv;
    out.d = (a * p2 - tx * p0) * inv;
    out.ty = (tx * b - a * ty) * inv;
    out.p0 = c02 * inv;
    out.p1 = (c * p0 - a * p1) * inv;
    out.p2 = (a * d - c * b) * inv;
    return out;
  }
};

inline void multiplyColMajor4(const float* lhs, const float* rhs, float* out);

inline Homography2D z0Homography(const float* colMajor) {
  return {
      colMajor[0],
      colMajor[1],
      colMajor[4],
      colMajor[5],
      colMajor[12],
      colMajor[13],
      colMajor[3],
      colMajor[7],
      colMajor[15]};
}

// Android View.setTransformProperty decomposes the RN 4x4 (column-major read
// as i*4+j, matching MatrixMathHelper) into scale / Euler / translation, then
// drops shear. With perspective in the list, SkewMatrixHelper is skipped, so
// skewY(45deg)+perspective becomes scale(√2, 1/√2) * rotateZ(45deg) — a diamond
// — instead of a vertical parallelogram.
struct DecomposedViewTransform {
  float scaleX{1};
  float scaleY{1};
  float scaleZ{1};
  float rotationXDeg{0};
  float rotationYDeg{0};
  float rotationZDeg{0};
  float translateX{0};
  float translateY{0};
  float translateZ{0};
  float perspectiveZ{0};
  bool valid{false};
};

inline bool matrixComponentNonZero(float value) {
  return std::fabs(value) > 1.0e-5f;
}

inline bool decomposeViewTransform(
    const float* colMajor,
    DecomposedViewTransform& out) {
  out = {};
  const double m15 = static_cast<double>(colMajor[15]);
  if (!std::isfinite(m15) || std::fabs(m15) < 1.0e-5) {
    return false;
  }
  double matrix[4][4];
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      matrix[i][j] = static_cast<double>(colMajor[i * 4 + j]) / m15;
    }
  }
  double row[3][3];
  for (int i = 0; i < 3; ++i) {
    row[i][0] = matrix[i][0];
    row[i][1] = matrix[i][1];
    row[i][2] = matrix[i][2];
  }
  auto length = [](const double v[3]) {
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  };
  auto normalize = [](double v[3], double norm) {
    const double im = 1.0 / (std::fabs(norm) < 1.0e-5 ? 1.0 : norm);
    v[0] *= im;
    v[1] *= im;
    v[2] *= im;
  };
  auto dot = [](const double a[3], const double b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  };

  const double scaleX = length(row[0]);
  if (scaleX < 1.0e-5) {
    return false;
  }
  normalize(row[0], scaleX);
  const double shearXY = dot(row[0], row[1]);
  row[1][0] -= row[0][0] * shearXY;
  row[1][1] -= row[0][1] * shearXY;
  row[1][2] -= row[0][2] * shearXY;
  const double scaleY = length(row[1]);
  if (scaleY < 1.0e-5) {
    return false;
  }
  normalize(row[1], scaleY);
  const double shearXZ = dot(row[0], row[2]);
  row[2][0] -= row[0][0] * shearXZ;
  row[2][1] -= row[0][1] * shearXZ;
  row[2][2] -= row[0][2] * shearXZ;
  const double shearYZ = dot(row[1], row[2]);
  row[2][0] -= row[1][0] * shearYZ;
  row[2][1] -= row[1][1] * shearYZ;
  row[2][2] -= row[1][2] * shearYZ;
  const double scaleZ = length(row[2]);
  if (scaleZ < 1.0e-5) {
    return false;
  }
  normalize(row[2], scaleZ);

  const double pdum0 = row[1][1] * row[2][2] - row[1][2] * row[2][1];
  const double pdum1 = row[1][2] * row[2][0] - row[1][0] * row[2][2];
  const double pdum2 = row[1][0] * row[2][1] - row[1][1] * row[2][0];
  if (row[0][0] * pdum0 + row[0][1] * pdum1 + row[0][2] * pdum2 < 0) {
    out.scaleX = static_cast<float>(-scaleX);
    out.scaleY = static_cast<float>(-scaleY);
    out.scaleZ = static_cast<float>(-scaleZ);
    for (int i = 0; i < 3; ++i) {
      row[i][0] = -row[i][0];
      row[i][1] = -row[i][1];
      row[i][2] = -row[i][2];
    }
  } else {
    out.scaleX = static_cast<float>(scaleX);
    out.scaleY = static_cast<float>(scaleY);
    out.scaleZ = static_cast<float>(scaleZ);
  }

  constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;
  auto round3 = [](double n) {
    return std::round(n * 1000.0) * 0.001;
  };
  out.rotationXDeg = static_cast<float>(
      round3(-std::atan2(row[2][1], row[2][2]) * kRadToDeg));
  out.rotationYDeg = static_cast<float>(round3(
      -std::atan2(
          -row[2][0],
          std::sqrt(row[2][1] * row[2][1] + row[2][2] * row[2][2])) *
      kRadToDeg));
  out.rotationZDeg = static_cast<float>(
      round3(-std::atan2(row[1][0], row[0][0]) * kRadToDeg));
  out.translateX = static_cast<float>(matrix[3][0]);
  out.translateY = static_cast<float>(matrix[3][1]);
  out.translateZ = static_cast<float>(matrix[3][2]);

  auto isZero = [](double d) {
    return std::isfinite(d) && std::fabs(d) < 1.0e-5;
  };
  if (!isZero(matrix[0][3]) || !isZero(matrix[1][3]) || !isZero(matrix[2][3])) {
    double perspMat[16];
    for (int i = 0; i < 4; ++i) {
      for (int j = 0; j < 4; ++j) {
        perspMat[i * 4 + j] = j == 3 ? 0.0 : matrix[i][j];
      }
    }
    perspMat[15] = 1.0;
    double inv[16];
    auto invert4 = [](const double* src, double* dst) {
      double a[4][8];
      for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
          a[i][j] = src[i * 4 + j];
          a[i][j + 4] = i == j ? 1.0 : 0.0;
        }
      }
      for (int col = 0; col < 4; ++col) {
        int pivot = col;
        for (int row = col + 1; row < 4; ++row) {
          if (std::fabs(a[row][col]) > std::fabs(a[pivot][col])) {
            pivot = row;
          }
        }
        if (std::fabs(a[pivot][col]) < 1.0e-12) {
          return false;
        }
        if (pivot != col) {
          for (int j = 0; j < 8; ++j) {
            const double tmp = a[col][j];
            a[col][j] = a[pivot][j];
            a[pivot][j] = tmp;
          }
        }
        const double div = a[col][col];
        for (int j = 0; j < 8; ++j) {
          a[col][j] /= div;
        }
        for (int row = 0; row < 4; ++row) {
          if (row == col) {
            continue;
          }
          const double factor = a[row][col];
          for (int j = 0; j < 8; ++j) {
            a[row][j] -= factor * a[col][j];
          }
        }
      }
      for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
          dst[i * 4 + j] = a[i][j + 4];
        }
      }
      return true;
    };
    if (invert4(perspMat, inv)) {
      // rhs * transpose(inv), matching MatrixMathHelper.multiplyVectorByMatrix
      const double rhs[4]{
          matrix[0][3], matrix[1][3], matrix[2][3], matrix[3][3]};
      // rhs * transpose(inv): perspective[2] uses column 2 of inv.
      out.perspectiveZ = static_cast<float>(
          rhs[0] * inv[8] + rhs[1] * inv[9] + rhs[2] * inv[10] +
          rhs[3] * inv[11]);
    }
  }

  out.valid = true;
  return true;
}

inline bool usesAndroidPlanarPresentation(const float* colMajor) {
  if (!matrixComponentNonZero(colMajor[11]) &&
      !matrixComponentNonZero(colMajor[3]) &&
      !matrixComponentNonZero(colMajor[7])) {
    return false;
  }
  DecomposedViewTransform decomposed;
  if (!decomposeViewTransform(colMajor, decomposed)) {
    return false;
  }
  return std::fabs(decomposed.rotationXDeg) < 0.5f &&
      std::fabs(decomposed.rotationYDeg) < 0.5f;
}

inline Homography2D androidPlanarHomography(
    const DecomposedViewTransform& decomposed,
    float pivotX,
    float pivotY) {
  const float radians =
      decomposed.rotationZDeg * 3.14159265f / 180.0f;
  const float cosine = std::cos(radians);
  const float sine = std::sin(radians);
  Homography2D rotationScale;
  rotationScale.a = cosine * decomposed.scaleX;
  rotationScale.c = -sine * decomposed.scaleY;
  rotationScale.b = sine * decomposed.scaleX;
  rotationScale.d = cosine * decomposed.scaleY;
  rotationScale.tx = decomposed.translateX;
  rotationScale.ty = decomposed.translateY;
  return Homography2D::translate(pivotX, pivotY) * rotationScale *
      Homography2D::translate(-pivotX, -pivotY);
}

inline void setIdentityColMajor4(float* matrix) {
  static constexpr float kIdentity[16]{
      1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  std::memcpy(matrix, kIdentity, sizeof(kIdentity));
}

inline void composeAndroid3DMatrix(
    const DecomposedViewTransform& decomposed,
    float* out) {
  float perspective[16];
  float rotateX[16];
  float rotateY[16];
  float rotateZ[16];
  float scale[16];
  setIdentityColMajor4(perspective);
  setIdentityColMajor4(rotateX);
  setIdentityColMajor4(rotateY);
  setIdentityColMajor4(rotateZ);
  setIdentityColMajor4(scale);
  if (std::fabs(decomposed.perspectiveZ) > 1.0e-6f) {
    const float distance = -1.0f / decomposed.perspectiveZ;
    if (std::isfinite(distance) && std::fabs(distance) > 1.0e-3f) {
      perspective[11] = -1.0f / distance;
    }
  }
  const float rx = decomposed.rotationXDeg * 3.14159265f / 180.0f;
  const float ry = decomposed.rotationYDeg * 3.14159265f / 180.0f;
  const float rz = decomposed.rotationZDeg * 3.14159265f / 180.0f;
  const float cx = std::cos(rx);
  const float sx = std::sin(rx);
  rotateX[5] = cx;
  rotateX[6] = sx;
  rotateX[9] = -sx;
  rotateX[10] = cx;
  const float cy = std::cos(ry);
  const float sy = std::sin(ry);
  rotateY[0] = cy;
  rotateY[2] = -sy;
  rotateY[8] = sy;
  rotateY[10] = cy;
  const float cz = std::cos(rz);
  const float sz = std::sin(rz);
  rotateZ[0] = cz;
  rotateZ[1] = sz;
  rotateZ[4] = -sz;
  rotateZ[5] = cz;
  scale[0] = decomposed.scaleX;
  scale[5] = decomposed.scaleY;
  scale[12] = decomposed.translateX;
  scale[13] = decomposed.translateY;
  float tmpA[16];
  float tmpB[16];
  multiplyColMajor4(rotateZ, scale, tmpA);
  multiplyColMajor4(rotateY, tmpA, tmpB);
  multiplyColMajor4(rotateX, tmpB, tmpA);
  multiplyColMajor4(perspective, tmpA, out);
}

inline Homography2D android3DHomography(
    const DecomposedViewTransform& decomposed,
    float pivotX,
    float pivotY) {
  float matrix[16];
  composeAndroid3DMatrix(decomposed, matrix);
  const float translate[16]{
      1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, pivotX, pivotY, 0, 1};
  const float invTranslate[16]{
      1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -pivotX, -pivotY, 0, 1};
  float tmp[16];
  float composed[16];
  multiplyColMajor4(matrix, invTranslate, tmp);
  multiplyColMajor4(translate, tmp, composed);
  return z0Homography(composed);
}

// Column-vector multiply: out = lhs * rhs (rhs applied first).
inline void multiplyColMajor4(const float* lhs, const float* rhs, float* out) {
  for (int column = 0; column < 4; ++column) {
    const float r0 = rhs[column * 4 + 0];
    const float r1 = rhs[column * 4 + 1];
    const float r2 = rhs[column * 4 + 2];
    const float r3 = rhs[column * 4 + 3];
    out[column * 4 + 0] =
        r0 * lhs[0] + r1 * lhs[4] + r2 * lhs[8] + r3 * lhs[12];
    out[column * 4 + 1] =
        r0 * lhs[1] + r1 * lhs[5] + r2 * lhs[9] + r3 * lhs[13];
    out[column * 4 + 2] =
        r0 * lhs[2] + r1 * lhs[6] + r2 * lhs[10] + r3 * lhs[14];
    out[column * 4 + 3] =
        r0 * lhs[3] + r1 * lhs[7] + r2 * lhs[11] + r3 * lhs[15];
  }
}

inline Homography2D nodePivotTransform(
    const SceneNode& node,
    float originX,
    float originY) {
  if (!node.hasTransform) {
    return {};
  }
  const float pivotX = originX + node.width * 0.5f;
  const float pivotY = originY + node.height * 0.5f;
  DecomposedViewTransform decomposed;
  const bool hasPerspective = matrixComponentNonZero(node.transformM[11]) ||
      matrixComponentNonZero(node.transformM[3]) ||
      matrixComponentNonZero(node.transformM[7]);
  if (hasPerspective &&
      decomposeViewTransform(node.transformM, decomposed)) {
    if (std::fabs(decomposed.rotationXDeg) < 0.5f &&
        std::fabs(decomposed.rotationYDeg) < 0.5f) {
      return androidPlanarHomography(decomposed, pivotX, pivotY);
    }
    return android3DHomography(decomposed, pivotX, pivotY);
  }
  const float translate[16]{
      1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, pivotX, pivotY, 0, 1};
  const float invTranslate[16]{
      1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -pivotX, -pivotY, 0, 1};
  float tmp[16];
  float composed[16];
  multiplyColMajor4(node.transformM, invTranslate, tmp);
  multiplyColMajor4(translate, tmp, composed);
  return z0Homography(composed);
}

} // namespace ReactNativeSimulator
