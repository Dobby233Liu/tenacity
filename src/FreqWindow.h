/**********************************************************************

  Audacity: A Digital Audio Editor

  FreqWindow.h

  Dominic Mazzoni

**********************************************************************/

#ifndef __AUDACITY_FREQ_WINDOW__
#define __AUDACITY_FREQ_WINDOW__

#include <vector>
#include <wx/font.h> // member variable
#include <wx/statusbr.h> // to inherit
#include "SpectrumAnalyst.h"
#include "widgets/wxPanelWrapper.h" // to inherit

// Tenacity libraries
#include <lib-math/SampleFormat.h>
#include <lib-preferences/Prefs.h>

class wxMemoryDC;
class wxScrollBar;
class wxSlider;
class wxTextCtrl;
class wxButton;
class wxCheckBox;
class wxChoice;

class TenacityProject;
class FrequencyPlotDialog;
class FreqGauge;
class RulerPanel;

DECLARE_EXPORTED_EVENT_TYPE(TENACITY_DLL_API, EVT_FREQWINDOW_RECALC, -1);

class FreqPlot final : public wxWindow
{
public:
   FreqPlot(wxWindow *parent, wxWindowID winid);

   // We don't need or want to accept focus.
   bool AcceptsFocus() const;

private:
   void OnPaint(wxPaintEvent & event);
   void OnErase(wxEraseEvent & event);
   void OnMouseEvent(wxMouseEvent & event);

private:
    FrequencyPlotDialog *freqWindow;

    DECLARE_EVENT_TABLE()
};

class FrequencyPlotDialog final : public wxDialogWrapper,
                                  public PrefsListener
{
public:
   FrequencyPlotDialog(wxWindow *parent, wxWindowID id,
              TenacityProject &project,
              const TranslatableString & title, const wxPoint & pos);
   virtual ~ FrequencyPlotDialog();

   bool Show( bool show = true ) override;

private:
   void Populate();

   void GetAudio();

   void PlotMouseEvent(wxMouseEvent & event);
   void PlotPaint(wxPaintEvent & event);

   void OnCloseWindow(wxCloseEvent & event);
   void OnCloseButton(wxCommandEvent & event);
   void OnGetURL(wxCommandEvent & event);
   void OnSize(wxSizeEvent & event);
   void OnPanScroller(wxScrollEvent & event);
   void OnZoomSlider(wxCommandEvent & event);
   void OnAlgChoice(wxCommandEvent & event);
   void OnSizeChoice(wxCommandEvent & event);
   void OnFuncChoice(wxCommandEvent & event);
   void OnAxisChoice(wxCommandEvent & event);
   void OnExport(wxCommandEvent & event);
   void OnReplot(wxCommandEvent & event);
   void OnGridOnOff(wxCommandEvent & event);
   void OnRecalc(wxCommandEvent & event);

   void SendRecalcEvent();
   void Recalc();
   void DrawPlot();
   void DrawBackground(wxMemoryDC & dc);

   // PrefsListener implementation
   void UpdatePrefs() override;

 private:
   bool mDrawGrid;
   int mSize;
   SpectrumAnalyst::Algorithm mAlg;
   int mFunc;
   int mAxis;
   int dBRange;
   TenacityProject *mProject;

#ifdef __WXMSW__
   static const int fontSize = 8;
#else
   static const int fontSize = 10;
#endif

   RulerPanel *vRuler;
   RulerPanel *hRuler;
   FreqPlot *mFreqPlot;
   FreqGauge *mProgress;

   wxRect mPlotRect;

   wxFont mFreqFont;

   std::unique_ptr<wxCursor> mArrowCursor;
   std::unique_ptr<wxCursor> mCrossCursor;

   wxButton *mCloseButton;
   wxButton *mExportButton;
   wxButton *mReplotButton;
   wxCheckBox *mGridOnOff;
   wxChoice *mAlgChoice;
   wxChoice *mSizeChoice;
   wxChoice *mFuncChoice;
   wxChoice *mAxisChoice;
   wxScrollBar *vPanScroller;
   wxSlider *vZoomSlider;
   wxTextCtrl *mCursorText;
   wxTextCtrl *mPeakText;


   double mRate;
   size_t mDataLen;
   Floats mData;
   size_t mWindowSize;

   /// Whether x axis is in log-frequency.
   bool mLogAxis;
   /// The minimum y value to plot.
   float mYMin;
   /// The maximum y value to plot.
   float mYMax;

   std::unique_ptr<wxBitmap> mBitmap;

   int mMouseX;
   int mMouseY;

   std::unique_ptr<SpectrumAnalyst> mAnalyst;

   DECLARE_EVENT_TABLE()

   friend class FreqPlot;
};

#endif
