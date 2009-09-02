// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#include "Globals.h"

#if defined(HAVE_WX) && HAVE_WX
#include "GUI/ConfigDlg.h"
#include "Debugger/Debugger.h" // for the CDebugger class
#endif

#include "Config.h"
#include "LookUpTables.h"
#include "ImageWrite.h"
#include "Render.h"
#include "nGLUtil.h"
#include "Fifo.h"
#include "OpcodeDecoding.h"
#include "TextureMngr.h"
#include "BPStructs.h"
#include "VertexLoader.h"
#include "VertexLoaderManager.h"
#include "VertexManager.h"
#include "PixelShaderCache.h"
#include "PixelShaderManager.h"
#include "VertexShaderCache.h"
#include "VertexShaderManager.h"
#include "XFB.h"
#include "XFBConvert.h"
#include "TextureConverter.h"

#include "VideoState.h"

SVideoInitialize g_VideoInitialize;
PLUGIN_GLOBALS* globals;


// Nasty stuff which win32 needs for wxw

#if defined(_WIN32) && defined(HAVE_WX) && HAVE_WX
HINSTANCE g_hInstance;

class wxDLLApp : public wxApp
{
	bool OnInit()
	{
		return true;
	}
};
IMPLEMENT_APP_NO_MAIN(wxDLLApp) 

WXDLLIMPEXP_BASE void wxSetInstance(HINSTANCE hInst);

BOOL APIENTRY DllMain(HINSTANCE hinstDLL,	// DLL module handle
					  DWORD dwReason,		// reason called
					  LPVOID lpvReserved)	// reserved
{
	switch (dwReason)
	{
	case DLL_PROCESS_ATTACH:
		{       // Use wxInitialize() if you don't want GUI instead of the following 12 lines
			wxSetInstance((HINSTANCE)hinstDLL);
			int argc = 0;
			char **argv = NULL;
			wxEntryStart(argc, argv);
			if ( !wxTheApp || !wxTheApp->CallOnInit() )
				return FALSE;
		}
		break; 

	case DLL_PROCESS_DETACH:
		wxEntryCleanup(); // Use wxUninitialize() if you don't want GUI 
		break;
	default:
		break;
	}

	g_hInstance = hinstDLL;
	return TRUE;
}
#endif


/* Create debugging window. There's currently a strange crash that occurs whe a game is loaded
   if the OpenGL plugin was loaded before. I'll try to fix that. Currently you may have to
   clsoe the window if it has auto started, and then restart it after the dll has loaded
   for the purpose of the game. At that point there is no need to use the same dll instance
   as the one that is rendering the game. However, that could be done. */

#if defined(HAVE_WX) && HAVE_WX
CDebugger *m_frame;
void DllDebugger(HWND _hParent, bool Show)
{
	if(!m_frame && Show)
	{
		m_frame = new CDebugger(NULL);
		m_frame->Show();
	}
	else if (m_frame && !Show)
	{
		if(m_frame->Close())
			m_frame = NULL;
	}
}

void DoDllDebugger(){}
#else
void DllDebugger(HWND _hParent, bool Show) { }
void DoDllDebugger() { }
#endif


void GetDllInfo (PLUGIN_INFO* _PluginInfo) 
{
    _PluginInfo->Version = 0x0100;
    _PluginInfo->Type = PLUGIN_TYPE_VIDEO;
#ifdef DEBUGFAST 
    sprintf(_PluginInfo->Name, "Dolphin OpenGL (DebugFast)");
#elif defined _DEBUG
    sprintf(_PluginInfo->Name, "Dolphin OpenGL (Debug)");
#else
    sprintf(_PluginInfo->Name, "Dolphin OpenGL");
#endif
}

void SetDllGlobals(PLUGIN_GLOBALS* _pPluginGlobals) {
    globals = _pPluginGlobals;
}

void DllConfig(HWND _hParent)
{
	wxWindow * win = new wxWindow();
#ifdef _WIN32
	win->SetHWND((WXHWND)_hParent);
	win->AdoptAttributesFromHWND();
#endif
	//win->Reparent(wxGetApp().GetTopWindow());
	ConfigDialog *frame = new ConfigDialog(win);
	OpenGL_AddBackends(frame);
	OpenGL_AddResolutions(frame);
	frame->ShowModal();
}

