//
//  GridController.cpp
//  Bespoke
//
//  Created by Ryan Challinor on 2/9/15.
//
//

#include "GridController.h"
#include "ModularSynth.h"
#include "FillSaveDropdown.h"
#include "PatchCableSource.h"

GridController::GridController()
: mMessageType(kMidiMessage_Note)
, mController(nullptr)
, mControllerPage(0)
, mRows(0)
, mCols(0)
, mGrid(nullptr)
, mClicked(false)
, mClickedCell(0,0)
{
   bzero(mControls, sizeof(int)*MAX_GRIDCONTROLLER_ROWS*MAX_GRIDCONTROLLER_COLS);
   bzero(mInput, sizeof(float)*MAX_GRIDCONTROLLER_ROWS*MAX_GRIDCONTROLLER_COLS);
   bzero(mLights, sizeof(int)*MAX_GRIDCONTROLLER_ROWS*MAX_GRIDCONTROLLER_COLS);
}

void GridController::CreateUIControls()
{
   IDrawableModule::CreateUIControls();
   
   mGrid = new UIGrid(0,0,100,100,1,1);
}

string GridController::GetTitleLabel()
{
   return string("grid:")+Name();
}

void GridController::DrawModule()
{
   if (Minimized() || IsVisible() == false)
      return;
   
   DrawGrid();
}

void GridController::DrawGrid()
{
   int w,h;
   GetDimensions(w,h);
   mGrid->SetDimensions(w, h);
   
   if (IsMultisliderGrid())
   {
      mGrid->Draw();
   }
   else //color mode
   {
      for (int col=0; col < mCols; ++col)
      {
         for (int row=0; row < mRows; ++row)
         {
            ofVec2f pos = mGrid->GetCellPosition(col, row);
            ofPushStyle();
            ofFill();
            
            switch (mLights[col][row])
            {
               case kGridColorOff:
                  ofSetColor(100,100,100,255*gModuleDrawAlpha);
                  break;
               case kGridColor1Dim:
                  ofSetColor(160,100,100,255*gModuleDrawAlpha);
                  break;
               case kGridColor1Bright:
                  ofSetColor(255,100,100,255*gModuleDrawAlpha);
                  break;
               case kGridColor2Dim:
                  ofSetColor(100,160,100,255*gModuleDrawAlpha);
                  break;
               case kGridColor2Bright:
                  ofSetColor(100,255,100,255*gModuleDrawAlpha);
                  break;
               case kGridColor3Dim:
                  ofSetColor(160,160,100,255*gModuleDrawAlpha);
                  break;
               case kGridColor3Bright:
                  ofSetColor(255,200,100,255*gModuleDrawAlpha);
                  break;
            }
            ofRect(pos.x+1,pos.y+1,w/mCols-2,h/mRows-2);
            
            if (mInput[col][row] > 0)
            {
               ofNoFill();
               ofSetColor(ofColor::yellow);
               ofRect(pos.x,pos.y,w/mCols,h/mRows);
            }
            
            ofPopStyle();
         }
      }
   }
}

void GridController::OnControllerPageSelected()
{
   for (int i=0; i<mCols; ++i)
   {
      for (int j=0; j<mRows; ++j)
      {
         SetLightDirect(i, j, mLights[i][j], K(force));
      }
   }
}

void GridController::OnInput(int control, float velocity)
{
   int x;
   int y;
   bool found = false;
   for (x=0;x<mCols;++x)
   {
      for (y=0;y<mRows;++y)
      {
         if (mControls[x][y] == control)
         {
            found = true;
            break;
         }
      }
      if (found)
         break;
   }
   
   if (found)
   {
      mInput[x][y] = velocity;
      
      for (auto cable : GetPatchCableSource()->GetPatchCables())
      {
         auto* listener = dynamic_cast<IGridControllerListener*>(cable->GetTarget());
         if (listener)
            listener->OnGridButton(x, y, velocity, this);
      }
      
      mHistory.AddEvent(gTime, HasInput());
   }
}

