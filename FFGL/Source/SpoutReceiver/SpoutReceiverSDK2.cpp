﻿/*
	
	SpoutReceiverSDK2.dll

	LJ - leadedge@adam.com.au

	FFGL plugin for receiving DirectX texture from an equivalent
	sending application	either using wglDxInterop or memory share
	Note fix to FFGL.cpp to allow setting string parameters
	http://resolume.com/forum/viewtopic.php?f=14&t=10324
	----------------------------------------------------------------------
	26.06.14 - major change to use Spout SDK
	08-07-14 - Version 3.000
	14.07-14 - changed to fixed SpoutReceiver object
	16.07.14 - restored host fbo binding after readtexture otherwise texture draw does not work
			 - used a local texture for both textureshare and memoryshare
	25.07.14 - Version 3.001 - corrected ReceiveTexture in SpoutSDK.cpp for reset if the sender was closed
	01.08.14 - Version 3.002 - external sender registration
	13.08.14 - Version 3.003 - restore viewport
	18.08.14 - recompiled for testing and copied to GitHub
	20.08.14 - activated event locks
			 - included DX9 mode compile flag (default true for Version 1 compatibility)
			 - included DX9 arg for SelectSenderPanel
			 - Version 3.004
			 - recompiled for testing and copied to GitHub
	=======================================================================================================
	24.08.14 - recompiled with MB sendernames class revision
			 - disabled mouse hook for SpoutPanel
			 - Version 3.005
	26.08.14 - removed mouse hook
			 - detect existing sender name on restart after Magic full screen.
			   Version 3.006
	29.08.14 - detect host name and dll start
			 - user messages for revised SpoutPanel instead of MessageBox
			 - Version 3.007
	31.08.14 - changed from quad to vertex array for draw
	01.09.14 - leak test and some changes made to SpoutGLDXinterop
	02.09.14 - added more information in plugin Description and About
			 - Version 3.008
			 - Uploaded to GitHub
	15-09-14 - added RestoreOpenGLstate before return for sender size change
			 - Version 3.009
	16-09-14 - change from saving state matrices to just saving the viewport
			 - Version 3.010


*/
#include "SpoutReceiverSDK2.h"
#include <FFGL.h>
#include <FFGLLib.h>

// #include <windows.h>

// To force memoryshare, enable the define in below
// #define MemoryShareMode

#ifndef MemoryShareMode
	#define FFPARAM_SharingName		(0)
	#define FFPARAM_Update			(1)
	#define FFPARAM_Select			(2)
	#define FFPARAM_Aspect			(3)

#else
	#define FFPARAM_Aspect			(0)
#endif
        
////////////////////////////////////////////////////////////////////////////////////////////////////
//  Plugin information
////////////////////////////////////////////////////////////////////////////////////////////////////
static CFFGLPluginInfo PluginInfo (
	SpoutReceiverSDK2::CreateInstance,				// Create method
	#ifndef MemoryShareMode
	"OF48",										// Plugin unique ID
	"SpoutReceiver2",							// Plugin name (receive texture from DX)
	1,											// API major version number
	001,										// API minor version number
	2,											// Plugin major version number
	001,										// Plugin minor version number
	FF_SOURCE,									// Plugin type
	"Spout Receiver - Vers 3.010\nReceives textures from Spout Senders\n\nSender Name : enter a sender name\nUpdate : update the name entry\nSelect : select a sender using 'SpoutPanel'\nAspect : preserve aspect ratio of the received sender", // Plugin description
	#else
	"OF49",										// Plugin unique ID
	"SpoutReceiver2M",							// Plugin name (receive texture from DX)
	1,											// API major version number
	001,										// API minor version number
	2,											// Plugin major version number
	001,										// Plugin minor version number
	FF_SOURCE,									// Plugin type
	"Spout Memoryshare receiver",				// Plugin description
	#endif
	"S P O U T - Version 2\nspout.zeal.co"		// About
);

