#include "vcLiveFeed.h"

#include "udServerAPI.h"

#include "vcState.h"
#include "vcRender.h"
#include "vcStrings.h"

#include "imgui.h"
#include "imgui_ex/vcImGuiSimpleWidgets.h"

#include "vcLabelRenderer.h"
#include "vcPolygonModel.h"
#include "vcStringFormat.h"

#include "udFile.h"
#include "udPlatformUtil.h"
#include "udChunkedArray.h"
#include "udStringUtil.h"

#define MAX_KEY_FRAME_COUNT 25

// the display model is 35 meters too high?
udDouble3 hackDisplayOffsetAmount = udDouble3::create(0, 0, 30.0);

struct vcLiveFeedItemLOD
{
  double distance; // Normalized Distance
  double sspixels; // Screenspace Pixels

  const char *pModelAddress;
  vcPolygonModel *pModel; // The LOD does not own this though

  const char *pPinIcon;
  vcLabelInfo *pLabelInfo;
  const char *pLabelText;
};

struct vcLiveFeedItem
{
  //udUUID uuid;
  char uuid[33];
  char type[25];

  double time;
  double refreshHeightTime;
  bool justSpawned;

  bool visible;
  bool selected;
  double lastUpdated;

  bool calculateHeadingPitch;
  udDouble2 headingPitch; // Estimated for many IOTs

  udDouble3 previousPositionLatLong; // Previous known location
  udDouble3 livePositionLatLong; // Latest known location

  //udDouble3 previousLerpedLatLong;
  udDouble3 displayPosition; // Where we're going to display the item (geolocated space)

  double tweenAmount;

  double minBoundingRadius;
  //udChunkedArray<vcLiveFeedItemLOD> lodLevels;

  udDouble3 displayPositions[MAX_KEY_FRAME_COUNT];
  int count;

  bool lineCalculated;
  vcLineInstance *pLine;
  vcLabelInfo label;
};

struct vcLiveFeedUpdateInfo
{
  vcState *pProgramState;
  vcLiveFeed *pFeed;

  bool newer; // Are we fetching newer or older data?
};

//void vcLiveFeedItem_ClearLODs(vcLiveFeedItem *pFeedItem)
//{
//  for (size_t lodLevelIndex = 0; lodLevelIndex < pFeedItem->lodLevels.length; ++lodLevelIndex)
//  {
//    vcLiveFeedItemLOD &ref = pFeedItem->lodLevels[lodLevelIndex];
//
//    udFree(ref.pPinIcon);
//    udFree(ref.pLabelText);
//    udFree(ref.pLabelInfo);
//    udFree(ref.pModelAddress);
//  }
//
//  pFeedItem->lodLevels.Deinit();
//}

udDouble4 frustumPlanes[6];

// Returns -1=outside, 0=inside, >0=partial (bits of planes crossed)
static int vcQuadTree_FrustumTest( const udDouble3 &boundCenter, const udDouble3 &boundExtents)
{
  int partial = 0;

  for (int i = 0; i < 6; ++i)
  {
    double distToCenter = udDot4(udDouble4::create(boundCenter, 1.0), frustumPlanes[i]);
    //optimized for case where boxExtents are all same: udFloat radiusBoxAtPlane = udDot3(boxExtents, udAbs(udVec3(curPlane)));
    double radiusBoxAtPlane = udDot3(boundExtents, udAbs(frustumPlanes[i].toVector3()));
    if (distToCenter < -radiusBoxAtPlane)
      return -1; // Box is entirely behind at least one plane
    else if (distToCenter <= radiusBoxAtPlane) // If spanned (not entirely infront)
      partial |= (1 << i);
  }

  return partial;
}

void vcLiveFeed_LoadModel(void *pUserData)
{
  vcLiveFeed *pFeed = (vcLiveFeed*)pUserData;
  udLockMutex(pFeed->m_pMutex);

  vcLiveFeedPolyCache *pItem = nullptr;
  for (size_t pI = 0; pI < pFeed->m_polygonModels.length; ++pI)
  {
    if (pFeed->m_polygonModels[pI].loadStatus == vcLiveFeedPolyCache::LS_InQueue)
    {
      pItem = &pFeed->m_polygonModels[pI];
      break;
    }
  }

  if (!pItem)
    return;

  pItem->loadStatus = vcLiveFeedPolyCache::LS_Downloading;
  udReleaseMutex(pFeed->m_pMutex);

  if (udFile_Load(pItem->pModelURL, &pItem->pModelData, &pItem->modelDataLength) != udR_Success)
  {
    pItem->loadStatus = vcLiveFeedPolyCache::LS_Failed;
  }
  else
  {
    pItem->loadStatus = vcLiveFeedPolyCache::LS_Downloaded;
  }
}


#include "udWeb.h"
#include "udStringUtil.h"

vcPolygonModel *pCarModel = nullptr;
vcPolygonModel *pTaxiModel = nullptr;
vcPolygonModel *pBusModel = nullptr;
vcPolygonModel *pHGVModel = nullptr;
vcPolygonModel *pMotorcycleModel = nullptr;
vcPolygonModel *pPrivateHireModel = nullptr;
vcPolygonModel *pAirQualityStationModel = nullptr;
vcPolygonModel *pCCTVModel = nullptr;

extern QueryVisualizationTexture densityMap;
udInt2 densityMapSize = udInt2::zero();
uint32_t *pDensityMapPixels = nullptr;

struct PointInfo
{
  char name[256];
  udDouble3 latLon;

  udDouble4x4 transform;
};
udChunkedArray<PointInfo> cctv;
udChunkedArray<PointInfo> airQualityStations;

