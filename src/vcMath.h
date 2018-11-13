#include "udPlatform/udMath.h"

template <typename T>
struct udRay
{
  udVector3<T> position;
  udQuaternion<T> orientation;

  // static members
  static udRay<T> create(const udVector3<T> &position, const udQuaternion<T> &orientation) { udRay<T> r = { position, orientation }; return r; }
  template <typename U>
  static udRay<T> create(const udRay<U> &_v) { udRay<T> r = { udVector3<T>::create(_v.position), udQuaternion<T>::create(_v.orientation) }; return r; }
};

template <typename T>
struct udPlane
{
  udVector3<T> point;
  udVector3<T> normal;

  // static members
  static udPlane<T> create(const udVector3<T> &position, const udVector3<T> &normal) { udPlane<T> r = { position, normal }; return r; }
  template <typename U>
  static udPlane<T> create(const udPlane<U> &_v) { udPlane<T> r = { udVector3<T>::create(_v.point), udVector3<T>::create(_v.normal) }; return r; }
};

template <typename T>
udRay<T> udRotateAround(udRay<T> ray, udVector3<T> center, udVector3<T> axis, T angle)
{
  udRay<T> r;

  udQuaternion<T> rotation = udQuaternion<T>::create(axis, angle);

  udVector3<T> direction = ray.position - center; // find current direction relative to center
  r.position = center + rotation.apply(direction); // define new position
  r.orientation = rotation * ray.orientation; // rotate object to keep looking at the center

  return r;
}

//Clamp with wrap
template<typename T>
inline T udClampWrap(T val, T min, T max)
{
  if (max <= min)
    return min;

  if (val < min)
    val += ((min - val + (max - min - T(1))) / (max - min)) * (max - min); // Clamp above min

  return (val - min) % (max - min) + min;
}

template<>
inline float udClampWrap(float val, float min, float max)
{
  if (max <= min)
    return min;

  if (val < min)
    val += udCeil((min - val) / (max - min)) * (max - min); // Clamp above min

  return fmodf(val - min, max - min) + min;
}

template<>
inline double udClampWrap(double val, double min, double max)
{
  if (max <= min)
    return min;

  if (val < min)
    val += udCeil((min - val) / (max - min)) * (max - min); // Clamp above min

  return fmod(val - min, max - min) + min;
}

template <typename T>
static udQuaternion<T> udQuaternionFromMatrix(const udMatrix4x4<T> &rotMat)
{
  udQuaternion<T> retVal;

  // Matrix to Quat code from http://www.euclideanspace.com/maths/geometry/rotations/conversions/matrixToQuaternion/
  T tr = rotMat.m._00 + rotMat.m._11 + rotMat.m._22;

  if (tr > T(0))
  {
    T S = (T)(udSqrt(tr + T(1)) * T(2)); // S=4*qw
    retVal.w = T(0.25) * S;
    retVal.x = (rotMat.m._21 - rotMat.m._12) / S;
    retVal.y = (rotMat.m._02 - rotMat.m._20) / S;
    retVal.z = (rotMat.m._10 - rotMat.m._01) / S;
  }
  else if ((rotMat.m._00 > rotMat.m._11) & (rotMat.m._00 > rotMat.m._22))
  {
    T S = udSqrt(T(1) + rotMat.m._00 - rotMat.m._11 - rotMat.m._22) * T(2); // S=4*qx
    retVal.w = (rotMat.m._21 - rotMat.m._12) / S;
    retVal.x = T(0.25) * S;
    retVal.y = (rotMat.m._01 + rotMat.m._10) / S;
    retVal.z = (rotMat.m._02 + rotMat.m._20) / S;
  }
  else if (rotMat.m._11 > rotMat.m._22)
  {
    T S = udSqrt(T(1) + rotMat.m._11 - rotMat.m._00 - rotMat.m._22) * T(2); // S=4*qy
    retVal.w = (rotMat.m._02 - rotMat.m._20) / S;
    retVal.x = (rotMat.m._01 + rotMat.m._10) / S;
    retVal.y = T(0.25) * S;
    retVal.z = (rotMat.m._12 + rotMat.m._21) / S;
  }
  else
  {
    T S = udSqrt(T(1) + rotMat.m._22 - rotMat.m._00 - rotMat.m._11) * T(2); // S=4*qz
    retVal.w = (rotMat.m._10 - rotMat.m._01) / S;
    retVal.x = (rotMat.m._02 + rotMat.m._20) / S;
    retVal.y = (rotMat.m._12 + rotMat.m._21) / S;
    retVal.z = T(0.25) * S;
  }
  return udNormalize(retVal);
}

template<typename T>
static void udExtractTransform(const udMatrix4x4<T> &matrix, udVector3<T> &position, udVector3<T> &scale, udQuaternion<T> &rotation)
{
  udMatrix4x4<T> mat = matrix;

  //Extract position
  position = mat.axis.t.toVector3();

  //Extract scales
  scale = udVector3<T>::create(udMag3(mat.axis.x), udMag3(mat.axis.y), udMag3(mat.axis.z));
  mat.axis.x /= scale.x;
  mat.axis.y /= scale.y;
  mat.axis.z /= scale.z;

  //Extract rotation
  rotation = udQuaternionFromMatrix(mat);
}

template <typename T>
static udVector3<T> udClosestPointOnOOBB(const udVector3<T> &point, const udMatrix4x4<T> &oobbMatrix)
{
  udVector3<T> origin = udVector3<T>::zero();
  udVector3<T> scale = udVector3<T>::zero();
  udQuaternion<T> rotation = udQuaternion<T>::identity();
  udExtractTransform(oobbMatrix, origin, scale, rotation);
  udVector3<T> localPoint = udInverse(rotation).apply(point - origin);
  return origin + rotation.apply(udClamp(localPoint, udDouble3::zero(), scale));
}

template <typename T>
static udResult udIntersect(const udPlane<T> &plane, const udRay<T> &ray, udVector3<T> *pPointInterception = nullptr, T *pIntersectionDistance = nullptr)
{
  udResult result = udR_Success;

  udVector3<T> rayDir = ray.orientation.apply({ 0, 1, 0 });
  udVector3<T> planeRay = plane.point - ray.position;

  T denom = udDot(plane.normal, rayDir);
  T distance = 0;

  UD_ERROR_IF(denom == T(0), udR_Failure_); // Parallel to the ray

  distance = udDot(planeRay, plane.normal) / denom;

  UD_ERROR_IF(distance < 0, udR_Failure_); // Behind the ray

  if (pPointInterception)
    *pPointInterception = ray.position + rayDir * distance;

  if (pIntersectionDistance)
    *pIntersectionDistance = distance;

epilogue:
  return result;
}