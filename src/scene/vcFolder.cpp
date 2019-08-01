#include "vcFolder.h"

#include "vcState.h"
#include "vcStrings.h"
#include "vcRender.h" // Included just for "ClearTiles"
#include "vcFenceRenderer.h"
#include "vcModals.h"

#include "udStringUtil.h"

#include "imgui.h"
#include "imgui_ex/vcImGuiSimpleWidgets.h"

// Other includes for creating nodes
#include "vcModel.h"
#include "vcPOI.h"
#include "vcMedia.h"
#include "vcLiveFeed.h"
#include "vcUnsupportedNode.h"
#include "vcI3S.h"
#include "vcWaterNode.h"
#include "vcViewpoint.h"

vcFolder::vcFolder(vdkProject *pProject, vdkProjectNode *pNode, vcState *pProgramState) :
  vcSceneItem(pProject, pNode, pProgramState)
{
  m_loadStatus = vcSLS_Loaded;
}

void vcFolder::AddToScene(vcState *pProgramState, vcRenderData *pRenderData)
{
  if (!m_visible)
    return;

  vdkProjectNode *pNode = m_pNode->pFirstChild;
  while (pNode != nullptr)
  {
    if (pNode->pUserData != nullptr)
    {
      vcSceneItem *pSceneItem = (vcSceneItem*)pNode->pUserData;

      pSceneItem->AddToScene(pProgramState, pRenderData);

      if (pSceneItem->m_lastUpdateTime < pSceneItem->m_pNode->lastUpdate)
        pSceneItem->UpdateNode(pProgramState);
    }
    else
    {
      // We need to create one
      if (pNode->itemtype == vdkPNT_Folder)
        pNode->pUserData = new vcFolder(pProgramState->activeProject.pProject, pNode, pProgramState);
      else if (pNode->itemtype == vdkPNT_PointOfInterest)
        pNode->pUserData = new vcPOI(pProgramState->activeProject.pProject, pNode, pProgramState);
      else if (pNode->itemtype == vdkPNT_PointCloud)
        pNode->pUserData = new vcModel(pProgramState->activeProject.pProject, pNode, pProgramState);
      else if (pNode->itemtype == vdkPNT_LiveFeed)
        pNode->pUserData = new vcLiveFeed(pProgramState->activeProject.pProject, pNode, pProgramState);
      else if (pNode->itemtype == vdkPNT_Media)
        pNode->pUserData = new vcMedia(pProgramState->activeProject.pProject, pNode, pProgramState);
      else if (pNode->itemtype == vdkPNT_Viewpoint)
        pNode->pUserData = new vcViewpoint(pProgramState->activeProject.pProject, pNode, pProgramState);
      else if (udStrEqual(pNode->itemtypeStr, "I3S"))
        pNode->pUserData = new vcI3S(pProgramState->activeProject.pProject, pNode, pProgramState);
      else if (udStrEqual(pNode->itemtypeStr, "Water"))
        pNode->pUserData = new vcWater(pProgramState->activeProject.pProject, pNode, pProgramState);
      else
        pNode->pUserData = new vcUnsupportedNode(pProgramState->activeProject.pProject, pNode, pProgramState); // Catch all
    }

    pNode = pNode->pNextSibling;
  }
}

void vcFolder::OnNodeUpdate(vcState * /*pProgramState*/)
{
  // Does Nothing
}

void vcFolder::ChangeProjection(const udGeoZone &newZone)
{
  vdkProjectNode *pNode = m_pNode->pFirstChild;
  while (pNode != nullptr)
  {
    if (pNode->pUserData)
      ((vcSceneItem*)pNode->pUserData)->ChangeProjection(newZone);
    pNode = pNode->pNextSibling;
  }
}

void vcFolder::ApplyDelta(vcState *pProgramState, const udDouble4x4 &delta)
{
  vdkProjectNode *pNode = m_pNode->pFirstChild;
  while (pNode != nullptr)
  {
    if (pNode->pUserData)
      ((vcSceneItem*)pNode->pUserData)->ApplyDelta(pProgramState, delta);
    pNode = pNode->pNextSibling;
  }
}

