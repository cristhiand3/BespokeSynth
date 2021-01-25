/*
  ==============================================================================

    SamplePlayer.cpp
    Created: 19 Oct 2017 10:10:15pm
    Author:  Ryan Challinor

  ==============================================================================
*/

#include "SamplePlayer.h"
#include "IAudioReceiver.h"
#include "Sample.h"
#include "SampleBank.h"
#include "SynthGlobals.h"
#include "ModularSynth.h"
#include "Profiler.h"
#include "FillSaveDropdown.h"
#include "PatchCableSource.h"
#include "Scale.h"
#include "UIControlMacros.h"

SamplePlayer::SamplePlayer()
: mVolume(1)
, mVolumeSlider(nullptr)
, mSpeed(1)
, mSpeedSlider(nullptr)
, mSample(nullptr)
, mSampleIndex(-1)
, mSampleList(nullptr)
, mPlay(false)
, mLoop(false)
, mLoopCheckbox(nullptr)
, mBank(nullptr)
, mSampleBankCable(nullptr)
, mDrawBuffer(0)
, mPlayButton(nullptr)
, mPauseButton(nullptr)
, mStopButton(nullptr)
, mDownloadYoutubeButton(nullptr)
, mLoadFileButton(nullptr)
, mScrubbingSample(false)
, mOscWheelGrabbed(false)
, mOscWheelSpeed(0)
, mPlaySpeed(1)
, mWidth(400)
, mHeight(170)
, mNoteInputBuffer(this)
, mAdsr(10,1,1,10)
, mZoomLevel(1)
, mZoomOffset(0)
, mActiveCuePointIndex(0)
, mSetCuePoint(false)
, mSelectPlayedCuePoint(false)
, mRecentPlayedCuePoint(-1)
, mShowGrid(false)
, mRunningProcessType(RunningProcessType::None)
, mRunningProcess(nullptr)
, mOnRunningProcessComplete(nullptr)
, mIsLoadingSample(false)
, mLastOutputSample(1)
, mSwitchAndRampVal(1)
{
   mYoutubeSearch[0] = 0;
}

void SamplePlayer::CreateUIControls()
{
   IDrawableModule::CreateUIControls();
   UIBLOCK0();
   UIBLOCK_SHIFTX(20);
   FLOATSLIDER(mVolumeSlider, "volume",&mVolume,0,2); UIBLOCK_SHIFTRIGHT();
   FLOATSLIDER(mSpeedSlider,"speed",&mSpeed,-2,2); UIBLOCK_NEWLINE();
   DROPDOWN(mSampleList,"samples",&mSampleIndex, 100); UIBLOCK_SHIFTRIGHT();
   TEXTENTRY(mDownloadYoutubeSearch,"yt:",30,mYoutubeSearch); UIBLOCK_NEWLINE();
   mDownloadYoutubeSearch->DrawLabel(true);
   mDownloadYoutubeSearch->SetRequireEnter(true);
   BUTTON(mPlayButton,"play"); UIBLOCK_SHIFTRIGHT();
   BUTTON(mPauseButton,"pause"); UIBLOCK_SHIFTRIGHT();
   BUTTON(mStopButton,"stop"); UIBLOCK_SHIFTRIGHT();
   CHECKBOX(mLoopCheckbox,"loop",&mLoop); UIBLOCK_SHIFTRIGHT();
   UIBLOCK_SHIFTX(30);
   BUTTON(mLoadFileButton,"load"); UIBLOCK_SHIFTRIGHT();
   BUTTON(mDownloadYoutubeButton,"youtube");
   UIBLOCK_SHIFTX(120);
   UIBLOCK_NEWCOLUMN();
   FLOATSLIDER_DIGITS(mCuePointStartSlider, "cue start", &mSampleCuePoints[0].startSeconds, 0, 100, 3);
   FLOATSLIDER_DIGITS(mCuePointLengthSlider, "cue length", &mSampleCuePoints[0].lengthSeconds, 0, 100, 3);
   FLOATSLIDER(mCuePointSpeedSlider, "cue speed", &mSampleCuePoints[0].speed, 0, 2);
   UIBLOCK_NEWCOLUMN();
   UICONTROL_CUSTOM(mCuePointSelector, new RadioButton(UICONTROL_BASICS("cuepoint"), &mActiveCuePointIndex, kRadioHorizontal));
   CHECKBOX(mSetCuePointCheckbox, "click sets cue point", &mSetCuePoint);
   CHECKBOX(mSelectPlayedCuePointCheckbox, "select played", &mSelectPlayedCuePoint);
   UIBLOCK_NEWCOLUMN();
   UIBLOCK_NEWCOLUMN();
   UIBLOCK_SHIFTDOWN();
   BUTTON(mPlayCurrentCuePointButton, "play cue");
   CHECKBOX(mShowGridCheckbox, "show grid", &mShowGrid);
   ENDUIBLOCK0();

   UIBLOCK(0, 65);
   for (size_t i = 0; i < mSearchResultButtons.size(); ++i)
   {
      BUTTON(mSearchResultButtons[i], ("searchresult" + ofToString(i)).c_str());
      mSearchResultButtons[i]->SetShowing(false);
   }
   ENDUIBLOCK0();

   for (int i = 0; i < (int)mSampleCuePoints.size(); ++i)
      mCuePointSelector->AddLabel(ofToString(i).c_str(), i);
   
   mSampleBankCable = new PatchCableSource(this, kConnectionType_Special);
   mSampleBankCable->SetManualPosition(8, 8);
   mSampleBankCable->AddTypeFilter("samplebank");
   AddPatchCableSource(mSampleBankCable);
}

