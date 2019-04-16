#include "vcCamera.h"
#include "vcState.h"
#include "vcPOI.h"

#include "imgui.h"
#include "imgui_ex/ImGuizmo.h"

#define ONE_PIXEL_SQ 0.0001
#define LENSNAME(x) #x+5

const char *lensNameArray[] =
{
  LENSNAME(vcLS_Custom),
  LENSNAME(vcLS_15mm),
  LENSNAME(vcLS_24mm),
  LENSNAME(vcLS_30mm),
  LENSNAME(vcLS_50mm),
  LENSNAME(vcLS_70mm),
  LENSNAME(vcLS_100mm),
};

UDCOMPILEASSERT(UDARRAYSIZE(lensNameArray) == vcLS_TotalLenses, "Lens Name not in Strings");

// higher == quicker smoothing
static const double sCameraTranslationSmoothingSpeed = 15.0;
static const double sCameraRotationSmoothingSpeed = 20.0;

const char** vcCamera_GetLensNames()
{
  return lensNameArray;
}

udDouble4x4 vcCamera_GetMatrix(vcCamera *pCamera)
{
  udQuaternion<double> orientation = udQuaternion<double>::create(pCamera->eulerRotation);
  udDouble3 lookPos = pCamera->position + orientation.apply(udDouble3::create(0.0, 1.0, 0.0));
  return udDouble4x4::lookAt(pCamera->position, lookPos, orientation.apply(udDouble3::create(0.0, 0.0, 1.0)));
}

void vcCamera_StopSmoothing(vcCameraInput *pCamInput)
{
  pCamInput->smoothTranslation = udDouble3::zero();
  pCamInput->smoothRotation = udDouble3::zero();
  pCamInput->smoothOrthographicChange = 0.0;
}

void vcCamera_UpdateSmoothing(vcCamera *pCamera, vcCameraInput *pCamInput, vcCameraSettings *pCamSettings, double deltaTime)
{
  static const double minSmoothingThreshold = 0.00001;

  // translation
  if (udMagSq3(pCamInput->smoothTranslation) > minSmoothingThreshold)
  {
    udDouble3 step = pCamInput->smoothTranslation * udMin(1.0, deltaTime * sCameraTranslationSmoothingSpeed);
    pCamera->position += step;
    pCamInput->smoothTranslation -= step;
  }

  // rotation
  if (udMagSq3(pCamInput->smoothRotation) > minSmoothingThreshold)
  {
    udDouble3 step = pCamInput->smoothRotation * udMin(1.0, deltaTime * sCameraRotationSmoothingSpeed);
    pCamera->eulerRotation += step;
    pCamInput->smoothRotation -= step;

    pCamera->eulerRotation.y = udClamp(pCamera->eulerRotation.y, -UD_HALF_PI, UD_HALF_PI);
    pCamera->eulerRotation.x = udMod(pCamera->eulerRotation.x, UD_2PI);
    pCamera->eulerRotation.z = udMod(pCamera->eulerRotation.z, UD_2PI);
  }

  // ortho zoom
  if (udAbs(pCamInput->smoothOrthographicChange) > minSmoothingThreshold)
  {
    double previousOrthoSize = pCamSettings->orthographicSize;

    double step = pCamInput->smoothOrthographicChange * udMin(1.0, deltaTime * sCameraTranslationSmoothingSpeed);
    pCamSettings->orthographicSize = udClamp(pCamSettings->orthographicSize * (1.0 - step), vcSL_CameraOrthoNearFarPlane.x, vcSL_CameraOrthoNearFarPlane.y);
    pCamInput->smoothOrthographicChange -= step;

    udDouble2 towards = pCamInput->worldAnchorPoint.toVector2() - pCamera->position.toVector2();
    if (udMagSq2(towards) > 0)
    {
      towards = (pCamInput->worldAnchorPoint.toVector2() - pCamera->position.toVector2()) / previousOrthoSize;
      pCamera->position += udDouble3::create(towards * - (pCamSettings->orthographicSize - previousOrthoSize), 0.0);
    }
  }
}

void vcCamera_BeginCameraPivotModeMouseBinding(vcState *pProgramState, int bindingIndex)
{
  switch (pProgramState->settings.camera.cameraMouseBindings[bindingIndex])
  {
  case vcCPM_Orbit:
    if (pProgramState->pickingSuccess)
    {
      pProgramState->cameraInput.isUsingAnchorPoint = true;
      pProgramState->cameraInput.worldAnchorPoint = pProgramState->worldMousePos;
      pProgramState->cameraInput.inputState = vcCIS_Orbiting;
      vcCamera_StopSmoothing(&pProgramState->cameraInput);
    }
    break;
  case vcCPM_Tumble:
    pProgramState->cameraInput.inputState = vcCIS_None;
    break;
  case vcCPM_Pan:
    if (pProgramState->pickingSuccess)
    {
      pProgramState->cameraInput.isUsingAnchorPoint = true;
      pProgramState->cameraInput.worldAnchorPoint = pProgramState->worldMousePos;
    }
    pProgramState->cameraInput.anchorMouseRay = pProgramState->pCamera->worldMouseRay;
    pProgramState->cameraInput.inputState = vcCIS_Panning;
    break;
  case vcCPM_Forward:
    pProgramState->cameraInput.inputState = vcCIS_MovingForward;
    break;
  default:
    // Do nothing
    break;
  };
}