void vcLiveFeed_GenerateDensityMapWT(void *pUserData)
{
  udUnused(pUserData);

  //api = { 'api': "qbF^obhbQEfDzUtNgUN3nZk1$7f6#?IA", 'query' : """
  //  SELECT cast((lat / 0.01) AS integer) as latz, cast((lon / 0.01) as integer) as lonz, COUNT(*)
  //  FROM geospock.default.hkjourneysrevisedlatest1daymedium AS event
  //  WHERE event.timestamp BETWEEN TIMESTAMP '2020-08-10 08:00:0' AND TIMESTAMP '2020-08-10 09:00:00'
  //  GROUP BY 1, 2
  //""" }

  const char *pFeedsJSON = nullptr;
  uint64_t responseLength = 0;
  int responseCode = 0;

  const char message[] = "{ \"api\": \"qbF^obhbQEfDzUtNgUN3nZk1$7f6#?IA\", \"query\": \"SELECT cast((lat / 0.0001) AS integer) as latz, cast((lon / 0.00025) as integer) as lonz, COUNT(*) FROM geospock.default.hkjourneysrevisedlatest1daymedium AS event WHERE event.timestamp BETWEEN TIMESTAMP '2020-08-10 08:00:0' AND TIMESTAMP '2020-08-10 09:00:00' GROUP BY 1, 2\"}";
  const char *pServerAddr = "http://54.183.255.231:7998/query";

  udWebOptions options = {};
  options.method = udWM_POST;
  options.pPostData = (uint8_t *)message;
  options.postLength = udLengthOf(message) - 1;

  udError vError = udWeb_RequestAdv(pServerAddr, options, &pFeedsJSON, &responseLength, &responseCode);

  udInt2 latBounds = udInt2::create(999999, -999999);
  udInt2 lonBounds = udInt2::create(999999, -999999);
  if (vError == udE_Success)
  {
    udJSON data;
    if (data.Parse(pFeedsJSON) == udR_Success)
    {
      udJSONArray *pFeeds = data.AsArray();
      for (size_t i = 0; i < pFeeds->length; ++i)
      {
        udJSONArray *pNode = pFeeds->GetElement(i)->AsArray();
        int lat = pNode->GetElement(0)->AsInt();
        int lon = pNode->GetElement(1)->AsInt();

        latBounds.x = udMin(lat, latBounds.x);
        latBounds.y= udMax(lat, latBounds.y);
        lonBounds.x = udMin(lon, lonBounds.x);
        lonBounds.y = udMax(lon, lonBounds.y);
        //int count = pNode->GetElement(2)->AsInt();
      }

      densityMapSize.x = latBounds.y - latBounds.x;
      densityMapSize.y = lonBounds.y - lonBounds.x;
      pDensityMapPixels = udAllocType(uint32_t, densityMapSize.x * densityMapSize.y, udAF_Zero);

      densityMap.boundsLatLon.x = latBounds.x / 10000.0;
      densityMap.boundsLatLon.y = latBounds.y / 10000.0;
      densityMap.boundsLatLon.z = lonBounds.x / 4000.0;
      densityMap.boundsLatLon.w = lonBounds.y / 4000.0;

      float colorRanges[] =
      {
        1000.0f,
      //  50000.0f,
        2500.0f,
      };

      for (size_t i = 0; i < pFeeds->length; ++i)
      {
        udJSONArray *pNode = pFeeds->GetElement(i)->AsArray();
        int lat = pNode->GetElement(0)->AsInt();
        int lon = pNode->GetElement(1)->AsInt();

        int count = pNode->GetElement(2)->AsInt();

        int pixelX = ((lat - latBounds.x) - 1);// / (latBounds.y - latBounds.x));
        int pixelY = ((lon - lonBounds.x) - 1);// / (lonBounds.y - lonBounds.x));

        //udFloat3 hsv = udFloat3::create(1.0f - udMin(1.0f, count / 100000.0f), 1, 1);
        //udFloat3 rgb = hsv2rgb(hsv);

       // int r = (int)(rgb.x * 255);
        //int g = (int)(rgb.y * 255);
       // int b = (int)(rgb.z * 255);
        int g = 0;
        int b = 0;
        int r = 0;

        udFloat3 colA = udFloat3::create(0, 0, 0);
        udFloat3 colB = udFloat3::create(0, 1, 0);
        udFloat3 colC = udFloat3::create(1, 0, 0);

        udFloat3 colAB = udLerp(colA, colB, udMin(1.0f, count / colorRanges[0]));
        udFloat3 finalCol = udLerp(colAB, colC, udMin(1.0f, count / colorRanges[1]));
        ////if (count <= colorRanges[0])
        //  g = ((int)(udClamp(count / colorRanges[0], 0.0f, 1.0f) * 255));
        ////else if (count <= colorRanges[1])
        ////  b = ((int)(udClamp((count - colorRanges[0]) / colorRanges[1], 0.0f, 1.0f) * 255));
        ////else if (count <= colorRanges[1])
        //  r = ((int)(udClamp((count - colorRanges[0]) / colorRanges[1], 0.0f, 1.0f) * 255));
        ////else
        ////  r = 255;

        r = (int)(finalCol.x * 255);
        g = (int)(finalCol.y * 255);
        b = (int)(finalCol.z * 255);

        uint32_t pixelValue = (0xff000000) | r | (g << 8) | (b << 16);

        pDensityMapPixels[udMax(0, pixelX + pixelY * densityMapSize.x)] += pixelValue;
      }
    }
  }
}

void vcLiveFeed_GenerateDensityMapMT(void *pUserData)
{
  udUnused(pUserData);

  vcTexture_Create(&densityMap.pTexture, densityMapSize.x, densityMapSize.y, pDensityMapPixels);
}