SamplePlayer::~SamplePlayer()
{
   if (mOwnsSample)
      delete mSample;
}

void SamplePlayer::Init()
{
   IDrawableModule::Init();
   
   if (OSCReceiver::connect(12345))
      OSCReceiver::addListener(this);
}

void SamplePlayer::Poll()
{
   IDrawableModule::Poll();
   
   juce::String clipboard = SystemClipboard::getTextFromClipboard();
   if (clipboard.contains("youtube"))
   {
      juce::String clipId = clipboard.substring(clipboard.indexOf("v=")+2, clipboard.length());
      mYoutubeId = clipId.toStdString();
      mDownloadYoutubeButton->SetShowing(true);
   }
   else
   {
      mYoutubeId = "";
      mDownloadYoutubeButton->SetShowing(false);
   }

   if (mRunningProcess != nullptr)
   {
      if (mModuleSaveData.GetBool("show_youtube_process_output"))
      {
         char buffer[512];
         auto num = mRunningProcess->readProcessOutput(buffer, sizeof(buffer));

         if (num > 0)
         {
            MemoryOutputStream result;
            result.write(buffer, (size_t)num);
            ofLog() << result.toString();
         }
      }

      if (mRunningProcessType == RunningProcessType::SearchYoutube)
      {
         string tempPath = ofToDataPath("youtube_temp");
         auto dir = juce::File(tempPath);
         if (dir.exists())
         {
            Array<juce::File> results;
            dir.findChildFiles(results, juce::File::findFiles, false, "*.info.json");
            if (results.size() != mYoutubeSearchResults.size())
            {
               for (auto& result : results)
               {
                  string file = result.getFileName().toStdString();
                  ofStringReplace(file, ".info.json", "");
                  vector<string> tokens = ofSplitString(file, "#");
                  if (tokens.size() >= 3)
                  {
                     string lengthStr = tokens[tokens.size() - 2];
                     string id = tokens[tokens.size() - 1];
                     bool found = false;
                     for (auto& existing : mYoutubeSearchResults)
                     {
                        if (existing.youtubeId == id)
                           found = true;
                     }
                     if (!found)
                     {
                        YoutubeSearchResult resultToAdd;
                        string name = "";
                        for (size_t i = 0; i < tokens.size() - 2; ++i)
                           name += tokens[i];
                        resultToAdd.name = name;
                        resultToAdd.lengthSeconds = ofToFloat(lengthStr);
                        resultToAdd.youtubeId = id;
                        mYoutubeSearchResults.push_back(resultToAdd);
                     }
                  }
               }
            }
         }
      }

      if (!mRunningProcess->isRunning())
      {
         ofLog() << mRunningProcess->readAllProcessOutput();
         delete mRunningProcess;
         mRunningProcess = nullptr;
         if (mOnRunningProcessComplete != nullptr)
            mOnRunningProcessComplete();
      }
   }

   if (mRecentPlayedCuePoint != -1)
   {
      int index = mRecentPlayedCuePoint;
      mRecentPlayedCuePoint = -1;

      if (index >= 0 && index < (int)mSampleCuePoints.size())
      {
         mActiveCuePointIndex = index;
         UpdateActiveCuePoint();
      }
   }
}

