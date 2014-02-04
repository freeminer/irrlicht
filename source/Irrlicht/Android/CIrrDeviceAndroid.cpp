// Copyright (C) 2002-2007 Nikolaus Gebhardt
// Copyright (C) 2007-2011 Christian Stehno
// This file is part of the "Irrlicht Engine".
// For conditions of distribution and use, see copyright notice in irrlicht.h

#include "CIrrDeviceAndroid.h"

#ifdef _IRR_COMPILE_WITH_ANDROID_DEVICE_

#include "os.h"
#include "CFileSystem.h"
#include "CAndroidAssetReader.h"
#include "CAndroidAssetFileArchive.h"
#include "CEGLManager.h"
#include "ISceneManager.h"
#include "IGUIEnvironment.h"
#include "CEGLManager.h"

namespace irr
{
	namespace video
	{
		IVideoDriver* createOGLES1Driver(const SIrrlichtCreationParameters& params,
			io::IFileSystem* io, video::IContextManager* contextManager);

		IVideoDriver* createOGLES2Driver(const SIrrlichtCreationParameters& params,
			io::IFileSystem* io, video::IContextManager* contextManager);
	}
}

namespace irr
{

CIrrDeviceAndroid::CIrrDeviceAndroid(const SIrrlichtCreationParameters& param)
	: CIrrDeviceStub(param), Focused(false), Initialized(false), Paused(true)
{
#ifdef _DEBUG
	setDebugName("CIrrDeviceAndroid");
#endif
	previousMotionData = new core::map<s32, irr::core::vector2d<s32> >;

	// Get the interface to the native Android activity.
	Android = (android_app*)(param.PrivateData);

	io::CAndroidAssetReader::Activity = Android->activity;
	io::CAndroidAssetFileArchive::Activity = Android->activity;

	// Set the private data so we can use it in any static callbacks.
	Android->userData = this;

	// Set the default command handler. This is a callback function that the Android
	// OS invokes to send the native activity messages.
	Android->onAppCmd = handleAndroidCommand;

	// Create a sensor manager to receive touch screen events from the java activity.
	SensorManager = ASensorManager_getInstance();
	SensorEventQueue = ASensorManager_createEventQueue(SensorManager, Android->looper, LOOPER_ID_USER, 0, 0);
	Android->onInputEvent = handleInput;

	// Create EGL manager.
	ContextManager = new video::CEGLManager();

	os::Printer::log("Waiting for Android activity window to be created.", ELL_DEBUG);

	do
	{
		s32 Events = 0;
		android_poll_source* Source = 0;

		while ((ALooper_pollAll(((Focused && !Paused) || !Initialized) ? 0 : -1, 0, &Events, (void**)&Source)) >= 0)
		{
			if(Source)
				Source->process(Android, Source);
		}
	}
	while(!Initialized);
}


CIrrDeviceAndroid::~CIrrDeviceAndroid()
{
	if (GUIEnvironment)
	{
		GUIEnvironment->drop();
		GUIEnvironment = 0;
	}

	if (SceneManager)
	{
		SceneManager->drop();
		SceneManager = 0;
	}

	if (VideoDriver)
	{
		VideoDriver->drop();
		VideoDriver = 0;
	}
}

bool CIrrDeviceAndroid::run()
{
	if (!Initialized)
		return false;

	os::Timer::tick();

	s32 Events = 0;
	android_poll_source* Source = 0;

	while ((ALooper_pollAll(((Focused && !Paused) || !Initialized) ? 0 : -1, 0, &Events, (void**)&Source)) >= 0)
	{
		if(Source)
			Source->process(Android, Source);

		if(!Initialized)
			break;
	}

	return Initialized;
}

void CIrrDeviceAndroid::yield()
{
	struct timespec ts = {0,1};
	nanosleep(&ts, NULL);	
}

void CIrrDeviceAndroid::sleep(u32 timeMs, bool pauseTimer)
{
	const bool wasStopped = Timer ? Timer->isStopped() : true;

	struct timespec ts;
	ts.tv_sec = (time_t) (timeMs / 1000);
	ts.tv_nsec = (long) (timeMs % 1000) * 1000000;

	if (pauseTimer && !wasStopped)
		Timer->stop();

	nanosleep(&ts, NULL);

	if (pauseTimer && !wasStopped)
		Timer->start();	
}

void CIrrDeviceAndroid::setWindowCaption(const wchar_t* text)
{
}

bool CIrrDeviceAndroid::present(video::IImage* surface, void* windowId, core::rect<s32>* srcClip)
{
	return true;
}

bool CIrrDeviceAndroid::isWindowActive() const
{
	return (Focused && !Paused);
}

bool CIrrDeviceAndroid::isWindowFocused() const
{
	return Focused;
}

bool CIrrDeviceAndroid::isWindowMinimized() const
{
	return !Focused;
}

void CIrrDeviceAndroid::closeDevice()
{
	ANativeActivity_finish(Android->activity);
}

void CIrrDeviceAndroid::setResizable(bool resize)
{
}

void CIrrDeviceAndroid::minimizeWindow()
{
}

void CIrrDeviceAndroid::maximizeWindow()
{
}

void CIrrDeviceAndroid::restoreWindow()
{
}

core::position2di CIrrDeviceAndroid::getWindowPosition()
{
	return core::position2di(0, 0);
}

E_DEVICE_TYPE CIrrDeviceAndroid::getType() const
{
	return EIDT_ANDROID;
}

void CIrrDeviceAndroid::handleAndroidCommand(android_app* app, int32_t cmd)
{
	CIrrDeviceAndroid* Device = (CIrrDeviceAndroid*)app->userData;

	switch (cmd)
	{
		case APP_CMD_SAVE_STATE:
			os::Printer::log("Android command APP_CMD_SAVE_STATE", ELL_DEBUG);
		break;
		case APP_CMD_INIT_WINDOW:
			os::Printer::log("Android command APP_CMD_INIT_WINDOW", ELL_DEBUG);
			Device->getExposedVideoData().OGLESAndroid.Window = app->window;

			if (Device->CreationParams.WindowSize.Width == 0 || Device->CreationParams.WindowSize.Height == 0)
			{
				Device->CreationParams.WindowSize.Width = ANativeWindow_getWidth(app->window);
				Device->CreationParams.WindowSize.Height = ANativeWindow_getHeight(app->window);
			}

			Device->getContextManager()->initialize(Device->CreationParams, Device->ExposedVideoData);
			Device->getContextManager()->generateSurface();
			Device->getContextManager()->generateContext();
			Device->getContextManager()->activateContext(Device->getContextManager()->getContext());

			if (!Device->Initialized)
			{
				io::CAndroidAssetFileArchive* Assets = new io::CAndroidAssetFileArchive(false, false);
				Assets->addDirectory("media");
				Device->FileSystem->addFileArchive(Assets);

				Device->createDriver();

				if (Device->VideoDriver)
					Device->createGUIAndScene();
			}
			Device->Initialized = true;
		break;
		case APP_CMD_TERM_WINDOW:
			os::Printer::log("Android command APP_CMD_TERM_WINDOW", ELL_DEBUG);
			Device->getContextManager()->destroySurface();
		break;
		case APP_CMD_GAINED_FOCUS:
			os::Printer::log("Android command APP_CMD_GAINED_FOCUS", ELL_DEBUG);
			Device->Focused = true;
		break;
		case APP_CMD_LOST_FOCUS:
			os::Printer::log("Android command APP_CMD_LOST_FOCUS", ELL_DEBUG);
			Device->Focused = false;
		break;
		case APP_CMD_DESTROY:
			os::Printer::log("Android command APP_CMD_DESTROY", ELL_DEBUG);
			Device->Initialized = false;
			break;
		case APP_CMD_PAUSE:
			os::Printer::log("Android command APP_CMD_PAUSE", ELL_DEBUG);
			Device->Paused = true;
			break;
		case APP_CMD_STOP:
			os::Printer::log("Android command APP_CMD_STOP", ELL_DEBUG);
			break;
		case APP_CMD_RESUME:
			os::Printer::log("Android command APP_CMD_RESUME", ELL_DEBUG);
			Device->Paused = false;
			break;
		default:
			break;
	}
}

s32 CIrrDeviceAndroid::handleInput(android_app* app, AInputEvent* androidEvent)
{
	CIrrDeviceAndroid* Device = (CIrrDeviceAndroid*)app->userData;
	s32 Status = 0;

	if (AInputEvent_getType(androidEvent) == AINPUT_EVENT_TYPE_MOTION)
	{
		SEvent Event;
		s32 PointerCount = AMotionEvent_getPointerCount(androidEvent);
		s32 AndroidEventAction = AMotionEvent_getAction(androidEvent);
		s32 EventAction =  AndroidEventAction & AMOTION_EVENT_ACTION_MASK;
		s32 ChangedPointerID = (AndroidEventAction & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

		bool MultiTouchEvent = true;
		bool Touched = false;

		switch (EventAction)
		{
		case AMOTION_EVENT_ACTION_DOWN:
		case AMOTION_EVENT_ACTION_POINTER_DOWN:
			Event.MultiTouchInput.Event = EMTIE_PRESSED_DOWN;
			Touched = true;
			break;
		case AMOTION_EVENT_ACTION_MOVE:
			Event.MultiTouchInput.Event = EMTIE_MOVED;
			Touched = true;
			break;
		case AMOTION_EVENT_ACTION_UP:
		case AMOTION_EVENT_ACTION_POINTER_UP:
			Event.MultiTouchInput.Event = EMTIE_LEFT_UP;
			break;
		default:
			MultiTouchEvent = false;
			break;
		}

		if (MultiTouchEvent)
		{
			Event.EventType = EET_MULTI_TOUCH_EVENT;
			Event.MultiTouchInput.clear();
			Event.MultiTouchInput.PointerCount = PointerCount;

			core::map<s32, irr::core::vector2d<s32> > *newMotionData = new core::map<s32, irr::core::vector2d<s32> >;

			for (s32 i = 0; i < PointerCount; ++i)
			{
				if (i >= NUMBER_OF_MULTI_TOUCHES)
					break;

				s32 x = AMotionEvent_getX(androidEvent, i);
				s32 y = AMotionEvent_getY(androidEvent, i);
				Event.MultiTouchInput.X[i] = x;
				Event.MultiTouchInput.Y[i] = y;

				s32 id = AMotionEvent_getPointerId(androidEvent, i);
				Event.MultiTouchInput.ID[i] = id;

				core::map<s32, irr::core::vector2d<s32> >::Node *previousMotion;
				if ((previousMotion = Device->previousMotionData->find(id))) {
					Event.MultiTouchInput.PrevX[i] = previousMotion->getValue().X;
					Event.MultiTouchInput.PrevY[i] = previousMotion->getValue().Y;
				} else {
					Event.MultiTouchInput.PrevX[i] = x;
					Event.MultiTouchInput.PrevY[i] = y;
				}

				if ((Event.MultiTouchInput.Touched[i] = Touched || (ChangedPointerID != id)))
					(*newMotionData)[id] = core::vector2d<s32>(x, y);
			}
			delete Device->previousMotionData;
			Device->previousMotionData = newMotionData;

			Device->postEventFromUser(Event);

			if (PointerCount > 0) {
				SEvent MouseEvent = {};
				MouseEvent.EventType = EET_MOUSE_INPUT_EVENT;
				MouseEvent.MouseInput.X = Event.MultiTouchInput.X[0];
				MouseEvent.MouseInput.Y = Event.MultiTouchInput.Y[0];

				switch (EventAction) {
				case AMOTION_EVENT_ACTION_DOWN:
					MouseEvent.MouseInput.Event = EMIE_LMOUSE_PRESSED_DOWN;
					MouseEvent.MouseInput.ButtonStates = EMBSM_LEFT;
					break;
				case AMOTION_EVENT_ACTION_MOVE:
					MouseEvent.MouseInput.Event = EMIE_MOUSE_MOVED;
					MouseEvent.MouseInput.ButtonStates = EMBSM_LEFT;
					break;
				case AMOTION_EVENT_ACTION_UP:
					MouseEvent.MouseInput.Event = EMIE_LMOUSE_LEFT_UP;
					break;
				}

				Device->postEventFromUser(MouseEvent);
			}

			Status = 1;
		}
	}

	if( AInputEvent_getType( androidEvent ) == AINPUT_EVENT_TYPE_KEY )
	{
		SEvent irrEvent;
		irrEvent.EventType = EET_KEY_INPUT_EVENT;
		s32 action = AKeyEvent_getAction(androidEvent);
		s32 meta = AKeyEvent_getMetaState(androidEvent);
		irrEvent.KeyInput.Char = 0;
		irrEvent.KeyInput.Control = false;//TODO: Control
		irrEvent.KeyInput.Shift = (meta & AMETA_SHIFT_ON)!=0;
		irrEvent.KeyInput.PressedDown = (action==AKEY_EVENT_ACTION_DOWN);//AKEY_EVENT_ACTION_UP
		s32 key = AKeyEvent_getKeyCode(androidEvent);
/*TODO:
	AKEYCODE_UNKNOWN		 = 0,
	AKEYCODE_SOFT_LEFT	   = 1,
	AKEYCODE_SOFT_RIGHT	  = 2,
	AKEYCODE_CALL			= 5,
	AKEYCODE_ENDCALL		 = 6,
	AKEYCODE_DPAD_CENTER	 = 23,
	AKEYCODE_VOLUME_UP	   = 24,
	AKEYCODE_VOLUME_DOWN	 = 25,
	AKEYCODE_POWER		   = 26,
	AKEYCODE_CAMERA		  = 27,
	AKEYCODE_CLEAR		   = 28,
	AKEYCODE_SYM			 = 63,
	AKEYCODE_EXPLORER		= 64,
	AKEYCODE_ENVELOPE		= 65,
	AKEYCODE_HEADSETHOOK	 = 79,
	AKEYCODE_FOCUS		   = 80,   // *Camera* focus
	AKEYCODE_NOTIFICATION	= 83,
	AKEYCODE_SEARCH		  = 84,
	AKEYCODE_MEDIA_STOP	  = 86,
	AKEYCODE_MEDIA_NEXT	  = 87,
	AKEYCODE_MEDIA_PREVIOUS  = 88,
	AKEYCODE_MEDIA_REWIND	= 89,
	AKEYCODE_MEDIA_FAST_FORWARD = 90,
	AKEYCODE_MUTE			= 91,
	AKEYCODE_PICTSYMBOLS	 = 94,
	AKEYCODE_SWITCH_CHARSET  = 95,
	AKEYCODE_BUTTON_A		= 96,
	AKEYCODE_BUTTON_B		= 97,
	AKEYCODE_BUTTON_C		= 98,
	AKEYCODE_BUTTON_X		= 99,
	AKEYCODE_BUTTON_Y		= 100,
	AKEYCODE_BUTTON_Z		= 101,
	AKEYCODE_BUTTON_L1	   = 102,
	AKEYCODE_BUTTON_R1	   = 103,
	AKEYCODE_BUTTON_L2	   = 104,
	AKEYCODE_BUTTON_R2	   = 105,
	AKEYCODE_BUTTON_THUMBL   = 106,
	AKEYCODE_BUTTON_THUMBR   = 107,
	AKEYCODE_BUTTON_START	= 108,
	AKEYCODE_BUTTON_SELECT   = 109,
	AKEYCODE_BUTTON_MODE	 = 110,
	AKEYCODE_NUM			 = 78,
*/
		if(key==AKEYCODE_HOME){
			irrEvent.KeyInput.Key = KEY_HOME;
		}else if(key==AKEYCODE_BACK){//the back button will not exit the app anymore, KEY_CANCEL makes sense to me
			irrEvent.KeyInput.Key = KEY_CANCEL;//KEY_BACK;
		}else if(key>=AKEYCODE_0 && key<=AKEYCODE_9){
			irrEvent.KeyInput.Key = (EKEY_CODE)(key-AKEYCODE_0+KEY_KEY_0);
			if(!irrEvent.KeyInput.Shift){
				irrEvent.KeyInput.Char = (wchar_t)(key-AKEYCODE_0)+L'0';
			}else{
				if(key == AKEYCODE_2){
					irrEvent.KeyInput.Char = L'@';
				}else if(key == AKEYCODE_1){
					irrEvent.KeyInput.Char = L'!';
				}else if(key == AKEYCODE_3){
					irrEvent.KeyInput.Char = L'#';
				}else if(key == AKEYCODE_4){
					irrEvent.KeyInput.Char = L'$';
				}else if(key == AKEYCODE_5){
					irrEvent.KeyInput.Char = L'%';
				}else if(key == AKEYCODE_6){
					irrEvent.KeyInput.Char = L'^';
				}else if(key == AKEYCODE_7){
					irrEvent.KeyInput.Char = L'&';
				}else if(key == AKEYCODE_8){
					irrEvent.KeyInput.Char = L'*';
				}else if(key == AKEYCODE_9){
					irrEvent.KeyInput.Char = L'(';
				}else if(key == AKEYCODE_0){
					irrEvent.KeyInput.Char = L')';
				}
			}
		}else if(key==AKEYCODE_STAR){
			irrEvent.KeyInput.Key = KEY_KEY_8 ;//US Keyboard
			irrEvent.KeyInput.Char = L'*';
		}else if(key==AKEYCODE_POUND){
			irrEvent.KeyInput.Key = KEY_KEY_3;//British Keyboard
			irrEvent.KeyInput.Char = L'£';
		}else if(key==AKEYCODE_DPAD_UP){
			irrEvent.KeyInput.Key = KEY_UP;
		}else if(key==AKEYCODE_DPAD_DOWN){
			irrEvent.KeyInput.Key = KEY_DOWN;
		}else if(key==AKEYCODE_DPAD_LEFT){
			irrEvent.KeyInput.Key = KEY_LEFT;
		}else if(key==AKEYCODE_DPAD_RIGHT){
			irrEvent.KeyInput.Key = KEY_RIGHT;
		}else if(key>=AKEYCODE_A && key<=AKEYCODE_Z){
			irrEvent.KeyInput.Key = (EKEY_CODE)(key-AKEYCODE_A+KEY_KEY_A);
			if(!irrEvent.KeyInput.Shift){
				irrEvent.KeyInput.Char = (wchar_t)(key-AKEYCODE_A)+L'a';
			}else{
				irrEvent.KeyInput.Char = (wchar_t)(key-AKEYCODE_A)+L'A';
			}
		}else if(key==AKEYCODE_COMMA){
			irrEvent.KeyInput.Key = KEY_COMMA;
			if(irrEvent.KeyInput.Shift){
				irrEvent.KeyInput.Char = L'<';
			}else{
				irrEvent.KeyInput.Char = L',';
			}
		}else if(key==AKEYCODE_PERIOD){
			irrEvent.KeyInput.Key = KEY_PERIOD;
			if(irrEvent.KeyInput.Shift){
				irrEvent.KeyInput.Char = L'>';
			}else{
				irrEvent.KeyInput.Char = L'.';
			}
		}else if(key==AKEYCODE_ALT_LEFT){
			irrEvent.KeyInput.Key = KEY_LMENU;
		}else if(key==AKEYCODE_ALT_RIGHT){
			irrEvent.KeyInput.Key = KEY_RMENU;
		}else if(key==AKEYCODE_SHIFT_LEFT){
			irrEvent.KeyInput.Key = KEY_LSHIFT;
		}else if(key==AKEYCODE_SHIFT_RIGHT){
			irrEvent.KeyInput.Key = KEY_RSHIFT;
		}else if(key==AKEYCODE_TAB){
			irrEvent.KeyInput.Key = KEY_TAB;
			irrEvent.KeyInput.Char = L'\t';
		}else if(key==AKEYCODE_SPACE){
			irrEvent.KeyInput.Key = KEY_SPACE;
			irrEvent.KeyInput.Char = L' ';
		}else if(key==AKEYCODE_ENTER){
			irrEvent.KeyInput.Key = KEY_RETURN;
			irrEvent.KeyInput.Char = L'\n';
		}else if(key==AKEYCODE_DEL){
			irrEvent.KeyInput.Key = KEY_BACK;
		}else if(key==AKEYCODE_MINUS){
			irrEvent.KeyInput.Key = KEY_MINUS;
			if(irrEvent.KeyInput.Shift){
				irrEvent.KeyInput.Char = L'_';
			}else{
				irrEvent.KeyInput.Char = L'-';
			}
		}else if(key==AKEYCODE_EQUALS){
			irrEvent.KeyInput.Key = KEY_PLUS;//US Keyboard
			if(irrEvent.KeyInput.Shift){
				irrEvent.KeyInput.Char = L'+';
			}else{
				irrEvent.KeyInput.Char = L'=';
			}
		}else if(key==AKEYCODE_LEFT_BRACKET){
			irrEvent.KeyInput.Key = KEY_OEM_4;//US Keyboard
			if(irrEvent.KeyInput.Shift){
				irrEvent.KeyInput.Char = L'{';
			}else{
				irrEvent.KeyInput.Char = L'[';
			}
		}else if(key==AKEYCODE_RIGHT_BRACKET){
			irrEvent.KeyInput.Key = KEY_OEM_6;//US Keyboard
			if(irrEvent.KeyInput.Shift){
				irrEvent.KeyInput.Char = L'}';
			}else{
				irrEvent.KeyInput.Char = L']';
			}
		}else if(key==AKEYCODE_BACKSLASH){
			irrEvent.KeyInput.Key = KEY_OEM_5;//US Keyboard
			if(irrEvent.KeyInput.Shift){
				irrEvent.KeyInput.Char = L'|';
			}else{
				irrEvent.KeyInput.Char = L'\\';
			}
		}else if(key==AKEYCODE_SEMICOLON){
			irrEvent.KeyInput.Key = KEY_OEM_1;//US Keyboard
			if(irrEvent.KeyInput.Shift){
				irrEvent.KeyInput.Char = L':';
			}else{
				irrEvent.KeyInput.Char = L';';
			}
		}else if(key==AKEYCODE_APOSTROPHE){
			irrEvent.KeyInput.Key = KEY_OEM_7;//US Keyboard
			if(irrEvent.KeyInput.Shift){
				irrEvent.KeyInput.Char = L'\"';
			}else{
				irrEvent.KeyInput.Char = L'\'';
			}
		}else if(key==AKEYCODE_SLASH){
			irrEvent.KeyInput.Key = KEY_OEM_2;//US Keyboard
			if(irrEvent.KeyInput.Shift){
				irrEvent.KeyInput.Char = L'?';
			}else{
				irrEvent.KeyInput.Char = L'/';
			}
		}else if(key==AKEYCODE_AT){
			irrEvent.KeyInput.Key = KEY_KEY_2;//US Keyboard
			irrEvent.KeyInput.Char = L'@';
		}else if(key==AKEYCODE_PLUS){
			irrEvent.KeyInput.Key = KEY_PLUS;
			irrEvent.KeyInput.Char = L'+';
		}else if(key==AKEYCODE_MENU){//Menubutton of the unhidable toolbar
			irrEvent.KeyInput.Key = KEY_MENU;
		}else if(key==AKEYCODE_MEDIA_PLAY_PAUSE){
			irrEvent.KeyInput.Key = KEY_PLAY;//hmmm
		}else if(key==AKEYCODE_PAGE_UP){
			irrEvent.KeyInput.Key = KEY_PRIOR;
		}else if(key==AKEYCODE_PAGE_DOWN){
			irrEvent.KeyInput.Key = KEY_NEXT;
		}else if(key==AKEYCODE_GRAVE){
			irrEvent.KeyInput.Key = KEY_OEM_3;//US Keyboard
			if(irrEvent.KeyInput.Shift){
				irrEvent.KeyInput.Char = L'~';
			}else{
				irrEvent.KeyInput.Char = L'`';
			}
		}else{
			//__android_log_print(ANDROID_LOG_ERROR, "Unhandled Key", "Code: %i Shift: %i\n", key, (int)irrEvent.KeyInput.Shift);
			return 0;
		}
 
		//__android_log_print(ANDROID_LOG_ERROR, "Key", "Code: %i Shift: %i\n", key, (int)irrEvent.KeyInput.Shift);
		Device->postEventFromUser(irrEvent);
		return 1;
	}

	return Status;
}

void CIrrDeviceAndroid::createDriver()
{
	switch(CreationParams.DriverType)
	{
	case video::EDT_OGLES1:
#ifdef _IRR_COMPILE_WITH_OGLES1_
		VideoDriver = video::createOGLES1Driver(CreationParams, FileSystem, ContextManager);
#else
		os::Printer::log("No OpenGL ES 1.0 support compiled in.", ELL_ERROR);
#endif
		break;
	case video::EDT_OGLES2:
#ifdef _IRR_COMPILE_WITH_OGLES2_
		VideoDriver = video::createOGLES2Driver(CreationParams, FileSystem, ContextManager);
#else
		os::Printer::log("No OpenGL ES 2.0 support compiled in.", ELL_ERROR);
#endif
		break;
	case video::EDT_NULL:
		VideoDriver = video::createNullDriver(FileSystem, CreationParams.WindowSize);
		break;
	case video::EDT_SOFTWARE:
	case video::EDT_BURNINGSVIDEO:
	case video::EDT_OPENGL:
	case video::EDT_DIRECT3D8:
	case video::EDT_DIRECT3D9:
		os::Printer::log("This driver is not available in Android. Try OpenGL ES 1.0 or ES 2.0.", ELL_ERROR);
		break;
	default:
		os::Printer::log("Unable to create video driver of unknown type.", ELL_ERROR);
		break;
	}
}

video::SExposedVideoData& CIrrDeviceAndroid::getExposedVideoData()
{
	return ExposedVideoData;
}

} // end namespace irr

#endif