void vcLiveFeed_GenerateCCTVPointsWT(void *pUserData)
{
  vcLiveFeedUpdateInfo *pInfo = (vcLiveFeedUpdateInfo *)pUserData;

  cctv.Init(128);

  //api = { 'api': "qbF^obhbQEfDzUtNgUN3nZk1$7f6#?IA", 'query' : """
  //  SELECT cast((lat / 0.01) AS integer) as latz, cast((lon / 0.01) as integer) as lonz, COUNT(*)
  //  FROM geospock.default.hkjourneysrevisedlatest1daymedium AS event
  //  WHERE event.timestamp BETWEEN TIMESTAMP '2020-08-10 08:00:0' AND TIMESTAMP '2020-08-10 09:00:00'
  //  GROUP BY 1, 2
  //""" }

  const char *pFeedsJSON = nullptr;
  uint64_t responseLength = 0;
  int responseCode = 0;

  const char message[] = "{ \"api\": \"qbF^obhbQEfDzUtNgUN3nZk1$7f6#?IA\", \"query\": \"SELECT lat, lon, description FROM geospock.default.hongkongcctv1 AS poi\"}";
  const char *pServerAddr = "http://54.183.255.231:7998/query";

  udWebOptions options = {};
  options.method = udWM_POST;
  options.pPostData = (uint8_t *)message;
  options.postLength = udLengthOf(message) - 1;

  udError vError = udWeb_RequestAdv(pServerAddr, options, &pFeedsJSON, &responseLength, &responseCode);

  if (vError == udE_Success)
  {
    udLockMutex(pInfo->pFeed->m_pMutex);
    udJSON data;
    if (data.Parse(pFeedsJSON) == udR_Success)
    {
      udJSONArray *pFeeds = data.AsArray();
      for (size_t i = 0; i < pFeeds->length; ++i)
      {
        udJSONArray *pNode = pFeeds->GetElement(i)->AsArray();

        PointInfo *pNewPoint = cctv.PushBack();
        pNewPoint->latLon.x = pNode->GetElement(0)->AsDouble();
        pNewPoint->latLon.y = pNode->GetElement(1)->AsDouble();
        udStrcpy(pNewPoint->name, pNode->GetElement(2)->AsString());
      }
    }

    udReleaseMutex(pInfo->pFeed->m_pMutex);
  }
}

void vcLiveFeed_GenerateCCTVPointsMT(void *pUserData)
{
  vcLiveFeedUpdateInfo *pInfo = (vcLiveFeedUpdateInfo *)pUserData;

  udProjectNode *pParent = nullptr;
  if (udProjectNode_Create(pInfo->pProgramState->activeProject.pProject, &pParent, pInfo->pProgramState->activeProject.pRoot, "Folder", "CCTV Locations", nullptr, nullptr) != udE_Success)
  {
    // error message
  }

  for (int i = 0; i < cctv.length; ++i)
  {
    const char *pNodeName = cctv[i].name;
    udDouble3 lonLat = udDouble3::create(cctv[i].latLon.y, cctv[i].latLon.x, hackDisplayOffsetAmount.z + 15.0f);
    udDouble3 position = udGeoZone_LatLongToCartesian(pInfo->pProgramState->geozone, cctv[i].latLon, false);

    // place on maps, then offset slightly
    position = vcRender_QueryMapAtCartesian(pInfo->pProgramState->pActiveViewport->pRenderContext, position) + hackDisplayOffsetAmount;
    cctv[i].transform = udDouble4x4::scaleUniform(0.025f, position);

    // add node to project
    udProjectNode *pNode = nullptr;
    if (udProjectNode_Create(pInfo->pProgramState->activeProject.pProject, &pNode, pParent, "POI", pNodeName, nullptr, nullptr) != udE_Success)
    {
      // error UI
    }

    if (udProjectNode_SetGeometry(pInfo->pProgramState->activeProject.pProject, pNode, udPGT_Point, 1, (double *)&lonLat) != udE_Success)
    {
      // error ui
    }
  }
}

void vcLiveFeed_GenerateAirStationPointsWT(void *pUserData)
{
  vcLiveFeedUpdateInfo *pInfo = (vcLiveFeedUpdateInfo *)pUserData;

  airQualityStations.Init(128);

  //api = { 'api': "qbF^obhbQEfDzUtNgUN3nZk1$7f6#?IA", 'query' : """
  //  SELECT cast((lat / 0.01) AS integer) as latz, cast((lon / 0.01) as integer) as lonz, COUNT(*)
  //  FROM geospock.default.hkjourneysrevisedlatest1daymedium AS event
  //  WHERE event.timestamp BETWEEN TIMESTAMP '2020-08-10 08:00:0' AND TIMESTAMP '2020-08-10 09:00:00'
  //  GROUP BY 1, 2
  //""" }

  const char *pFeedsJSON = nullptr;
  uint64_t responseLength = 0;
  int responseCode = 0;

  const char message[] = "{ \"api\": \"qbF^obhbQEfDzUtNgUN3nZk1$7f6#?IA\", \"query\": \"SELECT lat, lon, facility_name FROM geospock.default.hkairqualitystations\"}";
  const char *pServerAddr = "http://54.183.255.231:7998/query";

  udWebOptions options = {};
  options.method = udWM_POST;
  options.pPostData = (uint8_t *)message;
  options.postLength = udLengthOf(message) - 1;

  udError vError = udWeb_RequestAdv(pServerAddr, options, &pFeedsJSON, &responseLength, &responseCode);

  if (vError == udE_Success)
  {
    udLockMutex(pInfo->pFeed->m_pMutex);
    udJSON data;
    if (data.Parse(pFeedsJSON) == udR_Success)
    {
      udJSONArray *pFeeds = data.AsArray();
      for (size_t i = 0; i < pFeeds->length; ++i)
      {
        udJSONArray *pNode = pFeeds->GetElement(i)->AsArray();

        PointInfo *pNewPoint = airQualityStations.PushBack();
        pNewPoint->latLon.x = pNode->GetElement(0)->AsDouble();
        pNewPoint->latLon.y = pNode->GetElement(1)->AsDouble();
        udStrcpy(pNewPoint->name, pNode->GetElement(2)->AsString());
      }
    }

    udReleaseMutex(pInfo->pFeed->m_pMutex);
  }
}