void SamplePlayer::Process(double time)
{
   PROFILER(SamplePlayer);
   
   if (!mEnabled || GetTarget() == nullptr || mSample == nullptr)
      return;
   
   mNoteInputBuffer.Process(time);
   
   ComputeSliders(0);
   SyncOutputBuffer(mSample->NumChannels());
   
   int bufferSize = GetTarget()->GetBuffer()->BufferSize();
   assert(bufferSize == gBufferSize);
   
   float volSq = mVolume * mVolume;
   
   const float kBlendSpeed = 1;
   if (mOscWheelGrabbed)
   {
      mPlaySpeed = ofLerp(mPlaySpeed, mOscWheelSpeed, kBlendSpeed);
      mPlaySpeed = ofClamp(mPlaySpeed, -5, 5);
   }
   else
   {
      mPlaySpeed = ofLerp(mPlaySpeed, mSpeed, kBlendSpeed);
   }
   mSample->SetRate(mPlaySpeed);
   
   gWorkChannelBuffer.SetNumActiveChannels(mSample->NumChannels());
   mLastOutputSample.SetNumActiveChannels(mSample->NumChannels());
   mSwitchAndRampVal.SetNumActiveChannels(mSample->NumChannels());

   if (mPlay && mSample->ConsumeData(time, &gWorkChannelBuffer, bufferSize, true))
   {
      for (int ch = 0; ch < gWorkChannelBuffer.NumActiveChannels(); ++ch)
      {
         for (int i = 0; i < bufferSize; ++i)
            gWorkChannelBuffer.GetChannel(ch)[i] *= volSq * mAdsr.Value(time + i * gInvSampleRateMs);
      }
   }
   else
   {
      gWorkChannelBuffer.Clear();
      mPlay = false;
      mAdsr.Stop(time);
   }

   for (int ch = 0; ch < gWorkChannelBuffer.NumActiveChannels(); ++ch)
   {
      for (int i = 0; i < bufferSize; ++i)
      {
         gWorkChannelBuffer.GetChannel(ch)[i] += mSwitchAndRampVal.GetChannel(ch)[0];
         mSwitchAndRampVal.GetChannel(ch)[0] *= .999f;
         if (mSwitchAndRampVal.GetChannel(ch)[0] < .0001f && mSwitchAndRampVal.GetChannel(ch)[0] > -.0001f)
            mSwitchAndRampVal.GetChannel(ch)[0] = 0;
      }

      Add(GetTarget()->GetBuffer()->GetChannel(ch), gWorkChannelBuffer.GetChannel(ch), bufferSize);
      GetVizBuffer()->WriteChunk(gWorkChannelBuffer.GetChannel(ch), bufferSize, ch);
      mLastOutputSample.GetChannel(ch)[0] = gWorkChannelBuffer.GetChannel(ch)[bufferSize-1];
   }
}

void SamplePlayer::PostRepatch(PatchCableSource* cableSource, bool fromUserClick)
{
   if (cableSource == mSampleBankCable)
   {
      mBank = dynamic_cast<SampleBank*>(mSampleBankCable->GetTarget());
      
      UpdateSampleList();
   }
}

void SamplePlayer::PlayNote(double time, int pitch, int velocity, int voiceIdx /*= -1*/, ModulationParameters modulation /*= ModulationParameters()*/)
{
   if (!mEnabled)
      return;

   if (mSelectPlayedCuePoint)
      mRecentPlayedCuePoint = pitch;

   if (!NoteInputBuffer::IsTimeWithinFrame(time))
   {
      mNoteInputBuffer.QueueNote(time, pitch, velocity, voiceIdx, modulation);
      return;
   }
   
   if (velocity > 0 && mSample != nullptr)
      PlayCuePoint(time, pitch, velocity);
}

void SamplePlayer::PlayCuePoint(double time, int index, int velocity)
{
   float startSeconds, lengthSeconds, speed;
   GetPlayInfoForPitch(index, startSeconds, lengthSeconds, speed);
   mSample->SetPlayPosition(((gTime - time) / 1000 + startSeconds) * gSampleRate * mSample->GetSampleRateRatio());
   mPlay = true;
   mSpeed = speed;
   mAdsr.Clear();
   mAdsr.Start(time, velocity / 127.0f);
   if (lengthSeconds > 0)
      mAdsr.Stop(time + lengthSeconds * 1000);
   SwitchAndRamp();
}

void SamplePlayer::SwitchAndRamp()
{
   for (int ch = 0; ch < mSwitchAndRampVal.NumActiveChannels(); ++ch)
      mSwitchAndRampVal.GetChannel(ch)[0] = mLastOutputSample.GetChannel(ch)[0];
}

void SamplePlayer::UpdateSampleList()
{
   if (mBank == nullptr)
      return;
   
   mSampleList->Clear();
   vector<SampleInfo> samples = mBank->GetSamples();
   for (int i=0; i<samples.size(); ++i)
   {
      mSampleList->AddLabel(samples[i].mSample->Name(), i);
   }
}

void SamplePlayer::DropdownClicked(DropdownList* list)
{
   if (list == mSampleList)
   {
      UpdateSampleList();
   }
}

void SamplePlayer::DropdownUpdated(DropdownList* list, int oldVal)
{
   if (list == mSampleList)
   {
      UpdateSample(mBank->GetSampleInfo(mSampleIndex).mSample, false);
   }
}

void SamplePlayer::RadioButtonUpdated(RadioButton* radio, int oldVal)
{
   if (radio == mCuePointSelector)
      UpdateActiveCuePoint();
}