/////////////////////////////////
//  Constructor and destructor //
/////////////////////////////////
SpoutReceiverSDK2::SpoutReceiverSDK2()
:CFreeFrameGLPlugin(),
 m_initResources(1),
 m_maxCoordsLocation(-1)
{

	HMODULE module;
	char path[MAX_PATH];

	/*
	// Debug console window so printf works
	FILE* pCout;
	AllocConsole();
	freopen_s(&pCout, "CONOUT$", "w", stdout); 
	printf("SpoutReceiver2 Vers 3.010\n");
	*/


	// Input properties - this is a source and has no inputs
	SetMinInputs(0);
	SetMaxInputs(0);
	
	//
	// ======== initial values ========
	//

	// Memoryshare define
	// if set to true (memory share only) this over-rides bMemoryMode below
	// and it connects as memory share and there is no user option to select
	// default is false (automatic)
	#ifdef MemoryShareMode
	bMemoryMode = true;
	#else
	bMemoryMode = false;
	#endif
	
	g_Width	= 512;
	g_Height = 512;        // arbitrary initial image size
	myTexture = 0;         // only used for memoryshare mode

	bInitialized = false;
	bDX9mode = true;       // DirectX 9 mode rather than DirectX 11
	bMemoryMode = false;   // default mode is texture rather than memory
	bInitialized = false;  // not initialized yet by either means
	bAspect = false;       // preserve aspect ratio of received texture in draw
	bStarted = false;
	UserSenderName[0] = 0; // User entered sender name
	strcpy_s(InitialSenderName, "0x8e14549a"); // Start of the SpoutCam CLSID for an arbitrary name that will never be entered

	//
	// Parameters
	//
	#ifndef MemoryShareMode
	SetParamInfo(FFPARAM_SharingName, "Sender Name",   FF_TYPE_TEXT, "");
	SetParamInfo(FFPARAM_Update,      "Update",        FF_TYPE_EVENT, false );
	SetParamInfo(FFPARAM_Select,      "Select Sender", FF_TYPE_EVENT, false );

	bMemoryMode = false;
	#else
	bMemoryMode = true;
	#endif
	SetParamInfo(FFPARAM_Aspect,       "Aspect",       FF_TYPE_BOOLEAN, false );

	// For memory mode, tell Spout to use memoryshare
	if(bMemoryMode) {
		receiver.SetMemoryShareMode();
		// Give it a user name for ProcessOpenGL
		strcpy_s(UserSenderName, 256, InitialSenderName); 
	}

	// Set DirectX mode depending on DX9 flag
	if(bDX9mode) 
		receiver.SetDX9(true);
	else 
	    receiver.SetDX9(false);

	// Find the host executable name
	module = GetModuleHandle(NULL);
	GetModuleFileNameA(module, path, MAX_PATH);
	_splitpath_s(path, NULL, 0, NULL, 0, HostName, MAX_PATH, NULL, 0);
	// Magic reacts on button-up, so when the dll loads
	// the parameters are not activated. 
	// Isadora and Resolume act on button down and 
	// Isadora activates all parameters on plugin load.
	// So allow one cycle for starting.
	if(strcmp(HostName, "Magic") == 0) bStarted = true;

}


SpoutReceiverSDK2::~SpoutReceiverSDK2()
{
	// OpenGL context required
	if(wglGetCurrentContext()) {
		// ReleaseReceiver does nothing if there is no receiver
		receiver.ReleaseReceiver();
		if(myTexture != 0) glDeleteTextures(1, &myTexture);
		myTexture = 0;
	}

}


////////////////////////////////////////////////////////////
//						Methods                           //
////////////////////////////////////////////////////////////
DWORD SpoutReceiverSDK2::InitGL(const FFGLViewportStruct *vp)
{
	if(UserSenderName[0] == 0) {
		// For detection of the active sender leave UserSenderName empty
		// If it is given some value it will keep trying to connect
		// until the user enters a name of a sender that exists
		// If this is the behaviour required give it an initial name
		// Otherwise do not and it will start with the active sender
		// strcpy_s(UserSenderName, 256, InitialSenderName);
	}
	return FF_SUCCESS;
}


DWORD SpoutReceiverSDK2::DeInitGL()
{
	// OpenGL context required
	if(wglGetCurrentContext()) {
		// ReleaseReceiver does nothing if there is no receiver
		receiver.ReleaseReceiver();
		if(myTexture != 0) glDeleteTextures(1, &myTexture);
		myTexture = 0;
	}
	return FF_SUCCESS;
}


