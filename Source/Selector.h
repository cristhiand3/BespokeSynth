//
//  Selector.h
//  Bespoke
//
//  Created by Ryan Challinor on 2/7/16.
//
//

#ifndef __Bespoke__Selector__
#define __Bespoke__Selector__

#include "IDrawableModule.h"
#include "RadioButton.h"
#include "INoteReceiver.h"

class PatchCableSource;

class Selector : public IDrawableModule, public IRadioButtonListener, public INoteReceiver
{
public:
   Selector();
   ~Selector();
   static IDrawableModule* Create() { return new Selector(); }
   
   string GetTitleLabel() override { return "selector"; }
   void CreateUIControls() override;
   
   void RadioButtonUpdated(RadioButton* radio, int oldVal) override;
   
   void LoadLayout(const ofxJSONElement& moduleInfo) override;
   void SaveLayout(ofxJSONElement& moduleInfo) override;
   void SetUpFromSaveData() override;

   //INoteReceiver
   void PlayNote(double time, int pitch, int velocity, int voiceIdx = -1, ModulationParameters modulation = ModulationParameters()) override;
   void SendCC(int control, int value, int voiceIdx = -1) override {}
   
   //IPatchable
   void PostRepatch(PatchCableSource* cableSource, bool fromUserClick) override;
   
private:
   //IDrawableModule
   void DrawModule() override;
   bool Enabled() const override { return mEnabled; }
   void GetModuleDimensions(float& width, float& height) override;
   
   void SyncList();
   void SetIndex(int index);
   
   RadioButton* mSelector;
   int mCurrentValue;
   
   vector<PatchCableSource*> mControlCables;
};


#endif /* defined(__Bespoke__Selector__) */