void SamplePlayer::UpdateActiveCuePoint()
{
   if (mActiveCuePointIndex >= 0 && mActiveCuePointIndex < (int)mSampleCuePoints.size())
   {
      mCuePointStartSlider->SetVar(&mSampleCuePoints[mActiveCuePointIndex].startSeconds);
      mCuePointLengthSlider->SetVar(&mSampleCuePoints[mActiveCuePointIndex].lengthSeconds);
      mCuePointSpeedSlider->SetVar(&mSampleCuePoints[mActiveCuePointIndex].speed);
   }
}

void SamplePlayer::FilesDropped(vector<string> files, int x, int y)
{
   Sample* sample = new Sample();
   sample->Read(files[0].c_str());
   UpdateSample(sample, true);
}

void SamplePlayer::UpdateSample(Sample* sample, bool ownsSample)
{
   Sample* oldSamplePtr = mSample;

   float lengthSeconds = sample->LengthInSamples() / (gSampleRate * sample->GetSampleRateRatio());
   for (size_t i = 0; i < mSampleCuePoints.size(); ++i)
   {
      mCuePointStartSlider->SetExtents(0, lengthSeconds);
      mCuePointLengthSlider->SetExtents(0, lengthSeconds);
   }
   
   sample->SetPlayPosition(0);
   sample->SetLooping(mLoop);
   sample->SetRate(mSpeed);
   mSample = sample;
   mVolume = 1;
   mPlay = false;
   mOwnsSample = ownsSample;   
   mZoomLevel = 1;
   mZoomOffset = 0;

   mErrorString = "";

   if (mOwnsSample)
      delete oldSamplePtr;

   mIsLoadingSample = true;
}

void SamplePlayer::ButtonClicked(ClickButton *button)
{
   if (mSample)
   {
      if (button == mPlayButton)
      {
         mPlay = true;
         mAdsr.Clear();
         mAdsr.Start(gTime + gBufferSize*gInvSampleRateMs, 1);
      }
      if (button == mPauseButton)
      {
         mPlay = false;
         SwitchAndRamp();
      }
      if (button == mStopButton)
      {
         mPlay = false;
         mSample->SetPlayPosition(0);
         SwitchAndRamp();
      }
   }
   if (button == mDownloadYoutubeButton)
      DownloadYoutube("https://www.youtube.com/watch?v="+mYoutubeId, "", mYoutubeId);
   if (button == mLoadFileButton)
      LoadFile();

   for (size_t i = 0; i < mSearchResultButtons.size(); ++i)
   {
      if (button == mSearchResultButtons[i])
      {
         if (i < mYoutubeSearchResults.size())
         {
            DownloadYoutube("https://www.youtube.com/watch?v=" + mYoutubeSearchResults[i].youtubeId, "", mYoutubeSearchResults[i].name);

            mYoutubeSearchResults.clear();
            break;
         }
      }
   }

   if (button == mPlayCurrentCuePointButton)
      PlayCuePoint(gTime, mActiveCuePointIndex, 127);
}

void SamplePlayer::TextEntryComplete(TextEntry* entry)
{
   if (entry == mDownloadYoutubeSearch)
   {
      SearchYoutube(mYoutubeSearch);
      //youtube-dl "ytsearch5:duck quack sound effect" --no-playlist --write-info-json --skip-download -o "test/%(title)s [len %(duration)s] [id %(id)s].%(ext)s"
   }
}

namespace
{
   const char* GetYoutubeDlPrefix()
   {
#if BESPOKE_WINDOWS
      return "youtube-dl.exe ";
#else
      return "export PATH=/opt/local/bin:$PATH; youtube-dl ";
#endif
   }
}

void SamplePlayer::DownloadYoutube(string search, string options, string title)
{
   mPlay = false;
   if (mSample)
      mSample->SetPlayPosition(0);

   const char* tempDownloadName = "youtube.m4a";
   {
      auto file = juce::File(ofToDataPath(tempDownloadName));
      if (file.existsAsFile())
         file.deleteFile();
   }

   const char* tempConvertedName = "youtube.wav";
   {
      auto file = juce::File(ofToDataPath(tempConvertedName));
      if (file.existsAsFile())
         file.deleteFile();
   }
   
   string command = GetYoutubeDlPrefix() + search + " --extract-audio --audio-format wav --audio-quality 0 --no-progress -o \""+ ofToDataPath(tempDownloadName) +"\" "+ options;
   ofLog() << "running " << command;
   mRunningProcessType = RunningProcessType::DownloadYoutube;

   mOnRunningProcessComplete = [this, tempConvertedName, title] { OnYoutubeDownloadComplete(tempConvertedName, title); };

   if (mRunningProcess)
      mRunningProcess->kill();
   delete mRunningProcess;
   mRunningProcess = new ChildProcess();
   mRunningProcess->start(command);
}