void vcLiveFeed_GenerateAirStationPointsMT(void *pUserData)
{
  vcLiveFeedUpdateInfo *pInfo = (vcLiveFeedUpdateInfo *)pUserData;

  udProjectNode *pParent = nullptr;
  if (udProjectNode_Create(pInfo->pProgramState->activeProject.pProject, &pParent, pInfo->pProgramState->activeProject.pRoot, "Folder", "Air Quality Stations", nullptr, nullptr) != udE_Success)
  {
    // error message
  }

  for (int i = 0; i < airQualityStations.length; ++i)
  {
    const char *pNodeName = airQualityStations[i].name;

    udDouble3 lonLat = udDouble3::create(airQualityStations[i].latLon.y, airQualityStations[i].latLon.x, hackDisplayOffsetAmount.z);
    udDouble3 position = udGeoZone_LatLongToCartesian(pInfo->pProgramState->geozone, airQualityStations[i].latLon, false);

    // place on maps, then offset slightly
    position = vcRender_QueryMapAtCartesian(pInfo->pProgramState->pActiveViewport->pRenderContext, position) + hackDisplayOffsetAmount;
    airQualityStations[i].transform = udDouble4x4::scaleUniform(0.025f, position);

    // add node to project
    udProjectNode *pNode = nullptr;
    if (udProjectNode_Create(pInfo->pProgramState->activeProject.pProject, &pNode, pParent, "POI", pNodeName, nullptr, nullptr) != udE_Success)
    {
      // error UI
    }

    if (udProjectNode_SetGeometry(pInfo->pProgramState->activeProject.pProject, pNode, udPGT_Point, 1, (double *)&lonLat) != udE_Success)
    {
      // error ui
    }
  }
}

void vcLiveFeed_UpdateFeed(void *pUserData)
{
  vcLiveFeedUpdateInfo *pInfo = (vcLiveFeedUpdateInfo*)pUserData;

  const char *pFeedsJSON = nullptr;
  uint64_t responseLength = 0;
  int responseCode = 0;

  //const char *pServerAddr = "v1/feeds/fetch";
  //const char *pMessage = udTempStr("{ \"groupid\": \"%s\", \"time\": %f, \"newer\": %s }", udUUID_GetAsString(&pInfo->pFeed->m_groupID), pInfo->newer ? pInfo->pFeed->m_newestFeedUpdate : pInfo->pFeed->m_oldestFeedUpdate, pInfo->newer ? "true" : "false");

  //const char message[] = "{ \"api\": \"qbF^obhbQEfDzUtNgUN3nZk1$7f6#?IA\", \"query\": \"SELECT * FROM geospock.default.hkjourneysrevisedlatest1daymedium AS event WHERE event.timestamp BETWEEN TIMESTAMP '2020-08-10 08:00:00' AND TIMESTAMP '2020-08-10 08:00:01'\"}";
  const char message[] = "{ \"api\": \"qbF^obhbQEfDzUtNgUN3nZk1$7f6#?IA\", \"query\": \"SELECT * FROM geospock.default.hkjourneysrevisedlatest1daylarge AS event WHERE event.timestamp BETWEEN TIMESTAMP '2020-08-10 08:00:00' AND TIMESTAMP '2020-08-10 08:00:20'\"}";
  const char *pServerAddr = "http://54.183.255.231:7998/query";

  udWebOptions options = {};
  options.method = udWM_POST;
  options.pPostData = (uint8_t*)message;
  options.postLength = udLengthOf(message) - 1;

  udError vError = udWeb_RequestAdv(pServerAddr, options, &pFeedsJSON, &responseLength, &responseCode);

 // double updatedTime = 0.0;

  pInfo->pFeed->m_lastFeedSync = udGetEpochSecsUTCf();

  //udLockMutex(pInfo->pFeed->m_pMutex);

  // todo: memory cleanup
  //pInfo->pFeed->m_feedItems.Clear();

  if (vError == udE_Success)
  {
    udJSON data;
    if (data.Parse(pFeedsJSON) == udR_Success)
    {
      udJSONArray *pFeeds = data.AsArray();
      for (size_t i = 0; i < pFeeds->length; ++i)
      {
        udJSONArray *pNode = pFeeds->GetElement(i)->AsArray();
        vcLiveFeedItem *pFeedItem = nullptr;

        //udUUID uuid;
        //if (udUUID_SetFromString(&uuid, pNode->GetElement(3)->AsString()) != udR_Success)
        //  continue;

        const char *uuid = pNode->GetElement(3)->AsString();
        const char *type = pNode->GetElement(4)->AsString();

        size_t j = 0;
        for (; j < pInfo->pFeed->m_feedItems.length; ++j)
        {
          vcLiveFeedItem *pCachedFeedItem = pInfo->pFeed->m_feedItems[j];
        
          if (udStrEqual(uuid, pCachedFeedItem->uuid))
          {
            pFeedItem = pCachedFeedItem;
            break;
          }
        }

        udDouble3 newPositionLatLong = udDouble3::create(pNode->GetElement(0)->AsDouble(), pNode->GetElement(1)->AsDouble(), 0.0);// pNode->Get("geometry.coordinates").AsDouble3();
        //double updated = pNode->Get("updated").AsDouble();
        //pFeedItem->count++;

        if (pFeedItem == nullptr)
        {
          pFeedItem = udAllocType(vcLiveFeedItem, 1, udAF_Zero);

          vcLineRenderer_CreateLine(&pFeedItem->pLine);

          pFeedItem->label.backColourRGBA = 0x7f000000;
          pFeedItem->label.textColourRGBA = 0xffffffff;
          pFeedItem->label.pSceneItem = pInfo->pFeed;
          pFeedItem->label.pText;
          //pFeedItem->justSpawned = true;
          //pFeedItem->lodLevels.Init(4);
          //pFeedItem->uuid = uuid;
          udStrcpy(pFeedItem->uuid, uuid);
          udStrcpy(pFeedItem->type, type);
          udLockMutex(pInfo->pFeed->m_pMutex);
          pInfo->pFeed->m_feedItems.PushBack(pFeedItem);
          udReleaseMutex(pInfo->pFeed->m_pMutex);

          pFeedItem->previousPositionLatLong = newPositionLatLong;
          //pFeedItem->displayPosition = udDouble3::zero();
          pFeedItem->tweenAmount = 1.0f;
        }

        if (pFeedItem->count < MAX_KEY_FRAME_COUNT - 1)
        {
          pFeedItem->displayPositions[pFeedItem->count] = udGeoZone_LatLongToCartesian(pInfo->pProgramState->geozone, newPositionLatLong, false);
          pFeedItem->displayPositions[pFeedItem->count] = vcRender_QueryMapAtCartesian(pInfo->pProgramState->pActiveViewport->pRenderContext, pFeedItem->displayPositions[pFeedItem->count]) + hackDisplayOffsetAmount;
          pFeedItem->count++;
        }
      }
    }
  }

 // udReleaseMutex(pInfo->pFeed->m_pMutex);

//epilogue:
  udServerAPI_ReleaseResult(&pFeedsJSON);

  pInfo->pFeed->m_loadStatus = vcSLS_Loaded;
}