DWORD SpoutReceiverSDK2::ProcessOpenGL(ProcessOpenGLStruct *pGL)
{
	bool bRet;

	//
	// Initialize a receiver
	//
	//
	if(!bInitialized) {

		// If UserSenderName is already set, CreateReceiver will attempt to connect to
		// that sender otherwise if UserSenderName is NULL the active sender will be used.
		if(UserSenderName[0]) strcpy_s(SenderName, UserSenderName);

		// CreateReceiver will return true only if it finds a sender running.
		if(receiver.CreateReceiver(SenderName, g_Width, g_Height)) {
			// Did it initialized in Memory share mode ?
			bMemoryMode = receiver.GetMemoryShareMode();
			// Initialize a texture - Memorymode RGB or Texturemode RGBA
			InitTexture();
			bInitialized = true;
		}

		return FF_SUCCESS;
	}
	else {
		//
		// Receive a shared texture
		//
		//	Success : Returns the sender name, width and height
		//	Failure : No sender detected
		//
		SetViewport(); // Aspect ratio control changes the viewport
		bRet = receiver.ReceiveTexture(SenderName, width, height, myTexture, GL_TEXTURE_2D);

		// Important - Restore the FFGL host FBO binding BEFORE the draw
		if(pGL->HostFBO) glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, pGL->HostFBO);
		if(bRet) {
			// Received the texture OK, but the sender or texture dimensions could have changed
			// Reset the global width and height so that the viewport can be set for aspect ratio control
			if(width != g_Width || height != g_Height) {
				g_Width  = width;
				g_Height = height;
				// Reset the local texture
				InitTexture();
				RestoreViewport(); // don't forget this
				return FF_SUCCESS;
			} // endif sender has changed
			// All matches so draw the texture
			DrawTexture(myTexture);
		}
		RestoreViewport(); // back to what it was
	}

	return FF_SUCCESS;

}


void SpoutReceiverSDK2::SetViewport()
{
	float fx, fy, aspect, vpScaleX, vpScaleY, vpWidth, vpHeight;
	int vpx, vpy;

	// Push current viewport attributes
	glPushAttrib(GL_VIEWPORT_BIT );

	// find the current viewport dimensions to scale to the aspect ratio required
	// and to restore the viewport afterwards
	glGetIntegerv(GL_VIEWPORT, vpdim);

	// Scale width and height to the current viewport size
	vpScaleX = (float)vpdim[2]/(float)g_Width;
	vpScaleY = (float)vpdim[3]/(float)g_Height;
	vpWidth  = (float)g_Width  * vpScaleX;
	vpHeight = (float)g_Height * vpScaleY;
	vpx = vpy = 0;

	// User option "Aspect" to preserve aspect ratio
	// Note - width is primary
	if(bAspect) {
		// back to original aspect ratio
		aspect = (float)g_Width/(float)g_Height;
		if(g_Width > g_Height) {
			fy = vpWidth/aspect;
			vpy = (int)(vpHeight-fy)/2;
			vpHeight = fy;
		}
		else {
			fx = vpHeight/aspect;
			vpx = (int)(vpWidth-fx)/2;
			vpWidth = fx;
		}
	}
	glViewport(vpx, vpy, (int)vpWidth, (int)vpHeight);

}


void SpoutReceiverSDK2::RestoreViewport()
{
	glViewport(vpdim[0], vpdim[1], vpdim[2], vpdim[3]);
	glPopAttrib();
}


DWORD SpoutReceiverSDK2::GetParameter(DWORD dwIndex)
{
	DWORD dwRet = FF_FAIL;

	#ifndef MemoryShareMode
	switch (dwIndex) {

		case FFPARAM_SharingName:
			dwRet = (DWORD)UserSenderName;
			return dwRet;
		default:
			return FF_FAIL;
	}
	#endif

	return FF_FAIL;
}