void SamplePlayer::OnYoutubeDownloadComplete(string filename, string title)
{
   Sample* sample = new Sample();
   if (juce::File(ofToDataPath(filename)).existsAsFile())
   {
      sample->Read(ofToDataPath(filename).c_str(), false, Sample::ReadType::Async);
      sample->SetName(title);
      UpdateSample(sample, true);
   }
   else
   {
      UpdateSample(new Sample(), true);
      mErrorString = "couldn't download sample. do you have youtube-dl and ffmpeg installed?";
   }
}

void SamplePlayer::SearchYoutube(string searchTerm)
{
   string tempPath = ofToDataPath("youtube_temp");
   auto dir = juce::File(tempPath);
   if (dir.exists())
      dir.deleteRecursively();
   dir.createDirectory();
   mYoutubeSearchResults.clear();

   string options = "\"ytsearch"+ofToString(kMaxYoutubeSearchResults)+":"+searchTerm+"\" --no-playlist --write-info-json --skip-download -o \""+tempPath+"/%(title)s#%(duration)s#%(id)s.%(ext)s\"";
   string command = GetYoutubeDlPrefix() + options;
   ofLog() << "running " << command;

   mRunningProcessType = RunningProcessType::SearchYoutube;

   mOnRunningProcessComplete = [this, searchTerm] { if (mYoutubeSearchResults.size() == 0) { mErrorString = "zero results found for " + searchTerm; } };

   if (mRunningProcess)
      mRunningProcess->kill();
   delete mRunningProcess;
   mRunningProcess = new ChildProcess();
   mRunningProcess->start(command);
}

void SamplePlayer::LoadFile()
{
   FileChooser chooser("Load sample", File(ofToDataPath("samples")),
                       TheSynth->GetGlobalManagers()->mAudioFormatManager.getWildcardForAllFormats(), true, false, TheSynth->GetMainComponent()->getTopLevelComponent());
   if (chooser.browseForFileToOpen())
   {
      auto file = chooser.getResult();

      Sample* sample = new Sample();
      if (file.existsAsFile())
         sample->Read(file.getFullPathName().toStdString().c_str());
      UpdateSample(sample, true);
   }
}

void SamplePlayer::FillData(vector<float> data)
{
   Sample* sample = new Sample();
   sample->Create(data.size());
   float* sampleData = sample->Data()->GetChannel(0);
   for (size_t i = 0; i < data.size(); ++i)
      sampleData[i] = data[i];
   UpdateSample(sample, true);
}

void SamplePlayer::OnClicked(int x, int y, bool right)
{
   IDrawableModule::OnClicked(x,y,right);
   
   if (right)
      return;

   if (mYoutubeSearchResults.size() > 0)
      return;
   
   if (y > 60 && y < mHeight - 20 && mSample != nullptr)
   {
      SwitchAndRamp();
      mPlay = true;
      mAdsr.Clear();
      mAdsr.Start(gTime + gBufferSize * gInvSampleRateMs, 1);
      mSample->SetPlayPosition(int(GetPlayPositionForMouse(x)));
      mScrubbingSample = true;

      if (mSetCuePoint)
         SetCuePointForX(x);
   }
}

bool SamplePlayer::MouseMoved(float x, float y)
{
   IDrawableModule::MouseMoved(x, y);
   if (mScrubbingSample && mSample != nullptr)
   {
      SwitchAndRamp();
      mSample->SetPlayPosition(int(GetPlayPositionForMouse(x)));
      mAdsr.Clear();
      mAdsr.Start(gTime + gBufferSize * gInvSampleRateMs, 1);

      if (mSetCuePoint)
         SetCuePointForX(x);
   }
   return true;
}

void SamplePlayer::SetCuePointForX(float mouseX)
{
   mSampleCuePoints[mActiveCuePointIndex].startSeconds = GetPlayPositionForMouse(mouseX) / (gSampleRate * mSample->GetSampleRateRatio());
   mSampleCuePoints[mActiveCuePointIndex].speed = mSpeed;
}

void SamplePlayer::MouseReleased()
{
   IDrawableModule::MouseReleased();
   mScrubbingSample = false;
}

float SamplePlayer::GetPlayPositionForMouse(float mouseX) const
{
   if (mSample != nullptr)
      return ofMap(mouseX, 5, mWidth-5, GetZoomStartSample(), GetZoomEndSample(), true);
   return 0;
}

void SamplePlayer::GetPlayInfoForPitch(int pitch, float& startSeconds, float& lengthSeconds, float& speed) const
{
   if (pitch < mSampleCuePoints.size())
   {
      startSeconds = mSampleCuePoints[pitch].startSeconds;
      lengthSeconds = mSampleCuePoints[pitch].lengthSeconds;
      speed = mSampleCuePoints[pitch].speed;
   }
   else
   {
      startSeconds = 0;
      lengthSeconds = 0;
      speed = 1;
   }
}