vcLiveFeed::vcLiveFeed(vcProject *pProject, udProjectNode *pNode, vcState *pProgramState) :
  vcSceneItem(pProject, pNode, pProgramState),
  m_selectedItem(0),
  m_visibleItems(0),
  m_tweenPositionAndOrientation(true),
  m_updateFrequency(15.0),
  m_decayFrequency(300.0),
  m_maxDisplayDistance(50000.0),
  m_pMutex(udCreateMutex())
{
  m_feedItems.Init(512);
  m_polygonModels.Init(16);

  m_lastFeedSync = 0.0;
  m_newestFeedUpdate = udGetEpochSecsUTCf() - m_decayFrequency;
  m_oldestFeedUpdate = m_newestFeedUpdate;
  m_fetchNow = true;

  m_labelLODModifier = 1.0;

  OnNodeUpdate(pProgramState);

  m_loadStatus = vcSLS_Pending;

  vcPolygonModel_CreateFromURL(&pCarModel, "asset://assets//models//Car//car low poly.obj", pProgramState->pWorkerPool);
  vcPolygonModel_CreateFromURL(&pTaxiModel, "asset://assets//models//Taxi//13914_Taxi_v2_L1.obj", pProgramState->pWorkerPool);
  vcPolygonModel_CreateFromURL(&pBusModel, "asset://assets//models//Bus//bus.obj", pProgramState->pWorkerPool);
  vcPolygonModel_CreateFromURL(&pHGVModel, "asset://assets//models//HGV//model.obj", pProgramState->pWorkerPool);
  vcPolygonModel_CreateFromURL(&pMotorcycleModel, "asset://assets//models//Motorcycle//model.obj", pProgramState->pWorkerPool);
  vcPolygonModel_CreateFromURL(&pPrivateHireModel, "asset://assets//models//PrivateHire//model.obj", pProgramState->pWorkerPool);
  vcPolygonModel_CreateFromURL(&pAirQualityStationModel, "asset://assets//models//AirQualityStation//model.obj", pProgramState->pWorkerPool);
  vcPolygonModel_CreateFromURL(&pCCTVModel, "asset://assets//models//CCTV//model.obj", pProgramState->pWorkerPool);
}

void vcLiveFeed::OnNodeUpdate(vcState *pProgramState)
{
  const char *pTempStr = nullptr;

  udProjectNode_GetMetadataString(m_pNode, "groupid", &pTempStr, nullptr);
  udUUID_Clear(&m_groupID);
  udUUID_SetFromString(&m_groupID, pTempStr);

  udProjectNode_GetMetadataDouble(m_pNode, "updateFrequency", &m_updateFrequency, 30.0);
  udProjectNode_GetMetadataDouble(m_pNode, "maxDisplayTime", &m_decayFrequency, 300.0);
  udProjectNode_GetMetadataDouble(m_pNode, "maxDisplayDistance", &m_maxDisplayDistance, 50000.0);
  udProjectNode_GetMetadataDouble(m_pNode, "lodModifier", &m_labelLODModifier, 1.0);

  vcProject_GetNodeMetadata(m_pNode, "tweenEnabled", &m_tweenPositionAndOrientation, true);
  vcProject_GetNodeMetadata(m_pNode, "snapToMap", &m_snapToMap, false);

  ChangeProjection(pProgramState->geozone);
}

#include "vcInternalModels.h"
#include <math.h>