void Initialize(void *init)
{
    frameCount = 0;
    SVideoInitialize *_pVideoInitialize = (SVideoInitialize*)init;
    g_VideoInitialize = *(_pVideoInitialize); // Create a shortcut to _pVideoInitialize that can also update it

    InitLUTs();
    InitXFBConvTables();
    g_Config.Load();
    
    if (!OpenGL_Create(g_VideoInitialize, 640, 480)) { //640x480 will be the default if all else fails//
        g_VideoInitialize.pLog("Renderer::Create failed\n", TRUE);
        return;
    }
    _pVideoInitialize->pPeekMessages = g_VideoInitialize.pPeekMessages;
    _pVideoInitialize->pUpdateFPSDisplay = g_VideoInitialize.pUpdateFPSDisplay;
    _pVideoInitialize->pWindowHandle = g_VideoInitialize.pWindowHandle;

    Renderer::AddMessage("Dolphin OpenGL Video Plugin" ,5000);
}

void DoState(unsigned char **ptr, int mode) {
#ifndef _WIN32
	OpenGL_MakeCurrent();
#endif
    // Clear all caches that touch RAM
    TextureMngr::Invalidate();
    // DisplayListManager::Invalidate();
    
    VertexLoaderManager::MarkAllDirty();
    
    PointerWrap p(ptr, mode);
    VideoCommon_DoState(p);
    
    // Refresh state.
    if (mode == PointerWrap::MODE_READ)
        BPReload();
}

// This is called after Video_Initialize() from the Core
void Video_Prepare(void)
{
    OpenGL_MakeCurrent();
    if (!Renderer::Init()) {
        g_VideoInitialize.pLog("Renderer::Create failed\n", TRUE);
        PanicAlert("Can't create opengl renderer. You might be missing some required opengl extensions, check the logs for more info");
        exit(1);
    }

    TextureMngr::Init();

    BPInit();
    VertexManager::Init();
    Fifo_Init(); // must be done before OpcodeDecoder_Init()
    OpcodeDecoder_Init();
    VertexShaderCache::Init();
    VertexShaderManager::Init();
    PixelShaderCache::Init();
    PixelShaderManager::Init();
    GL_REPORT_ERRORD();
    VertexLoaderManager::Init();
    TextureConverter::Init();
}

void Shutdown(void) 
{
    TextureConverter::Shutdown();
    VertexLoaderManager::Shutdown();
    VertexShaderCache::Shutdown();
    VertexShaderManager::Shutdown();
    PixelShaderManager::Shutdown();
    PixelShaderCache::Shutdown();
    Fifo_Shutdown();
    VertexManager::Shutdown();
    TextureMngr::Shutdown();
    OpcodeDecoder_Shutdown();
    Renderer::Shutdown();
    OpenGL_Shutdown();
}

void Video_Stop(void) 
{
    Fifo_Stop();
}

void Video_EnterLoop()
{
    Fifo_EnterLoop(g_VideoInitialize);
}

bool ScreenShot(TCHAR *File) 
{
    char str[64];
    int left = 200, top = 15;
    sprintf(str, "Dolphin OpenGL");

    Renderer::ResetGLState();
    Renderer::RenderText(str, left+1, top+1, 0xff000000);
    Renderer::RenderText(str, left, top, 0xffc0ffff);
    Renderer::RestoreGLState();

    if (Renderer::SaveRenderTarget(File, 0)) {
        char msg[255];
        sprintf(msg, "saved %s\n", File);
        Renderer::AddMessage(msg, 500);
    	return true;
    }
	return false;
}

unsigned int Video_Screenshot(TCHAR* _szFilename)
{
    if (ScreenShot(_szFilename))
        return TRUE;

    return FALSE;
}

void Video_AddMessage(const char* pstr, u32 milliseconds)
{
	Renderer::AddMessage(pstr,milliseconds);
}

void Video_UpdateXFB(u8* _pXFB, u32 _dwWidth, u32 _dwHeight, s32 _dwYOffset, bool scheduling)
{
	if(g_Config.bUseXFB && XFB_isInit())
	{
		if (scheduling) // from CPU in DC without fifo&CP (some 2D homebrews)
		{
			XFB_SetUpdateArgs(_pXFB, _dwWidth, _dwHeight, _dwYOffset);
			g_XFBUpdateRequested = TRUE;
		}
		else
		{
			if (_pXFB) // from CPU in SC mode
				XFB_Draw(_pXFB, _dwWidth, _dwHeight, _dwYOffset);
			else // from GP in DC without fifo&CP (some 2D homebrews)
			{
				XFB_Draw();
				g_XFBUpdateRequested = FALSE;
			}
		}
	}
}