void SamplePlayer::SetCuePoint(int pitch, float startSeconds, float lengthSeconds, float speed)
{
   if (pitch < mSampleCuePoints.size())
   {
      mSampleCuePoints[pitch].startSeconds = startSeconds;
      mSampleCuePoints[pitch].lengthSeconds = lengthSeconds;
      mSampleCuePoints[pitch].speed = speed;
   }
}

void SamplePlayer::DrawModule()
{
   if (Minimized() || IsVisible() == false)
      return;
   
   mVolumeSlider->Draw();
   mSpeedSlider->Draw();
   mSampleList->Draw();
   mLoopCheckbox->Draw();
   mPlayButton->Draw();
   mPauseButton->Draw();
   mStopButton->Draw();
   mDownloadYoutubeButton->Draw();
   mDownloadYoutubeSearch->Draw();
   mLoadFileButton->Draw();
   mCuePointSelector->Draw();
   mSetCuePointCheckbox->Draw();
   mSelectPlayedCuePointCheckbox->Draw();
   mCuePointStartSlider->Draw();
   mCuePointLengthSlider->Draw();
   mCuePointSpeedSlider->Draw();
   mPlayCurrentCuePointButton->Draw();
   mShowGridCheckbox->Draw();

   for (size_t i = 0; i < mSearchResultButtons.size(); ++i)
   {
      if (i < mYoutubeSearchResults.size())
      {
         mSearchResultButtons[i]->SetShowing(true);
         int minutes = int(mYoutubeSearchResults[i].lengthSeconds / 60);
         int secondsRemainder = int(mYoutubeSearchResults[i].lengthSeconds) % 60;
         string lengthStr = ofToString(minutes) + ":";
         if (secondsRemainder < 10)
            lengthStr += "0";
         lengthStr += ofToString(secondsRemainder);
         mSearchResultButtons[i]->SetLabel(("(" + lengthStr + ") " + mYoutubeSearchResults[i].name).c_str());
      }
      else
      {
         mSearchResultButtons[i]->SetShowing(false);
      }
      mSearchResultButtons[i]->Draw();
   }

   ofPushMatrix();
   ofTranslate(5,58);
   if (mRunningProcess != nullptr || (mSample && mSample->IsSampleLoading()))
   {
      const int kNumDots = 8;
      const float kCircleRadius = 20;
      const float kDotRadius = 3;
      const float kSpinSpeed = .003f;
      ofPushStyle();
      ofFill();
      for (int i = 0; i < kNumDots; ++i)
      {
         float theta = float(i) / kNumDots * M_PI * 2 + gTime * kSpinSpeed;
         ofCircle(cos(theta) * kCircleRadius + (mWidth-10) * .5f, sin(theta) * kCircleRadius + (mHeight-65) * .5f, kDotRadius);
      }
      ofPopStyle();

      if (mSample && mSample->IsSampleLoading())
      {
         ofPushStyle();
         ofFill();
         ofSetColor(255, 255, 255, 50);
         ofRect(0, 0, (mWidth - 10) * mSample->GetSampleLoadProgress(), mHeight - 65);
         ofSetColor(40, 40, 40);
         DrawTextNormal("loading sample...", 10, 10, 10);
         ofPopStyle();
      }
   }
   else if (mYoutubeSearchResults.size() > 0)
   {
      //don't draw, buttons will draw instead
   }
   else if (mErrorString != "")
   {
      ofPushStyle();
      ofFill();
      ofSetColor(255, 255, 255, 50);
      ofRect(0, 0, mWidth - 10, mHeight - 65);
      ofSetColor(220, 0, 0);
      DrawTextNormal(mErrorString, 10, 10, 10);
      ofPopStyle();
   }
   else if (mSample && mSample->LengthInSamples() > 0)
   {
      if (mIsLoadingSample && !mSample->IsSampleLoading())
      {
         mIsLoadingSample = false;
         mDrawBuffer.Resize(mSample->LengthInSamples());
         mDrawBuffer.CopyFrom(mSample->Data());
      }

      float sampleWidth = mWidth - 10;
      DrawAudioBuffer(sampleWidth, mHeight - 65, &mDrawBuffer, GetZoomStartSample(), GetZoomEndSample(), mSample->GetPlayPosition());
      
      ofPushStyle();
      ofFill();

      ofSetColor(255, 255, 255);
      DrawTextNormal(mSample->Name(), 5, 27);

      float lengthSeconds = mSample->LengthInSamples() / (gSampleRate * mSample->GetSampleRateRatio());
      float x = ofMap(mSample->GetPlayPosition(), GetZoomStartSample(), GetZoomEndSample(), 0, sampleWidth);
      DrawTextNormal(ofToString(mSample->GetPlayPosition() / (gSampleRate * mSample->GetSampleRateRatio()), 1), x + 2, mHeight - 65, 11);

      if (mShowGrid && GetZoomEndSeconds() - GetZoomStartSeconds() < 5)
      {
         float lengthSeconds = GetZoomEndSeconds() - GetZoomStartSeconds();
         float lengthBeats = TheTransport->GetTempo() * (lengthSeconds / 60) * mSampleCuePoints[mActiveCuePointIndex].speed;
         float alpha = ofMap(lengthSeconds, 5, 4, 0, 200, true);
         ofSetColor(0, 255, 255, alpha);
         float secondsPerBeat = 60 / (TheTransport->GetTempo() * mSampleCuePoints[mActiveCuePointIndex].speed);
         float offset = mSampleCuePoints[mActiveCuePointIndex].startSeconds;
         float firstBeat = ceil((GetZoomStartSeconds() - offset) / secondsPerBeat);
         float firstBeatSeconds = firstBeat * secondsPerBeat + offset;
         for (int i = 0; i < ceil(lengthBeats); ++i)
         {
            float second = firstBeatSeconds + i * secondsPerBeat;
            float x = ofMap(second, GetZoomStartSeconds(), GetZoomEndSeconds(), 0, sampleWidth);
            ofLine(x, 0, x, mHeight - 65);
         }
      }

      for (size_t i=0; i<mSampleCuePoints.size(); ++i)
      {
         if (i == 0 || mSampleCuePoints[i].startSeconds != mSampleCuePoints[i-1].startSeconds)
         {
            float x = ofMap(mSampleCuePoints[i].startSeconds, GetZoomStartSeconds(), GetZoomEndSeconds(), 0, sampleWidth);
            float xEnd = ofMap(mSampleCuePoints[i].startSeconds + mSampleCuePoints[i].lengthSeconds, GetZoomStartSeconds(), GetZoomEndSeconds(), 0, sampleWidth);
            ofSetColor(0, 0, 0, 100);
            ofRect(x, 0, (xEnd - x) + 10, 10);
            ofRect(x, 0, 15, 10);
            ofSetColor(255, 255, 255);
            ofRect(x, 0, 1, 20, 1);
            if (i == mActiveCuePointIndex)
            {
               ofNoFill();
               ofRect(x, 0, 15, 10);
               ofFill();
            }
            DrawTextNormal(ofToString((int)i), x+2, 8, 11);
         }
      }

      ofPopStyle();
   }
   else
   {
      ofPushStyle();
      ofFill();
      ofSetColor(255,255,255,50);
      ofRect(0, 0, mWidth-10, mHeight - 65);
      ofSetColor(40,40,40);
      DrawTextNormal("drag and drop a sample here...", 10, 10, 10);
      ofPopStyle();
   }
   ofPopMatrix();

   if (mZoomLevel != 1)
   {
      ofNoFill();
      ofRect(5, mHeight - 7, mWidth-10, 7);
      ofFill();
      ofRect(mZoomOffset*(mWidth-10)+5, mHeight - 7, (mWidth-10)/mZoomLevel, 7);
   }
}