void vcLiveFeed::AddToScene(vcState *pProgramState, vcRenderData *pRenderData)
{
  udDouble4x4 transposedViewProjection = udTranspose(pProgramState->pActiveViewport->camera.matrices.viewProjection);
  frustumPlanes[0] = transposedViewProjection.c[3] + transposedViewProjection.c[0]; // Left
  frustumPlanes[1] = transposedViewProjection.c[3] - transposedViewProjection.c[0]; // Right
  frustumPlanes[2] = transposedViewProjection.c[3] + transposedViewProjection.c[1]; // Bottom
  frustumPlanes[3] = transposedViewProjection.c[3] - transposedViewProjection.c[1]; // Top
  frustumPlanes[4] = transposedViewProjection.c[3] + transposedViewProjection.c[2]; // Near
  frustumPlanes[5] = transposedViewProjection.c[3] - transposedViewProjection.c[2]; // Far
  // Normalize the planes
  for (int j = 0; j < 6; ++j)
    frustumPlanes[j] /= udMag3(frustumPlanes[j]);

  udLockMutex(m_pMutex);

  // draw CCTV
  udFloat4 cctvColour = udFloat4::create(1, 1, 1, 1);
  size_t j = 0;
  for (; j < cctv.length; ++j)
  {
    pRenderData->polyModels.PushBack({ vcRenderPolyInstance::RenderType_Polygon, vcRenderPolyInstance::RenderFlags_None, pCCTVModel, cctv[j].transform, nullptr, cctvColour, vcGLSCM_Back, true, this, (uint64_t)(j + 1) });
  }

  // draw air stations
  udFloat4 airStationColour = udFloat4::create(1, 1, 1, 1);
  size_t k = 0;
  for (; k < airQualityStations.length; ++k)
  {
    pRenderData->polyModels.PushBack({ vcRenderPolyInstance::RenderType_Polygon, vcRenderPolyInstance::RenderFlags_None, pAirQualityStationModel, airQualityStations[k].transform, nullptr, airStationColour, vcGLSCM_Back, true, this, (uint64_t)(j + k + 1) });
  }

  if (!m_visible)
    return;

  double now = udGetEpochSecsUTCf();

  //double recently = now - m_decayFrequency;

  static bool gotData = false;

  if (!gotData && (m_loadStatus != vcSLS_Loading && ((now >= m_lastFeedSync + m_updateFrequency) || (now - m_decayFrequency < m_oldestFeedUpdate) || m_fetchNow)))
  {
    m_loadStatus = vcSLS_Loading;
    gotData = true;

    vcLiveFeedUpdateInfo *pInfo = udAllocType(vcLiveFeedUpdateInfo, 1, udAF_Zero);
    pInfo->pProgramState = pProgramState;
    pInfo->pFeed = this;
    pInfo->newer = ((now >= m_lastFeedSync + m_updateFrequency) || m_fetchNow);

    m_fetchNow = false;

    udWorkerPool_AddTask(pProgramState->pWorkerPool, vcLiveFeed_UpdateFeed, pInfo);

    vcLiveFeedUpdateInfo *pInfo2 = udAllocType(vcLiveFeedUpdateInfo, 1, udAF_Zero);
    pInfo2->pProgramState = pProgramState;
    pInfo2->pFeed = this;
    pInfo2->newer = ((now >= m_lastFeedSync + m_updateFrequency) || m_fetchNow);

    udWorkerPool_AddTask(pProgramState->pWorkerPool, vcLiveFeed_GenerateDensityMapWT, pInfo2, false, vcLiveFeed_GenerateDensityMapMT);
    udWorkerPool_AddTask(pProgramState->pWorkerPool, vcLiveFeed_GenerateAirStationPointsWT, pInfo2, false, vcLiveFeed_GenerateAirStationPointsMT);
    udWorkerPool_AddTask(pProgramState->pWorkerPool, vcLiveFeed_GenerateCCTVPointsWT, pInfo2, false, vcLiveFeed_GenerateCCTVPointsMT);

    // memory leak
  }


  //printf("Selected Item: %zu\n", m_selectedItem);

  m_visibleItems = 0;

  static int skipId = -1;
  size_t i = 0;
  for (; i < m_feedItems.length; ++i)
  {
    vcLiveFeedItem *pFeedItem = m_feedItems[i];

    if (pFeedItem->count == 0)
      continue;

    if (skipId != -1 && i != skipId)
      continue;

    //if (m_selectedItem != 0 && IsSubitemSelected(i + 1))
    //  continue;

    //if (i != 5000)
    //  continue;

    pFeedItem->time += pProgramState->deltaTime;

    double intPart = 0.0;
    double lerpT = modf(pFeedItem->time, &intPart);
    int index = (int)(pFeedItem->time) % pFeedItem->count;
    int nextIndex = udMin((int)(index + 1), pFeedItem->count); // dont wrap

    udDouble3 cameraPosition = pProgramState->pActiveViewport->camera.position;
    double distanceSq = udMagSq3(pFeedItem->displayPosition - cameraPosition);

    bool selected = (m_selected && m_selectedItem == i + j + k + 1);

    pFeedItem->refreshHeightTime -= pProgramState->deltaTime;
    if (selected || (pFeedItem->refreshHeightTime <= 0.0f))
    {
      pFeedItem->justSpawned = false;
      pFeedItem->refreshHeightTime = 20.0f;
      if (selected || distanceSq < (500 * 500)) // only if its close
      {
        for (int d = 0; d < pFeedItem->count; ++d)
        {
          pFeedItem->displayPositions[d] = vcRender_QueryMapAtCartesian(pProgramState->pActiveViewport->pRenderContext, pFeedItem->displayPositions[d]) + hackDisplayOffsetAmount;
        }
      }
    }

    int indexA = udMin(index, pFeedItem->count - 1);
    int indexB = udMin(nextIndex, pFeedItem->count - 1);

    // calculate rotation
    udDouble3 lastDisplayPosition = pFeedItem->displayPosition;
    udQuaternion<double> rotation = udQuaternion<double>::identity();
    pFeedItem->displayPosition = udLerp(pFeedItem->displayPositions[indexA], pFeedItem->displayPositions[indexB], lerpT);

    // visibility test
    udDouble3 bounds = udDouble3::create(10, 10, 10);
    if (vcQuadTree_FrustumTest(pFeedItem->displayPosition, bounds) == -1)
      continue;

    udFloat4 modelColour = udFloat4::one();
    uint64_t id = (uint64_t)(i + j + k + 1);

    if (distanceSq <= 2500 * 2500) // only draw the model if its close
    {
      if (udMagSq3(lastDisplayPosition - pFeedItem->displayPosition) > 0.01) // at least small amount movement
        rotation = udDouble4x4::lookAt(lastDisplayPosition, pFeedItem->displayPosition).extractQuaternion() * rotation;

      // HANDLE VEHICLE MODELS HERE
      vcPolygonModel *pModel;
      udDouble4x4 worldTransform;
      if (udStrEqual(pFeedItem->type, "Car"))
      {
        udDoubleQuat orientModel = udDoubleQuat::create(UD_PIf, 0, 0) * udDoubleQuat::create(0.0, UD_PIf * 0.5, 0.0); // orient the model correctly
        worldTransform = udDouble4x4::scaleUniform(0.025f, pFeedItem->displayPosition) * udDouble4x4::rotationQuat(rotation * orientModel);
        pModel = pCarModel;
      }
      else if (udStrEqual(pFeedItem->type, "Bus"))
      {
        udDoubleQuat orientModel = udDoubleQuat::create(UD_PIf, 0, 0) * udDoubleQuat::create(0.0, UD_PIf * 0.5, 0.0); // orient the model correctly
        worldTransform = udDouble4x4::scaleUniform(0.065f, pFeedItem->displayPosition) * udDouble4x4::rotationQuat(rotation * orientModel);
        pModel = pBusModel;
      }
      else if (udStrEqual(pFeedItem->type, "Taxi"))
      {
        udDoubleQuat orientModel = udDoubleQuat::create(UD_PIf * -0.5f, 0, 0); // orient the model correctly
        worldTransform = udDouble4x4::scaleUniform(0.04f, pFeedItem->displayPosition) * udDouble4x4::rotationQuat(rotation * orientModel);
        pModel = pTaxiModel;
      }
      else if (udStrEqual(pFeedItem->type, "Private Hire"))
      {
        worldTransform = udDouble4x4::scaleUniform(0.025f, pFeedItem->displayPosition) * udDouble4x4::rotationQuat(rotation);
        pModel = pPrivateHireModel;
      }
      else if (udStrEqual(pFeedItem->type, "HGV"))
      {
        worldTransform = udDouble4x4::scaleUniform(0.025f, pFeedItem->displayPosition) * udDouble4x4::rotationQuat(rotation);
        pModel = pHGVModel;
      }
      else if (udStrEqual(pFeedItem->type, "Motorcycle"))
      {
        worldTransform = udDouble4x4::scaleUniform(0.025f, pFeedItem->displayPosition) * udDouble4x4::rotationQuat(rotation);
        pModel = pMotorcycleModel;
      }
      else
      {
        worldTransform = udDouble4x4::scaleUniform(0.025f, pFeedItem->displayPosition) * udDouble4x4::rotationQuat(rotation);
        pModel = gInternalModels[vcInternalModelType_Sphere];
      }

      pRenderData->polyModels.PushBack({ vcRenderPolyInstance::RenderType_Polygon, vcRenderPolyInstance::RenderFlags_None, pModel, worldTransform, nullptr, modelColour, vcGLSCM_Back, true, this, id });
    }

    //if (m_selectedItem == i)
    if (pFeedItem->count > 1)
    {
      if (!pFeedItem->lineCalculated)
      {
        pFeedItem->lineCalculated = true;
        vcLineRenderer_UpdatePoints(pFeedItem->pLine, pFeedItem->displayPositions, pFeedItem->count, modelColour, 2.0f, false);
      }
      pRenderData->lines.PushBack(pFeedItem->pLine);
    }

    if (selected)
    {
      double totalDist = 0.0;
      for (int p = 1; p < pFeedItem->count; ++p)
      {
        totalDist += udMag3(pFeedItem->displayPositions[p] - pFeedItem->displayPositions[p - 1]);
      }

      float duration = 20.0f;  // we know its over 20s
      float kph = ((float)totalDist / duration) * 60 * 60 / 1000;

      udFree(pFeedItem->label.pText);

      udSprintf(&pFeedItem->label.pText, "Type: %s\nSpeed: %0.2f KM/H\n%s", pFeedItem->type, kph, pFeedItem->uuid);


      pFeedItem->label.worldPosition = pFeedItem->displayPosition;
      pRenderData->labels.PushBack(&pFeedItem->label);
    }

    ++m_visibleItems;
  }


  udReleaseMutex(m_pMutex);
}