DWORD SpoutReceiverSDK2::SetParameter(const SetParameterStruct* pParam)
{
	unsigned int width, height;
	HANDLE dxShareHandle;
	DWORD dwFormat;

	if (pParam != NULL) {

		switch (pParam->ParameterNumber) {

		// These parameters will not exist for memoryshare mode
		#ifndef MemoryShareMode

		case FFPARAM_SharingName:
			if(pParam->NewParameterValue && strlen((char*)pParam->NewParameterValue) > 0) {
				strcpy_s(UserSenderName, 256, (char*)pParam->NewParameterValue);
				// If there is anything already in this field at startup, it is set by a saved composition
			}
			else {
				// Reset to an empty string so that the active sender 
				// is used and SelectSenderPanel works
				UserSenderName[0] = 0;
			}
			break;

		case FFPARAM_Update :
			// Update the user entered name
			if(pParam->NewParameterValue) { // name entry toggle is on
				// Is there a  user entered name
				if(UserSenderName[0] != 0) {
					if(!(bInitialized && strcmp(UserSenderName, SenderName) == 0)) {
						// Does the sender exist ?
						if(receiver.GetSenderInfo(UserSenderName, width, height, dxShareHandle, dwFormat)) {
							// Is it an external unregistered sender - e.g. VVVV ?
							if(!receiver.spout.interop.senders.FindSenderName(UserSenderName) ) {
								// register it
								receiver.spout.interop.senders.RegisterSenderName(UserSenderName);
							}
							// The user has typed it in, so make it the active sender
							receiver.spout.interop.senders.SetActiveSender(UserSenderName);
							// Start again
							if(bInitialized) receiver.ReleaseReceiver();
							bInitialized = false;
						}
					} // endif not already initialized and the same name
				} // endif user name entered
			} // endif Update
			break;

		// SpoutPanel sender selection
		case FFPARAM_Select :
			if (pParam->NewParameterValue) {
				if(bStarted) {
					if(UserSenderName[0]) {
						receiver.SelectSenderPanel("Using 'Sender Name' entry\nClear the name entry first");
					}
					else {
						if(bDX9mode)
							receiver.SelectSenderPanel("/DX9");
						else
							receiver.SelectSenderPanel("/DX11"); // default DX11 compatible
					}
				}
				else {
					bStarted = true;
				}
			} // endif new parameter
			break;

		#endif

		case FFPARAM_Aspect:
			if(pParam->NewParameterValue > 0)
				bAspect = true;
			else 
				bAspect = false;
			break;

		default:
			break;

		}
		return FF_SUCCESS;
	}

	return FF_FAIL;

}


// Initialize a local texture
void SpoutReceiverSDK2::InitTexture()
{
	if(myTexture != 0) {
		glDeleteTextures(1, &myTexture);
		myTexture = 0;
	}

	glGenTextures(1, &myTexture);
	glBindTexture(GL_TEXTURE_2D, myTexture);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	if(bMemoryMode)
		glTexImage2D(GL_TEXTURE_2D, 0,  GL_RGB, g_Width, g_Height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	else
		glTexImage2D(GL_TEXTURE_2D, 0,  GL_RGBA, g_Width, g_Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glBindTexture(GL_TEXTURE_2D, 0);

}


// Draw a texture
void SpoutReceiverSDK2::DrawTexture(GLuint TextureID)
{

	GLfloat tex_coords[] = {
						 0.0, 1.0,
						 0.0, 0.0,
						 1.0, 0.0,
						 1.0, 1.0 };

	GLfloat verts[] =  {
						-1.0, -1.0,
						-1.0,  1.0,
						 1.0,  1.0,
						 1.0, -1.0 };

	glPushMatrix();
	glColor4f(1.f, 1.f, 1.f, 1.f);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, TextureID); // bind our local texture

	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	glTexCoordPointer(2, GL_FLOAT, 0, tex_coords );
	glEnableClientState(GL_VERTEX_ARRAY);		
	glVertexPointer(2, GL_FLOAT, 0, verts );
	glDrawArrays( GL_TRIANGLE_FAN, 0, 4 );
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);

	glBindTexture(GL_TEXTURE_2D, 0);
	glDisable(GL_TEXTURE_2D);
	glPopMatrix();

}