float SamplePlayer::GetZoomStartSample() const
{
   if (mSample == nullptr)
      return 0;
   return ofClamp(mSample->LengthInSamples() * mZoomOffset, 0, mSample->LengthInSamples());
}

float SamplePlayer::GetZoomEndSample() const
{
   if (mSample == nullptr)
      return 1;
   return ofClamp(GetZoomStartSample() + mSample->LengthInSamples() / mZoomLevel, 1, mSample->LengthInSamples());
}

float SamplePlayer::GetZoomStartSeconds() const
{
   if (mSample == nullptr)
      return 0;
   return GetZoomStartSample() / (gSampleRate * mSample->GetSampleRateRatio());
}

float SamplePlayer::GetZoomEndSeconds() const
{
   if (mSample == nullptr)
      return 1;
   return GetZoomEndSample() / (gSampleRate * mSample->GetSampleRateRatio());
}

void SamplePlayer::oscMessageReceived(const OSCMessage& msg)
{
   if (msg.getAddressPattern().toString() == "/wheel/z")
   {
      bool grabbed = msg[0].getFloat32() > 0;
      if (grabbed)
      {
         mOscWheelPos = FLT_MAX;
         mOscWheelSpeed = 0;
         mOscWheelGrabbed = true;
      }
      else
      {
         mOscWheelGrabbed = false;
      }
   }
   else if (msg.getAddressPattern().toString() == "/wheel/x")
   {
      float pos = msg[0].getFloat32();
      if (mOscWheelPos == FLT_MAX)
      {
         mOscWheelPos = pos;
      }
      
      mOscWheelSpeed = (pos - mOscWheelPos) * 70;
      mOscWheelPos = pos;
   }
   else if (msg.getAddressPattern().toString() == "/Fader/x")
   {
      float pos = msg[0].getFloat32();
      mSpeed = ofLerp(mSpeedSlider->GetMin(), mSpeedSlider->GetMax(), pos);
   }
}