void vcFolder_AddInsertSeparator()
{
  ImGui::PushStyleColor(ImGuiCol_Separator, ImGui::GetStyle().Colors[ImGuiCol_HeaderHovered]); //RBGA
  ImVec2 pos = ImGui::GetCursorPos();
  ImVec2 winPos = ImGui::GetWindowPos();
  ImGui::SetWindowPos({ winPos.x + pos.x, winPos.y }, ImGuiCond_Always);
  ImGui::Separator();
  ImGui::SetCursorPos(pos);
  ImGui::SetWindowPos(winPos, ImGuiCond_Always);
  ImGui::PopStyleColor();
}

void vcFolder::HandleImGui(vcState *pProgramState, size_t *pItemID)
{
  vdkProjectNode *pNode = m_pNode->pFirstChild;
  size_t i = 0;
  while (pNode != nullptr)
  {
    ++(*pItemID);

    if (pNode->pUserData != nullptr)
    {
      if (pProgramState->pGotGeo != nullptr)
        ((vcSceneItem*)(pNode->pUserData))->ChangeProjection(*pProgramState->pGotGeo);

      if (pProgramState->getGeo && pNode->itemtype == vdkPNT_PointCloud && ((vcModel*)(pNode->pUserData))->m_pPreferredProjection != nullptr)
      {
        vcModel *pModel = (vcModel*)pNode->pUserData;
        if (pProgramState->pGotGeo == pModel->m_pPreferredProjection)
        {
          pProgramState->pGotGeo = nullptr;
          pProgramState->getGeo = false;
        }
        else if (pProgramState->pGotGeo == nullptr)
        {
          pProgramState->pGotGeo = pModel->m_pPreferredProjection;
          vcProject_UseProjectionFromItem(pProgramState, pModel);
        }
      }

      vcSceneItem *pSceneItem = (vcSceneItem*)pNode->pUserData;

      // This block is also after the loop
      if (pProgramState->sceneExplorer.insertItem.pParent == m_pNode && pProgramState->sceneExplorer.insertItem.pItem == pNode)
        vcFolder_AddInsertSeparator();

      // Can only edit the name while the item is still selected
      pSceneItem->m_editName = (pSceneItem->m_editName && pSceneItem->m_selected);

      // Visibility
      ImGui::Checkbox(udTempStr("###SXIVisible%zu", *pItemID), &pSceneItem->m_visible);
      ImGui::SameLine();

      vcIGSW_ShowLoadStatusIndicator((vcSceneLoadStatus)pSceneItem->m_loadStatus);

      // The actual model
      ImGui::SetNextTreeNodeOpen(pSceneItem->m_expanded, ImGuiCond_Always);
      ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
      if (pSceneItem->m_selected)
        flags |= ImGuiTreeNodeFlags_Selected;

      if (pSceneItem->m_editName)
      {
        pSceneItem->m_expanded = ImGui::TreeNodeEx(udTempStr("###SXIName%zu", *pItemID), flags);
        ImGui::SameLine();

		// TODO: (EVC-636) this `256` is wrong, that is not fixing the bug!
        size_t length = udStrlen(pNode->pName);
        char *pData = udAllocType(char, length + 256, udAF_None);
        memcpy(pData, pNode->pName, length + 1); // factor in \0

        length += 256;

        vcIGSW_InputTextWithResize(udTempStr("###FolderName%zu", *pItemID), &pData, &length);
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
          if (vdkProjectNode_SetName(pProgramState->activeProject.pProject, pNode, pData) != vE_Success)
            vcModals_OpenModal(pProgramState, vcMT_ProjectChangeFailed);
        }

        udFree(pData);

        if (ImGui::IsItemDeactivated() || !(pProgramState->sceneExplorer.selectedItems.back().pParent == m_pNode && pProgramState->sceneExplorer.selectedItems.back().pItem == pNode))
          pSceneItem->m_editName = false;
        else
          ImGui::SetKeyboardFocusHere(-1); // Set focus to previous widget
      }
      else
      {
        pSceneItem->m_expanded = ImGui::TreeNodeEx(udTempStr("%s###SXIName%zu", pNode->pName, *pItemID), flags);
        if (pSceneItem->m_selected && pProgramState->sceneExplorer.selectedItems.back().pParent == m_pNode && pProgramState->sceneExplorer.selectedItems.back().pItem == pNode && ImGui::GetIO().KeysDown[SDL_SCANCODE_F2])
          pSceneItem->m_editName = true;
      }

      bool sceneExplorerItemClicked = ((ImGui::IsMouseReleased(0) && ImGui::IsItemHovered() && !ImGui::IsItemActive()) || (!pSceneItem->m_selected && ImGui::IsItemActive()));
      bool sceneItemClicked = (pProgramState->sceneExplorer.selectUUIDWhenPossible[0] != '\0' && udStrEqual(pProgramState->sceneExplorer.selectUUIDWhenPossible, pNode->UUID));
      if (sceneExplorerItemClicked || sceneItemClicked)
      {
        if (!ImGui::GetIO().KeyCtrl)
          vcProject_ClearSelection(pProgramState);

        if (pSceneItem->m_selected)
        {
          vcProject_UnselectItem(pProgramState, m_pNode, pNode);
          pProgramState->sceneExplorer.clickedItem = { nullptr, nullptr };
        }
        else
        {
          vcProject_SelectItem(pProgramState, m_pNode, pNode);
          pProgramState->sceneExplorer.clickedItem = { m_pNode, pNode };
        }

        if (sceneItemClicked)
          memset(pProgramState->sceneExplorer.selectUUIDWhenPossible, 0, sizeof(pProgramState->sceneExplorer.selectUUIDWhenPossible));
      }

      if (pSceneItem->m_loadStatus == vcSLS_Loaded && pProgramState->sceneExplorer.movetoUUIDWhenPossible[0] != '\0' && udStrEqual(pProgramState->sceneExplorer.movetoUUIDWhenPossible, pNode->UUID))
      {
        vcProject_UseProjectionFromItem(pProgramState, pSceneItem);
        memset(pProgramState->sceneExplorer.movetoUUIDWhenPossible, 0, sizeof(pProgramState->sceneExplorer.movetoUUIDWhenPossible));
      }

      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseDragging())
      {
        ImVec2 minPos = ImGui::GetItemRectMin();
        ImVec2 maxPos = ImGui::GetItemRectMax();
        ImVec2 mousePos = ImGui::GetMousePos();

        if (pNode->itemtype == vdkPNT_Folder && mousePos.y > minPos.y && mousePos.y < maxPos.y)
          pProgramState->sceneExplorer.insertItem = { pNode, pNode };
        else
          pProgramState->sceneExplorer.insertItem = { m_pNode, pNode }; // This will become pNode->pNextSibling after drop
      }

      if (ImGui::BeginPopupContextItem(udTempStr("ModelContextMenu_%zu", *pItemID)))
      {
        if (!pSceneItem->m_selected)
        {
          if (!ImGui::GetIO().KeyCtrl)
            vcProject_ClearSelection(pProgramState);

          vcProject_SelectItem(pProgramState, m_pNode, pNode);
          pProgramState->sceneExplorer.clickedItem = { m_pNode, pNode };
        }

        if (ImGui::Selectable(vcString::Get("sceneExplorerEditName")))
          pSceneItem->m_editName = true;

        if (pSceneItem->m_pPreferredProjection != nullptr && pSceneItem->m_pPreferredProjection->srid != 0 && ImGui::Selectable(vcString::Get("sceneExplorerUseProjection")))
        {
          if (vcGIS_ChangeSpace(&pProgramState->gis, *pSceneItem->m_pPreferredProjection, &pProgramState->pCamera->position))
          {
            pProgramState->activeProject.pFolder->ChangeProjection(*pSceneItem->m_pPreferredProjection);
            // refresh map tiles when geozone changes
            vcRender_ClearTiles(pProgramState->pRenderContext);
          }
        }

        if (pNode->itemtype != vdkPNT_Folder && pSceneItem->GetWorldSpacePivot() != udDouble3::zero() && ImGui::Selectable(vcString::Get("sceneExplorerMoveTo")))
        {
          // Trigger a camera movement path
          pProgramState->cameraInput.inputState = vcCIS_MovingToPoint;
          pProgramState->cameraInput.startPosition = pProgramState->pCamera->position;
          pProgramState->cameraInput.startAngle = udDoubleQuat::create(pProgramState->pCamera->eulerRotation);
          pProgramState->cameraInput.progress = 0.0;

          pProgramState->isUsingAnchorPoint = true;
          pProgramState->worldAnchorPoint = pSceneItem->GetWorldSpacePivot();
        }

        // This is terrible but semi-required until we have undo
        if (pNode->itemtype == vdkPNT_PointCloud && ImGui::Selectable(vcString::Get("sceneExplorerResetPosition"), false))
        {
          if (pSceneItem->m_pPreferredProjection)
            ((vcModel*)pSceneItem)->ChangeProjection(*pSceneItem->m_pPreferredProjection);
          ((vcModel*)pSceneItem)->m_sceneMatrix = ((vcModel*)pSceneItem)->m_defaultMatrix;
          ((vcModel*)pSceneItem)->ChangeProjection(pProgramState->gis.zone);
        }

        if (ImGui::Selectable(vcString::Get("sceneExplorerRemoveItem")))
        {
          vcProject_RemoveItem(pProgramState, m_pNode, pNode);
          pProgramState->sceneExplorer.clickedItem = { nullptr, nullptr };

          ImGui::EndPopup();
          return;
        }

        ImGui::EndPopup();
      }

      if (pNode->itemtype != vdkPNT_Folder && pSceneItem->GetWorldSpacePivot() != udDouble3::zero() && ImGui::IsMouseDoubleClicked(0) && ImGui::IsItemHovered())
      {
        if (pSceneItem->m_pPreferredProjection != nullptr)
          vcProject_UseProjectionFromItem(pProgramState, pSceneItem);
        else
          pSceneItem->SetCameraPosition(pProgramState);
      }

      if (vcIGSW_IsItemHovered())
        ImGui::SetTooltip("%s", pNode->pName);

      if (!pSceneItem->m_expanded && pProgramState->sceneExplorer.insertItem.pParent == pNode && pProgramState->sceneExplorer.insertItem.pItem == nullptr)
      {
        // Double indent to match open folder insertion
        ImGui::Indent();
        ImGui::Indent();
        vcFolder_AddInsertSeparator();
        ImGui::Unindent();
        ImGui::Unindent();
      }

      // Show additional settings from ImGui
      if (pSceneItem->m_expanded)
      {
        ImGui::Indent();
        ImGui::PushID(udTempStr("SXIExpanded%zu", *pItemID));

        pSceneItem->HandleImGui(pProgramState, pItemID);

        ImGui::PopID();
        ImGui::Unindent();
        ImGui::TreePop();
      }
    }

    pNode = pNode->pNextSibling;
    ++i;
  }

  // This block is also in the loop above
  if (pProgramState->sceneExplorer.insertItem.pParent == m_pNode && pNode == pProgramState->sceneExplorer.insertItem.pItem)
    vcFolder_AddInsertSeparator();
}

void vcFolder::Cleanup(vcState *pProgramState)
{
  vdkProjectNode *pNode = m_pNode->pFirstChild;

  while (pNode != nullptr)
  {
    vcProject_RemoveItem(pProgramState, m_pNode, pNode);
    pNode = pNode->pNextSibling;
  }
}