void vcLiveFeed::ApplyDelta(vcState * /*pProgramState*/, const udDouble4x4 & /*delta*/)
{
  // Do nothing
}

void vcLiveFeed::HandleSceneExplorerUI(vcState *pProgramState, size_t * /*pItemID*/)
{
  if (pProgramState->settings.presentation.showDiagnosticInfo)
  {
    const char *strings[] = { udTempStr("%zu", m_feedItems.length), udTempStr("%zu", m_visibleItems), udTempStr("%.2f", (m_lastFeedSync + m_updateFrequency) - udGetEpochSecsUTCf()) };
    const char *pBuffer = vcStringFormat(vcString::Get("liveFeedDiagInfo"), strings, udLengthOf(strings));
    ImGui::Text("%s", pBuffer);
    udFree(pBuffer);
  }

  // Update Frequency
  {
    const double updateFrequencyMinValue = 5.0;
    const double updateFrequencyMaxValue = 300.0;

    if (ImGui::SliderScalar(vcString::Get("liveFeedUpdateFrequency"), ImGuiDataType_Double, &m_updateFrequency, &updateFrequencyMinValue, &updateFrequencyMaxValue, "%.0f s"))
    {
      m_updateFrequency = udClamp(m_updateFrequency, updateFrequencyMinValue, updateFrequencyMaxValue);
      udProjectNode_SetMetadataDouble(m_pNode, "updateFrequency", m_updateFrequency);
    }
  }

  // Decay Frequency
  {
    const double decayFrequencyMinValue = 30.0;
    const double decayFrequencyMaxValue = 604800.0; // 1 week

    if (ImGui::SliderScalar(vcString::Get("liveFeedMaxDisplayTime"), ImGuiDataType_Double, &m_decayFrequency, &decayFrequencyMinValue, &decayFrequencyMaxValue, "%.0f s", ImGuiSliderFlags_Logarithmic))
    {
      m_decayFrequency = udClamp(m_decayFrequency, decayFrequencyMinValue, decayFrequencyMaxValue);
      udProjectNode_SetMetadataDouble(m_pNode, "maxDisplayTime", m_decayFrequency);

      double recently = udGetEpochSecsUTCf() - m_decayFrequency;

      udLockMutex(m_pMutex);

      for (size_t i = 0; i < m_feedItems.length; ++i)
      {
        vcLiveFeedItem *pFeedItem = m_feedItems[i];
        pFeedItem->visible = (pFeedItem->lastUpdated > recently);
      }

      udReleaseMutex(m_pMutex);
    }

    // Falloff Distance
    {
      const double displayDistanceMinValue = 1.0;
      const double displayDistanceMaxValue = 100000.0;

      if (ImGui::SliderScalar(vcString::Get("liveFeedDisplayDistance"), ImGuiDataType_Double, &m_maxDisplayDistance, &displayDistanceMinValue, &displayDistanceMaxValue, "%.0f", ImGuiSliderFlags_Logarithmic))
      {
        m_maxDisplayDistance = udClamp(m_maxDisplayDistance, displayDistanceMinValue, displayDistanceMaxValue);
        udProjectNode_SetMetadataDouble(m_pNode, "maxDisplayDistance", m_maxDisplayDistance);
      }
    }

    // LOD Distances
    {
      const double minLODModifier = 0.01;
      const double maxLODModifier = 5.0;

      if (ImGui::SliderScalar(vcString::Get("liveFeedLODModifier"), ImGuiDataType_Double, &m_labelLODModifier, &minLODModifier, &maxLODModifier, "%.2f", ImGuiSliderFlags_Logarithmic))
      {
        m_labelLODModifier = udClamp(m_labelLODModifier, minLODModifier, maxLODModifier);
        udProjectNode_SetMetadataDouble(m_pNode, "lodModifier", m_labelLODModifier);
      }
    }

    // Tween
    if (ImGui::Checkbox(vcString::Get("liveFeedTween"), &m_tweenPositionAndOrientation))
      udProjectNode_SetMetadataBool(m_pNode, "tweenEnabled", m_tweenPositionAndOrientation);

    // Snap to map
    if (ImGui::Checkbox(vcString::Get("liveFeedSnapMap"), &m_snapToMap))
      udProjectNode_SetMetadataBool(m_pNode, "snapToMap", m_snapToMap);

    char groupStr[udUUID::udUUID_Length+1];
    udStrcpy(groupStr, udUUID_GetAsString(&m_groupID));
    if (vcIGSW_InputText(vcString::Get("liveFeedGroupID"), groupStr, udLengthOf(groupStr)))
    {
      if (udUUID_IsValid(groupStr))
      {
        udUUID_SetFromString(&m_groupID, groupStr);
        udProjectNode_SetMetadataString(m_pNode, "groupid", groupStr);
      }
    }
  }
}