void vcCamera_UpdateMatrices(vcCamera *pCamera, const vcCameraSettings &settings, const udFloat2 &windowSize, const udFloat2 *pMousePos = nullptr)
{
  // Update matrices
  double fov = settings.fieldOfView;
  double aspect = windowSize.x / windowSize.y;
  double zNear = settings.nearPlane;
  double zFar = settings.farPlane;

  pCamera->matrices.camera = vcCamera_GetMatrix(pCamera);

#if defined(GRAPHICS_API_D3D11)
  pCamera->matrices.projectionNear = udDouble4x4::perspectiveZO(fov, aspect, 0.5f, 10000.f);
#elif defined(GRAPHICS_API_OPENGL)
  pCamera->matrices.projectionNear = udDouble4x4::perspectiveNO(fov, aspect, 0.5f, 10000.f);
#endif

  switch (settings.cameraMode)
  {
  case vcCM_OrthoMap:
    pCamera->matrices.projectionUD = udDouble4x4::orthoZO(-settings.orthographicSize * aspect, settings.orthographicSize * aspect, -settings.orthographicSize, settings.orthographicSize, vcSL_CameraOrthoNearFarPlane.x, vcSL_CameraOrthoNearFarPlane.y);
#if defined(GRAPHICS_API_OPENGL)
    pCamera->matrices.projection = udDouble4x4::orthoNO(-settings.orthographicSize * aspect, settings.orthographicSize * aspect, -settings.orthographicSize, settings.orthographicSize, vcSL_CameraOrthoNearFarPlane.x, vcSL_CameraOrthoNearFarPlane.y);
#endif
    break;
  case vcCM_FreeRoam: // fall through
  default:
    pCamera->matrices.projectionUD = udDouble4x4::perspectiveZO(fov, aspect, zNear, zFar);
#if defined(GRAPHICS_API_OPENGL)
    pCamera->matrices.projection = udDouble4x4::perspectiveNO(fov, aspect, zNear, zFar);
#endif
  }

#if defined(GRAPHICS_API_D3D11)
  pCamera->matrices.projection = pCamera->matrices.projectionUD;
#endif

  pCamera->matrices.view = pCamera->matrices.camera;
  pCamera->matrices.view.inverse();

  pCamera->matrices.viewProjection = pCamera->matrices.projection * pCamera->matrices.view;
  pCamera->matrices.inverseViewProjection = udInverse(pCamera->matrices.viewProjection);

  // Calculate the mouse ray
  if (pMousePos != nullptr)
  {
    udDouble2 mousePosClip = udDouble2::create((pMousePos->x / windowSize.x) * 2.0 - 1.0, 1.0 - (pMousePos->y / windowSize.y) * 2.0);

    double nearClipZ = 0.0;
#if defined(GRAPHICS_API_OPENGL)
    nearClipZ = -1.0;
#endif
    udDouble4 mouseNear = (pCamera->matrices.inverseViewProjection * udDouble4::create(mousePosClip, nearClipZ, 1.0));
    udDouble4 mouseFar = (pCamera->matrices.inverseViewProjection * udDouble4::create(mousePosClip, 1.0, 1.0));

    mouseNear /= mouseNear.w;
    mouseFar /= mouseFar.w;

    pCamera->worldMouseRay.position = mouseNear.toVector3();
    pCamera->worldMouseRay.direction = udNormalize3(mouseFar - mouseNear).toVector3();
  }
}

void vcCamera_Create(vcCamera **ppCamera)
{
  if (ppCamera == nullptr)
    return;

  vcCamera *pCamera = udAllocType(vcCamera, 1, udAF_Zero);

  *ppCamera = pCamera;
}

void vcCamera_Destroy(vcCamera **ppCamera)
{
  if (ppCamera != nullptr)
    udFree(*ppCamera);
}