bool GridController::HasInput() const
{
   for (int i=0; i<mCols; ++i)
   {
      for (int j=0; j<mRows; ++j)
      {
         if (mInput[i][j] > 0)
         {
            return true;
         }
      }
   }
   
   return false;
}

void GridController::OnClicked(int x, int y, bool right)
{
   IDrawableModule::OnClicked(x, y, right);
   
   if (y>0)
   {
      GridCell cell = mGrid->GetGridCellAt(x, y);
      OnInput(mControls[cell.mCol][cell.mRow], 1);
      mClicked = true;
      mClickedCell = cell;
   }
}

void GridController::MouseReleased()
{
   IDrawableModule::MouseReleased();
   
   if (mClicked)
   {
      OnInput(mControls[mClickedCell.mCol][mClickedCell.mRow], 0);
   }
   mClicked = false;
}

void GridController::SetLight(int x, int y, GridColor color, bool force)
{
   if (x >= mCols || y >= mRows)
      return;
   
   int colorIdx = (int)color;
   int rawColor = 0;
   if (mColors.size())
   {
      if (colorIdx >= mColors.size())  //we don't have this many colors
      {
         while (colorIdx >= mColors.size())
            colorIdx -= 2; //move back by two to retain bright/dimness
         if (colorIdx <= 0)
            colorIdx = 1;  //never set a non-off light to "off"
      }
      rawColor = mColors[colorIdx];
   }
   
   SetLightDirect(x, y, rawColor, force);
}

void GridController::SetLightDirect(int x, int y, int color, bool force)
{
   if (mLights[x][y] != color || force)
   {
      if (mController)
      {
         if (mMessageType == kMidiMessage_Note)
            mController->SendNote(mControllerPage, mControls[x][y], color);
         else if (mMessageType == kMidiMessage_Control)
            mController->SendCC(mControllerPage, mControls[x][y], color);
      }
      mLights[x][y] = color;
      if (IsMultisliderGrid())
         mGrid->SetVal(x,y,color/127.0f);
      else
         mGrid->SetVal(x,y,color==kGridColorOff ? 0 : 1);
   }
}

void GridController::ResetLights()
{
   for (int i=0; i<mCols; ++i)
   {
      for (int j=0; j<mRows; ++j)
      {
         SetLight(i,j,kGridColorOff);
      }
   }
}

void GridController::SetTarget(IClickable* target)
{
   if (target)
      GetPatchCableSource()->SetTarget(target);
   else
      GetPatchCableSource()->ClearPatchCables();
}

void GridController::PostRepatch(PatchCableSource* cable)
{
   auto* listener = dynamic_cast<IGridControllerListener*>(cable->GetTarget());
   if (listener)
      listener->ConnectGridController(this);
}

void GridController::GetModuleDimensions(int& w, int& h)
{
   w = mCols * 20;
   h = mRows * 20;
}

void GridController::SetUp(GridLayout* layout, MidiController* controller)
{
   mRows = layout->mRows;
   mCols = layout->mCols;
   for (unsigned int row=0; row<mRows; ++row)
   {
      for (unsigned int col=0; col<mCols; ++col)
      {  
         int index = col + row * mCols;
         mControls[col][row] = layout->mControls[index];
      }
   }
   
   mColors.clear();
   unsigned int numColors = 2;
   for (unsigned int i=0; i<numColors; ++i)
   {
      mColors.push_back(i * 127);
   }
   
   mMessageType = layout->mType;
   
   mGrid->SetGrid(mCols,mRows);
   
   mController = controller;
}

void GridController::LoadLayout(const ofxJSONElement& moduleInfo)
{
   mModuleSaveData.LoadString("target", moduleInfo);
   
   SetUpFromSaveData();
}

void GridController::SetUpFromSaveData()
{
   SetUpPatchCables(mModuleSaveData.GetString("target"));
}