void vcLiveFeed::Cleanup(vcState * /*pProgramState*/)
{
  udLockMutex(m_pMutex);

  airQualityStations.Deinit();
  cctv.Deinit();

  for (size_t i = 0; i < m_feedItems.length; ++i)
  {
    vcLiveFeedItem *pFeedItem = m_feedItems[i];

    //vcLiveFeedItem_ClearLODs(pFeedItem);
    udFree(pFeedItem);
  }

  for (size_t i = 0; i < m_polygonModels.length; ++i)
  {
    while (m_polygonModels[i].loadStatus == vcLiveFeedPolyCache::LS_Downloading)
    {
      udYield(); // busy wait
    }

    udFree(m_polygonModels[i].pModelURL);
    vcPolygonModel_Destroy(&m_polygonModels[i].pModel);
    udFree(m_polygonModels[i].pModelData);
  }

  udReleaseMutex(m_pMutex);

  udDestroyMutex(&m_pMutex);

  m_feedItems.Deinit();
  m_polygonModels.Deinit();
}

void vcLiveFeed::ChangeProjection(const udGeoZone &newZone)
{
  //TODO: Handle updating everything to render in the new zone
  udUnused(newZone);
}

udDouble3 vcLiveFeed::GetLocalSpacePivot()
{
  return udDouble3::zero();
}

void vcLiveFeed::SelectSubitem(uint64_t internalId)
{
  this->m_selectedItem = internalId;
}

bool vcLiveFeed::IsSubitemSelected(uint64_t internalId)
{
  if (internalId == 0 || this->m_selectedItem == 0)
    return m_selected;

  return (m_selected && this->m_selectedItem == internalId);
}