void vcCamera_Apply(vcCamera *pCamera, vcCameraSettings *pCamSettings, vcCameraInput *pCamInput, double deltaTime, float speedModifier /* = 1.f*/)
{
  switch (pCamInput->inputState)
  {
  case vcCIS_MovingForward:
    pCamInput->keyboardInput.y += 1;
    // fall through
  case vcCIS_None:
  {
    udDouble3 addPos = udClamp(pCamInput->keyboardInput, udDouble3::create(-1, -1, -1), udDouble3::create(1, 1, 1)); // clamp in case 2 similarly mapped movement buttons are pressed
    double vertPos = addPos.z;
    addPos.z = 0.0;

    // Translation
    if (pCamSettings->cameraMode == vcCM_FreeRoam)
    {
      if (pCamSettings->moveMode == vcCMM_Plane)
      {
        addPos = (udDouble4x4::rotationYPR(pCamera->eulerRotation) * udDouble4::create(addPos, 1)).toVector3();
      }
      else if (pCamSettings->moveMode == vcCMM_Helicopter)
      {
        addPos = (udDouble4x4::rotationYPR(udDouble3::create(pCamera->eulerRotation.x, 0.0, 0.0)) * udDouble4::create(addPos, 1)).toVector3();
        addPos.z = 0.0; // might be unnecessary now
        if (addPos.x != 0.0 || addPos.y != 0.0)
          addPos = udNormalize3(addPos);
      }
    }
    else // map mode
    {
      if (vertPos != 0)
      {
        pCamInput->smoothOrthographicChange -= 0.005 * vertPos * pCamSettings->moveSpeed * speedModifier * deltaTime;
        pCamInput->worldAnchorPoint = pCamera->position; // stops translation occuring
      }
    }

    // Panning - DPAD
    pCamInput->controllerDPADInput = (udDouble4x4::rotationYPR(pCamera->eulerRotation) * udDouble4::create(pCamInput->controllerDPADInput, 1)).toVector3();

    if (pCamSettings->cameraMode == vcCM_OrthoMap || pCamSettings->moveMode == vcCMM_Helicopter)
      pCamInput->controllerDPADInput.z = 0.0;

    addPos += pCamInput->controllerDPADInput;

    addPos.z += vertPos;
    addPos *= pCamSettings->moveSpeed * speedModifier * deltaTime;
    pCamInput->smoothTranslation += addPos;

    // Check for a nan camera position and reset to zero, this allows the UI to be usable in the event of error
    if (isnan(pCamera->position.x) || isnan(pCamera->position.y) || isnan(pCamera->position.z))
      pCamera->position = udDouble3::zero();

    // Rotation
    if (pCamSettings->invertX)
      pCamInput->mouseInput.x *= -1.0;
    if (pCamSettings->invertY)
      pCamInput->mouseInput.y *= -1.0;

    pCamInput->smoothRotation += pCamInput->mouseInput * 0.5;
  }
  break;

  case vcCIS_Orbiting:
  {
    double distanceToPointSqr = udMagSq3(pCamInput->worldAnchorPoint - pCamera->position);
    if (distanceToPointSqr != 0.0 && (pCamInput->mouseInput.x != 0 || pCamInput->mouseInput.y != 0))
    {
      udRay<double> transform, tempTransform;
      transform.position = pCamera->position;
      transform.direction = udMath_DirFromYPR(pCamera->eulerRotation);
      if (pCamSettings->invertX)
        pCamInput->mouseInput.x *= -1.0;
      if (pCamSettings->invertY)
        pCamInput->mouseInput.y *= -1.0;

      // Apply input
      tempTransform = udRotateAround(transform, pCamInput->worldAnchorPoint, { 0, 0, 1 }, pCamInput->mouseInput.x);
      transform = udRotateAround(tempTransform, pCamInput->worldAnchorPoint, udDoubleQuat::create(udMath_DirToYPR(tempTransform.direction)).apply({ 1, 0, 0 }), pCamInput->mouseInput.y);

      // Prevent flipping
      if ((transform.direction.x > 0 && tempTransform.direction.x < 0) || (transform.direction.x < 0 && tempTransform.direction.x > 0))
        transform = tempTransform;

      udDouble3 euler = udMath_DirToYPR(transform.direction);

      // Handle special case where ATan2 is ambiguous
      if (pCamera->eulerRotation.y == -UD_HALF_PI)
        euler.x += UD_PI;

      // Apply transform
      pCamera->position = transform.position;
      pCamera->eulerRotation = euler;
      pCamera->eulerRotation.z = 0;
    }
  }
  break;

  case vcCIS_LookingAtPoint:
  {
    udDouble3 lookVector = pCamInput->lookAtPosition - pCamInput->startPosition;

    pCamInput->progress += deltaTime * 1.0;
    if (pCamInput->progress >= 1.0)
    {
      pCamInput->progress = 1.0;
      pCamInput->inputState = vcCIS_None;
    }

    double progress = udEase(pCamInput->progress, udET_QuadraticOut);

    udDouble3 targetEuler = udMath_DirToYPR(lookVector);
    pCamera->eulerRotation = udSlerp(pCamInput->startAngle, udDoubleQuat::create(targetEuler), progress).eulerAngles();

    if (pCamera->eulerRotation.y > UD_PI)
      pCamera->eulerRotation.y -= UD_2PI;
  }
  break;

  case vcCIS_FlyingThrough:
  {
    vcLineInfo *pLine = (vcLineInfo*)pCamInput->pObjectInfo;

    // If important things have been deleted cancel the flythrough
    if (pLine == nullptr || pLine->pPoints == nullptr || pLine->numPoints <= 1 || pCamInput->flyThroughPoint >= pLine->numPoints)
    {
      pCamInput->flyThroughPoint = 0;
      pCamInput->flyThroughActive = false;
      pCamInput->inputState = vcCIS_None;
      pCamInput->pObjectInfo = nullptr;
      break;
    }

    // Move to first point on first loop
    if (!pCamInput->flyThroughActive)
    {
      pCamInput->flyThroughActive = true;
      pCamInput->flyThroughPoint = 0;
      pCamInput->startPosition = pCamera->position;
      pCamInput->startAngle = udDoubleQuat::create(pCamera->eulerRotation);
      pCamInput->worldAnchorPoint = pLine->pPoints[pCamInput->flyThroughPoint];
      pCamInput->progress = 0.0;
      pCamInput->inputState = vcCIS_MovingToPoint;
      break;
    }

    // If target point is reached
    if (pCamInput->progress == 1.0)
    {
      pCamInput->progress = 0.0;
      pCamInput->startPosition = pLine->pPoints[pCamInput->flyThroughPoint];
      pCamInput->flyThroughPoint++;
      if (pCamInput->flyThroughPoint >= pLine->numPoints)
      {
        pCamInput->flyThroughPoint = 0;
        // Loop through points if polygon is closed
        if (!pLine->closed)
        {
          pCamInput->flyThroughActive = false;
          pCamInput->inputState = vcCIS_None;
          break;
        }
      }
      pCamInput->worldAnchorPoint = pLine->pPoints[pCamInput->flyThroughPoint];
    }

    udDouble3 moveVector = pCamInput->worldAnchorPoint - pCamInput->startPosition;

    // If consecutive points are in the same position (avoids divide by zero)
    if (moveVector == udDouble3::zero())
    {
      pCamInput->progress = 1.0;
      break;
    }

    double flyStep = pCamSettings->moveSpeed * speedModifier * deltaTime;
    pCamInput->progress = udMin(pCamInput->progress + flyStep / udMag3(moveVector), 1.0);
    udDouble3 leadingPoint = pCamInput->startPosition + moveVector * pCamInput->progress;
    udDouble3 cam2Point = leadingPoint - pCamera->position;
    double distCam2Point = udMag3(cam2Point);
    cam2Point = udNormalize3(distCam2Point == 0 ? moveVector : cam2Point); // avoids divide by zero

    // Smoothly rotate camera to face the leading point at all times
    udDouble3 targetEuler = udMath_DirToYPR(cam2Point);
    pCamera->eulerRotation = udSlerp(udDoubleQuat::create(pCamera->eulerRotation), udDoubleQuat::create(targetEuler), 0.2).eulerAngles();
    if (pCamera->eulerRotation.y > UD_PI)
      pCamera->eulerRotation.y -= UD_2PI;

    if (pCamSettings->moveMode == vcCMM_Helicopter)
      cam2Point.z = 0;

    // Slow camera if it gets too close to the leading point (100m)
    if (distCam2Point < 100)
      flyStep *= 0.9;

    pCamera->position += cam2Point * flyStep;
  }
  break;

  case vcCIS_MovingToPoint:
  {
    udDouble3 moveVector = pCamInput->worldAnchorPoint - pCamInput->startPosition;

    if (moveVector == udDouble3::zero())
      break;

    if (pCamSettings->moveMode == vcCMM_Helicopter)
      moveVector.z = 0;

    double length = udMag3(moveVector);
    double closest = udMax(0.9, (length - 100.0) / length); // gets to either 90% or within 100m

    moveVector *= closest;

    pCamInput->progress += deltaTime / 2; // 2 second travel time
    if (pCamInput->progress > 1.0)
    {
      pCamInput->progress = 1.0;
      pCamInput->inputState = vcCIS_None;
      if (pCamInput->flyThroughActive)
        pCamInput->inputState = vcCIS_FlyingThrough; // continue on to the next point
    }

    double travelProgress = udEase(pCamInput->progress, udET_CubicInOut);
    pCamera->position = pCamInput->startPosition + moveVector * travelProgress;

    udDouble3 targetEuler = udMath_DirToYPR(pCamInput->worldAnchorPoint - (pCamInput->startPosition + moveVector * closest));
    pCamera->eulerRotation = udSlerp(pCamInput->startAngle, udDoubleQuat::create(targetEuler), travelProgress).eulerAngles();

    if (pCamera->eulerRotation.y > UD_PI)
      pCamera->eulerRotation.y -= UD_2PI;
  }
  break;

  case vcCIS_CommandZooming:
  {
    udDouble3 addPos = udDouble3::zero();
    if (pCamSettings->cameraMode == vcCM_FreeRoam)
    {
      udDouble3 towards = pCamInput->worldAnchorPoint - pCamera->position;
      if (udMagSq3(towards) > 0)
      {
        double maxDistance = 0.9 * pCamSettings->farPlane; // limit to 90% of visible distance
        double distanceToPoint = udMag3(towards);
        if (pCamInput->mouseInput.y < 0)
          distanceToPoint = udClamp(maxDistance - distanceToPoint, 0.0, distanceToPoint);

        addPos = distanceToPoint * pCamInput->mouseInput.y * udNormalize3(towards);
    }
    }
    else // map mode
    {
      pCamInput->smoothOrthographicChange += pCamInput->mouseInput.y;
    }

    pCamInput->smoothTranslation += addPos;
  }
  break;

  case vcCIS_PinchZooming:
  {
    // TODO
  }
  break;

  case vcCIS_Panning:
  {
    udPlane<double> plane = udPlane<double>::create(pCamInput->worldAnchorPoint, { 0, 0, 1 });

    if (pCamSettings->cameraMode == vcCM_OrthoMap)
      plane.point.z = 0;

    if (pCamSettings->cameraMode != vcCM_OrthoMap && pCamSettings->moveMode == vcCMM_Plane)
      plane.normal = udDoubleQuat::create(pCamera->eulerRotation).apply({ 0, 1, 0 });

    udDouble3 offset = udDouble3::create(0, 0, 0);
    udDouble3 anchorOffset = udDouble3::create(0, 0, 0);
    if (udIntersect(plane, pCamera->worldMouseRay, &offset) == udR_Success && udIntersect(plane, pCamInput->anchorMouseRay, &anchorOffset) == udR_Success)
      pCamInput->smoothTranslation = (anchorOffset - offset);
  }
  break;

  case vcCIS_Count:
    break; // to cover all implemented cases
  }

  vcCamera_UpdateSmoothing(pCamera, pCamInput, pCamSettings, deltaTime);

  if (pCamInput->inputState == vcCIS_None && pCamInput->transitioningToMapMode)
  {
    pCamInput->transitioningToMapMode = false;
    pCamSettings->cameraMode = vcCM_OrthoMap; // actually swap now
  }

  // in orthographic mode, force camera straight down
  if (pCamSettings->cameraMode == vcCM_OrthoMap)
  {
    pCamera->position.z = vcSL_CameraOrthoNearFarPlane.y * 0.5;
    pCamera->eulerRotation = udDouble3::create(0.0, -UD_HALF_PI, 0.0); // down orientation
  }
}