void SamplePlayer::oscBundleReceived(const OSCBundle& bundle)
{
   for (const OSCBundle::Element* element = bundle.begin(); element != bundle.end(); ++element)
   {
      if (element->isMessage())
         oscMessageReceived(element->getMessage());
      else if (element->isBundle())
         oscBundleReceived(element->getBundle());
   }
}

bool SamplePlayer::MouseScrolled(int x, int y, float scrollX, float scrollY)
{
   if (fabs(scrollX) > fabsf(scrollY))
      scrollY = 0;
   else
      scrollX = 0;

   //horizontal scroll
   mZoomOffset = ofClamp(mZoomOffset + scrollX*.005f, 0, 1);

   //zoom scroll
   float oldZoomLevel = mZoomLevel;
   mZoomLevel = ofClamp(mZoomLevel + scrollY*.2f, 1, 20);
   float zoomAmount = (mZoomLevel - oldZoomLevel) / oldZoomLevel; //find actual adjusted amount
   float zoomCenter = ofMap(x, 5, mWidth-10, 0, 1, true)/oldZoomLevel;
   mZoomOffset += zoomCenter * zoomAmount;
   if (mZoomLevel == 1)
      mZoomOffset = 0;

   return false;
}

void SamplePlayer::CheckboxUpdated(Checkbox* checkbox)
{
   if (checkbox == mLoopCheckbox)
   {
      if (mSample != nullptr)
         mSample->SetLooping(mLoop);
   }
}

void SamplePlayer::GetModuleDimensions(float& width, float& height)
{
   width = mWidth;
   height = mHeight;
}

void SamplePlayer::FloatSliderUpdated(FloatSlider* slider, float oldVal)
{
}

void SamplePlayer::IntSliderUpdated(IntSlider* slider, int oldVal)
{
}

void SamplePlayer::LoadLayout(const ofxJSONElement& moduleInfo)
{
   mModuleSaveData.LoadString("target", moduleInfo);
   mModuleSaveData.LoadString("samplebank", moduleInfo,"",FillDropdown<SampleBank*>);
   mModuleSaveData.LoadFloat("width", moduleInfo, mWidth);
   mModuleSaveData.LoadFloat("height", moduleInfo, mHeight);
   mModuleSaveData.LoadBool("show_youtube_process_output", moduleInfo, false);
   
   SetUpFromSaveData();
}

void SamplePlayer::SaveLayout(ofxJSONElement& moduleInfo)
{
   IDrawableModule::SaveLayout(moduleInfo);
   moduleInfo["samplebank"] = mBank ? mBank->Name() : "";
   moduleInfo["width"] = mWidth;
   moduleInfo["height"] = mHeight;
}

void SamplePlayer::SetUpFromSaveData()
{
   SetTarget(TheSynth->FindModule(mModuleSaveData.GetString("target")));
   mSampleBankCable->SetTarget(TheSynth->FindModule(mModuleSaveData.GetString("samplebank"),false));
   Resize(mModuleSaveData.GetFloat("width"), mModuleSaveData.GetFloat("height"));
}

namespace
{
   const int kSaveStateRev = 1;
}

void SamplePlayer::SaveState(FileStreamOut& out)
{
   IDrawableModule::SaveState(out);
   
   out << kSaveStateRev;
   
   bool hasSample = (mSample != nullptr);
   out << hasSample;
   if (hasSample)
      mSample->SaveState(out);
   
   out << (int)mSampleCuePoints.size();
   for (size_t i=0; i<mSampleCuePoints.size(); ++i)
   {
      out << mSampleCuePoints[i].startSeconds;
      out << mSampleCuePoints[i].lengthSeconds;
      out << mSampleCuePoints[i].speed;
   }
}

void SamplePlayer::LoadState(FileStreamIn& in)
{
   IDrawableModule::LoadState(in);
   
   int rev;
   in >> rev;
   LoadStateValidate(rev <= kSaveStateRev);
   
   bool hasSample;
   in >> hasSample;
   if (hasSample)
   {
      Sample* sample = new Sample();
      sample->LoadState(in);
      UpdateSample(sample, true);
   }
   
   if (rev >= 1)
   {
      int size;
      in >> size;
      mSampleCuePoints.resize(size);
      for (size_t i = 0; i < size; ++i)
      {
         in >> mSampleCuePoints[i].startSeconds;
         in >> mSampleCuePoints[i].lengthSeconds;
         in >> mSampleCuePoints[i].speed;
      }
   }
}

vector<IUIControl*> SamplePlayer::ControlsToIgnoreInSaveState() const
{
   vector<IUIControl*> ignore;
   ignore.push_back(mDownloadYoutubeSearch);
   ignore.push_back(mLoadFileButton);
   return ignore;
}