void vcCamera_SwapMapMode(vcState *pProgramState)
{
  udDouble3 lookAtPosition = pProgramState->pCamera->position;
  double cameraHeight = pProgramState->pCamera->position.z;
  if (pProgramState->settings.camera.cameraMode == vcCM_FreeRoam)
  {
    pProgramState->settings.camera.orthographicSize = udMax(1.0, pProgramState->pCamera->position.z / vcCamera_HeightToOrthoFOVRatios[pProgramState->settings.camera.lensIndex]);

    // defer actually swapping projection mode
    pProgramState->cameraInput.transitioningToMapMode = true;

    lookAtPosition += udDouble3::create(0, 0, -1); // up
  }
  else
  {
    cameraHeight = pProgramState->settings.camera.orthographicSize * vcCamera_HeightToOrthoFOVRatios[pProgramState->settings.camera.lensIndex];
    pProgramState->settings.camera.cameraMode = vcCM_FreeRoam;
    pProgramState->cameraInput.transitioningToMapMode = false;

    lookAtPosition += udDouble3::create(0, 1, 0); // forward

    // also adjust the far plane (so things won't disappear if the view plane isn't configured correctly)
    pProgramState->settings.camera.farPlane = udMax(pProgramState->settings.camera.farPlane, float(pProgramState->settings.camera.orthographicSize * 2.0));
    pProgramState->settings.camera.nearPlane = pProgramState->settings.camera.farPlane * vcSL_CameraFarToNearPlaneRatio;
  }

  vcCamera_LookAt(pProgramState, lookAtPosition);

  pProgramState->pCamera->position.z = cameraHeight;
}


void vcCamera_LookAt(vcState *pProgramState, const udDouble3 &targetPosition)
{
  if (udMagSq3(targetPosition - pProgramState->pCamera->position) == 0.0)
    return;

  pProgramState->cameraInput.inputState = vcCIS_LookingAtPoint;
  pProgramState->cameraInput.startPosition = pProgramState->pCamera->position;
  pProgramState->cameraInput.startAngle = udDoubleQuat::create(pProgramState->pCamera->eulerRotation);
  pProgramState->cameraInput.lookAtPosition = targetPosition;
  pProgramState->cameraInput.progress = 0.0;
}

void vcCamera_HandleSceneInput(vcState *pProgramState, udDouble3 oscMove, udFloat2 windowSize, udFloat2 mousePos)
{
  ImGuiIO &io = ImGui::GetIO();

  udDouble3 keyboardInput = udDouble3::zero();
  udDouble3 mouseInput = udDouble3::zero();

  // bring in values
  keyboardInput += oscMove;

  float speedModifier = 1.f;

  ImVec2 mouseDelta = io.MouseDelta;
  float mouseWheel = io.MouseWheel;

  static bool isMouseBtnBeingHeld = false;
  static bool isRightTriggerHeld = false;
  static bool gizmoCapturedMouse = false;

  bool isBtnClicked[3] = { ImGui::IsMouseClicked(0, false), ImGui::IsMouseClicked(1, false), ImGui::IsMouseClicked(2, false) };
  bool isBtnDoubleClicked[3] = { ImGui::IsMouseDoubleClicked(0), ImGui::IsMouseDoubleClicked(1), ImGui::IsMouseDoubleClicked(2) };
  bool isBtnHeld[3] = { ImGui::IsMouseDown(0), ImGui::IsMouseDown(1), ImGui::IsMouseDown(2) };
  bool isBtnReleased[3] = { ImGui::IsMouseReleased(0), ImGui::IsMouseReleased(1), ImGui::IsMouseReleased(2) };

  isMouseBtnBeingHeld &= (isBtnHeld[0] || isBtnHeld[1] || isBtnHeld[2]);
  bool isFocused = (ImGui::IsItemHovered() || isMouseBtnBeingHeld) && !vcGizmo_IsActive() && !pProgramState->modalOpen;

  int totalButtonsHeld = 0;
  for (size_t i = 0; i < udLengthOf(isBtnHeld); ++i)
    totalButtonsHeld += isBtnHeld[i] ? 1 : 0;

  bool forceClearMouseState = (!isMouseBtnBeingHeld && !ImGui::IsItemHovered());

  // Was the gizmo just clicked on?
  gizmoCapturedMouse = gizmoCapturedMouse || (pProgramState->gizmo.operation != 0 && vcGizmo_IsHovered() && (isBtnClicked[0] || isBtnClicked[1] || isBtnClicked[2]));
  if (gizmoCapturedMouse)
  {
    // was the gizmo just released?
    gizmoCapturedMouse = isBtnHeld[0] || isBtnHeld[1] || isBtnHeld[2];
    forceClearMouseState = (forceClearMouseState || gizmoCapturedMouse);
  }

  if (forceClearMouseState)
  {
    memset(isBtnClicked, 0, sizeof(isBtnClicked));
    memset(isBtnDoubleClicked, 0, sizeof(isBtnDoubleClicked));
    memset(isBtnHeld, 0, sizeof(isBtnHeld));
    //memset(isBtnReleased, 0, sizeof(isBtnReleased)); // 09/04/19 - commented out to fix pan-release bug, check back and review once more fully tested
    mouseDelta = ImVec2();
    mouseWheel = 0.0f;
  }

  // Controller Input
  if (io.NavActive)
  {
    keyboardInput.x += io.NavInputs[ImGuiNavInput_LStickLeft]; // Left Stick Horizontal
    keyboardInput.y += -io.NavInputs[ImGuiNavInput_LStickUp]; // Left Stick Vertical
    mouseInput.x = -io.NavInputs[ImGuiNavInput_LStickRight] / 15.0f; // Right Stick Horizontal
    mouseInput.y = io.NavInputs[ImGuiNavInput_LStickDown] / 25.0f; // Right Stick Vertical

    // In Imgui the DPAD is bound to navigation, so disable DPAD panning until the issue is resolved
    //pProgramState->cameraInput.controllerDPADInput = udDouble3::create(io.NavInputs[ImGuiNavInput_DpadRight] - io.NavInputs[ImGuiNavInput_DpadLeft], 0, io.NavInputs[ImGuiNavInput_DpadUp] - io.NavInputs[ImGuiNavInput_DpadDown]);

    if (isRightTriggerHeld)
    {
      if (pProgramState->pickingSuccess && pProgramState->cameraInput.inputState == vcCIS_None)
      {
        pProgramState->cameraInput.isUsingAnchorPoint = true;
        pProgramState->cameraInput.worldAnchorPoint = pProgramState->worldMousePos;
        pProgramState->cameraInput.inputState = vcCIS_Orbiting;
        vcCamera_StopSmoothing(&pProgramState->cameraInput);
      }
      if (io.NavInputs[ImGuiNavInput_FocusNext] < 0.85f) // Right Trigger
      {
        pProgramState->cameraInput.inputState = vcCIS_None;
        isRightTriggerHeld = false;
      }
    }
    else if (io.NavInputs[ImGuiNavInput_FocusNext] > 0.85f) // Right Trigger
    {
      isRightTriggerHeld = true;
    }
    if (io.NavInputs[ImGuiNavInput_Input] && !io.NavInputsDownDuration[ImGuiNavInput_Input]) // Y Button
      vcCamera_SwapMapMode(pProgramState);
    if (io.NavInputs[ImGuiNavInput_Activate] && !io.NavInputsDownDuration[ImGuiNavInput_Activate]) // A Button
      pProgramState->settings.camera.moveMode = ((pProgramState->settings.camera.moveMode == vcCMM_Helicopter) ? vcCMM_Plane : vcCMM_Helicopter);
  }

  if (io.KeyCtrl)
    speedModifier *= 0.1f;

  if (io.KeyShift || io.NavInputs[ImGuiNavInput_FocusPrev] > 0.15f) // Left Trigger
    speedModifier *= 10.f;

  if ((!ImGui::GetIO().WantCaptureKeyboard || isFocused) && !pProgramState->modalOpen)
  {
    keyboardInput.y += io.KeysDown[SDL_SCANCODE_W] - io.KeysDown[SDL_SCANCODE_S];
    keyboardInput.x += io.KeysDown[SDL_SCANCODE_D] - io.KeysDown[SDL_SCANCODE_A];
    keyboardInput.z += io.KeysDown[SDL_SCANCODE_R] - io.KeysDown[SDL_SCANCODE_F];

    if (ImGui::IsKeyPressed(SDL_SCANCODE_SPACE, false))
      pProgramState->settings.camera.moveMode = ((pProgramState->settings.camera.moveMode == vcCMM_Helicopter) ? vcCMM_Plane : vcCMM_Helicopter);
  }

  if (isFocused)
  {
    if (keyboardInput != udDouble3::zero() || isBtnClicked[0] || isBtnClicked[1] || isBtnClicked[2]) // if input is detected, TODO: add proper any input detection
    {
      if (pProgramState->cameraInput.inputState == vcCIS_MovingToPoint || pProgramState->cameraInput.inputState == vcCIS_LookingAtPoint || pProgramState->cameraInput.inputState == vcCIS_FlyingThrough)
      {
        pProgramState->pCamera->eulerRotation.z = 0.0;
        pProgramState->cameraInput.inputState = vcCIS_None;

        if (pProgramState->cameraInput.flyThroughActive)
        {
          pProgramState->cameraInput.flyThroughActive = false;
          pProgramState->cameraInput.flyThroughPoint = 0;
        }

        if (pProgramState->cameraInput.transitioningToMapMode)
        {
          pProgramState->cameraInput.transitioningToMapMode = false;
          pProgramState->settings.camera.cameraMode = vcCM_OrthoMap; // swap immediately
        }
      }
    }
  }

  for (int i = 0; i < 3; ++i)
  {
    // Single Clicking
    if (isBtnClicked[i] && (pProgramState->cameraInput.inputState == vcCIS_None || totalButtonsHeld == 1)) // immediately override current input if this is a new button down
    {
      if (pProgramState->settings.camera.cameraMode == vcCM_FreeRoam)
      {
        vcCamera_BeginCameraPivotModeMouseBinding(pProgramState, i);
      }
      else
      {
        // orthographic always only pans
        pProgramState->cameraInput.isUsingAnchorPoint = true;
        pProgramState->cameraInput.worldAnchorPoint = pProgramState->pCamera->worldMouseRay.position;
        pProgramState->cameraInput.anchorMouseRay = pProgramState->pCamera->worldMouseRay;
        pProgramState->cameraInput.inputState = vcCIS_Panning;
      }
    }

    // Click and Hold

    if (isBtnHeld[i] && !isBtnClicked[i])
    {
      isMouseBtnBeingHeld = true;
      mouseInput.x = -mouseDelta.x / 100.0;
      mouseInput.y = -mouseDelta.y / 100.0;
      mouseInput.z = 0.0;
    }

    if (isBtnReleased[i])
    {
      if (pProgramState->settings.camera.cameraMode == vcCM_FreeRoam)
      {
        if ((pProgramState->settings.camera.cameraMouseBindings[i] == vcCPM_Orbit && pProgramState->cameraInput.inputState == vcCIS_Orbiting) ||
            (pProgramState->settings.camera.cameraMouseBindings[i] == vcCPM_Pan && pProgramState->cameraInput.inputState == vcCIS_Panning) ||
            (pProgramState->settings.camera.cameraMouseBindings[i] == vcCPM_Forward && pProgramState->cameraInput.inputState == vcCIS_MovingForward) ||
             pProgramState->cameraInput.inputState == vcCIS_CommandZooming)
        {
          pProgramState->cameraInput.inputState = vcCIS_None;

          // Should another mouse action take over? (it's being held down)
          for (int j = 0; j < 3; ++j)
          {
            if (isBtnHeld[j])
            {
              vcCamera_BeginCameraPivotModeMouseBinding(pProgramState, j);
              break;
            }
          }
        }
      }
      else // map mode
      {
        if (!isBtnHeld[0] && !isBtnHeld[1] && !isBtnHeld[2]) // nothing is pressed (remember, they're all mapped to panning)
        {
          pProgramState->cameraInput.inputState = vcCIS_None;
        }
        else if (pProgramState->cameraInput.inputState != vcCIS_Panning) // if not panning, begin (e.g. was zooming with double mouse)
        {
          // theres still a button being held, start panning
          pProgramState->cameraInput.isUsingAnchorPoint = true;
          pProgramState->cameraInput.worldAnchorPoint = pProgramState->pCamera->worldMouseRay.position;
          pProgramState->cameraInput.anchorMouseRay = pProgramState->pCamera->worldMouseRay;
          pProgramState->cameraInput.inputState = vcCIS_Panning;
        }

      }
    }

    // Double Clicking
    if (i == 0 && isBtnDoubleClicked[i]) // if left double clicked
    {
      if (pProgramState->pickingSuccess || pProgramState->settings.camera.cameraMode == vcCM_OrthoMap)
      {
        pProgramState->cameraInput.inputState = vcCIS_MovingToPoint;
        pProgramState->cameraInput.startPosition = pProgramState->pCamera->position;
        pProgramState->cameraInput.startAngle = udDoubleQuat::create(pProgramState->pCamera->eulerRotation);
        pProgramState->cameraInput.worldAnchorPoint = pProgramState->worldMousePos;
        pProgramState->cameraInput.progress = 0.0;

        if (pProgramState->settings.camera.cameraMode == vcCM_OrthoMap)
        {
          pProgramState->cameraInput.startAngle = udDoubleQuat::identity();
          pProgramState->cameraInput.worldAnchorPoint = pProgramState->pCamera->worldMouseRay.position;
        }
      }
    }
  }

  // Mouse Wheel
  const double defaultTimeouts[vcCM_Count] = { 0.25, 0.0 };
  double timeout = defaultTimeouts[pProgramState->settings.camera.cameraMode]; // How long you have to stop scrolling the scroll wheel before the point unlocks
  static double previousLockTime = 0.0;
  double currentTime = ImGui::GetTime();
  bool zooming = false;

  if (mouseWheel != 0)
  {
    zooming = true;
    if (pProgramState->settings.camera.scrollWheelMode == vcCSWM_Dolly)
    {
      if (previousLockTime < currentTime - timeout && (pProgramState->pickingSuccess || pProgramState->settings.camera.cameraMode == vcCM_OrthoMap) && pProgramState->cameraInput.inputState == vcCIS_None)
      {
        pProgramState->cameraInput.isUsingAnchorPoint = true;
        pProgramState->cameraInput.worldAnchorPoint = pProgramState->worldMousePos;
        pProgramState->cameraInput.inputState = vcCIS_CommandZooming;

        if (pProgramState->settings.camera.cameraMode == vcCM_OrthoMap)
          pProgramState->cameraInput.worldAnchorPoint = pProgramState->pCamera->worldMouseRay.position;
      }

      if (pProgramState->cameraInput.inputState == vcCIS_CommandZooming)
      {
        mouseInput.x = 0.0;
        mouseInput.y = mouseWheel / 10.f;
        mouseInput.z = 0.0;
        previousLockTime = currentTime;

        pProgramState->cameraInput.startPosition = pProgramState->pCamera->position;
      }
    }
    else
    {
      if (mouseWheel > 0)
        pProgramState->settings.camera.moveSpeed *= (1.f + mouseWheel / 10.f);
      else
        pProgramState->settings.camera.moveSpeed /= (1.f - mouseWheel / 10.f);

      pProgramState->settings.camera.moveSpeed = udClamp(pProgramState->settings.camera.moveSpeed, vcSL_CameraMinMoveSpeed, vcSL_CameraMaxMoveSpeed);
    }
  }

  if (!zooming && pProgramState->cameraInput.inputState == vcCIS_CommandZooming && previousLockTime < currentTime - timeout)
  {
    pProgramState->cameraInput.inputState = vcCIS_None;
  }

  // set pivot to send to apply function
  pProgramState->cameraInput.currentPivotMode = vcCPM_Tumble;
  if (pProgramState->settings.camera.cameraMode == vcCM_OrthoMap)
  {
    if (io.MouseDown[0] || io.MouseDown[1] || io.MouseDown[2])
      pProgramState->cameraInput.currentPivotMode = vcCPM_Pan;
  }
  else
  {
    if (io.MouseDown[0] && !io.MouseDown[1] && !io.MouseDown[2])
      pProgramState->cameraInput.currentPivotMode = pProgramState->settings.camera.cameraMouseBindings[0];

    if (!io.MouseDown[0] && io.MouseDown[1] && !io.MouseDown[2])
      pProgramState->cameraInput.currentPivotMode = pProgramState->settings.camera.cameraMouseBindings[1];

    if (!io.MouseDown[0] && !io.MouseDown[1] && io.MouseDown[2])
      pProgramState->cameraInput.currentPivotMode = pProgramState->settings.camera.cameraMouseBindings[2];
  }

  // Apply movement and rotation
  pProgramState->cameraInput.keyboardInput = keyboardInput;
  pProgramState->cameraInput.mouseInput = mouseInput;

  vcCamera_Apply(pProgramState->pCamera, &pProgramState->settings.camera, &pProgramState->cameraInput, pProgramState->deltaTime, speedModifier);

  if (pProgramState->cameraInput.inputState == vcCIS_None)
    pProgramState->cameraInput.isUsingAnchorPoint = false;

  vcCamera_UpdateMatrices(pProgramState->pCamera, pProgramState->settings.camera, windowSize, &mousePos);
}
