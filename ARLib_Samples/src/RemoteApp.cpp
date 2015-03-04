#include "RemoteApp.h"
#include "NatNetTypes.h"

RemoteApp::RemoteApp(bool showDebugWindow)
	: mRoot(nullptr)
	, mKeyboard(nullptr)
	, mMouse(nullptr)
	, mScene(nullptr)
	, mShutdown(false)
	, mWindow(nullptr)
	, mSmallWindow(nullptr)
	, mRiftAvailable(false)
	, mRift(nullptr)
	, mRenderTarget(nullptr)
	, mSmallRenderTarget(nullptr)
	, mTracker(nullptr)
    , mDebugDrawer(nullptr)
    , mDynamicsWorld(nullptr)
    , mGroundShape(nullptr)
{
	std::cout << "Creating Ogre application:" << std::endl;

	// check if Oculus Rift (ID 0) is available:
	ARLib::Rift::init();
	mRiftAvailable = ARLib::Rift::available(0);
	showDebugWindow = false;
	if (!mRiftAvailable) // at least show the debug window if it is not available
		showDebugWindow = true;

	initOgre(showDebugWindow);
    initBullet(showDebugWindow); //enable debug drawer
	initOIS();
	initRift();
	initTracking(showDebugWindow);

	Sleep(2000); // needed if the rift tracking camera is connected... too many concurrent usb initializations maybe?
	
	mVideoPlayerLeft  = new ARLib::VideoPlayer(0, "../../media/calib_results_CAM1.txt", 3.0f, "../../media/homography_CAM1.txt" );
	mVideoPlayerRight = new ARLib::VideoPlayer(1, "../../media/calib_results_CAM2.txt", 3.0f, "../../media/homography_CAM2.txt" );

    mScene = new RemoteScene(mRift, mTracker, mRoot,
						mWindow, mSmallWindow, mSceneMgr, 
						mDynamicsWorld, mMouse, mKeyboard,
						mVideoPlayerLeft, mVideoPlayerRight);
	mRoot->startRendering();
}

RemoteApp::~RemoteApp()
{
	std::cout << "Deleting Ogre application." << std::endl;
	delete mRenderTarget;
	delete mSmallRenderTarget;
	quitTracking();
	quitRift();
	std::cout << "Deleting Scene:" << std::endl;
	delete mScene;
	std::cout << "Closing OIS:" << std::endl;
	quitOIS();
    std::cout << "Closing Bullet:" << std::endl;
    quitBullet();
	std::cout << "Closing Ogre:" << std::endl;
	quitOgre();

	delete mVideoPlayerLeft;
	delete mVideoPlayerRight;
}

void RemoteApp::initOgre(bool showDebugWindow)
{
	Ogre::ConfigFile cf;
	mRoot = new Ogre::Root("plugins.cfg");
	cf.load("../../media/resources.cfg");
	mRoot->addFrameListener(this);
 
    // add resources
    Ogre::ConfigFile::SectionIterator seci = cf.getSectionIterator();
    Ogre::String secName, typeName, archName;
    while (seci.hasMoreElements())
    {
        secName = seci.peekNextKey(); Ogre::ConfigFile::SettingsMultiMap *settings = seci.getNext();
        Ogre::ConfigFile::SettingsMultiMap::iterator i;
        for (i = settings->begin(); i != settings->end(); ++i)
            Ogre::ResourceGroupManager::getSingleton().addResourceLocation(i->second, i->first, secName);
	}

	// initialize render system
	Ogre::RenderSystem* pRS = mRoot->getRenderSystemByName("OpenGL Rendering Subsystem");
	Ogre::ConfigOptionMap cfgMap = pRS->getConfigOptions();
	cfgMap["Full Screen"].currentValue = "No";
	cfgMap["VSync"].currentValue = "Yes";
	cfgMap["FSAA"].currentValue = "8";
	cfgMap["Video Mode"].currentValue = "1200 x 800"; // will be overriden
	for(Ogre::ConfigOptionMap::iterator iter = cfgMap.begin(); iter != cfgMap.end(); iter++)
		pRS->setConfigOption(iter->first, iter->second.currentValue);
	mRoot->setRenderSystem(pRS);
	mRoot->initialise(false, "ARLib Example");

	// create windows:
	if (mRiftAvailable)
	{
		Ogre::NameValuePairList miscParams;
		miscParams["monitorIndex"] = Ogre::StringConverter::toString(1);
		miscParams["border"] = "none";
		mWindow = mRoot->createRenderWindow("ARLib Example", 1920, 1080, true, &miscParams);
	}
	if (showDebugWindow)
	{
		Ogre::NameValuePairList miscParamsSmall;
		miscParamsSmall["monitorIndex"] = Ogre::StringConverter::toString(0);
		mSmallWindow = mRoot->createRenderWindow("ARLib Example (debug window)", 1920 / 2, 1080 / 2, false, &miscParamsSmall);
		if (!mWindow)
			mWindow = mSmallWindow;
	}

	Ogre::ResourceGroupManager::getSingleton().initialiseAllResourceGroups();
}
void RemoteApp::quitOgre()
{
	delete mRoot;
}

void RemoteApp::initBullet(bool enableDebugDrawing){
    mSceneMgr = mRoot->createSceneManager(Ogre::ST_GENERIC);
    mDynamicsWorld = new OgreBulletDynamics::DynamicsWorld(mSceneMgr, Ogre::AxisAlignedBox(-10,-10,-10,10,10,10), Ogre::Vector3(0,0,0), true, true, 1000);
   
    if(enableDebugDrawing == true){
        mDebugDrawer = new OgreBulletCollisions::DebugDrawer();
        mDebugDrawer->setDrawWireframe(true);

        mDynamicsWorld->setDebugDrawer(mDebugDrawer);
        mDynamicsWorld->setShowDebugShapes(true);

        Ogre::SceneNode *node = mSceneMgr->getRootSceneNode()->createChildSceneNode("debugDrawer", Ogre::Vector3::ZERO);
        node->attachObject(static_cast<Ogre::SimpleRenderable *>(mDebugDrawer));
    }

}

void RemoteApp::quitBullet(){
    delete mGroundShape;
    delete mDebugDrawer;
    delete mDynamicsWorld;
}

void RemoteApp::initOIS()
{
	OIS::ParamList pl;
    size_t windowHnd = 0;
    std::ostringstream windowHndStr;
 
    // tell OIS about the Ogre window
    mWindow->getCustomAttribute("WINDOW", &windowHnd);
    windowHndStr << windowHnd;
    pl.insert(std::make_pair(std::string("WINDOW"), windowHndStr.str()));

	// setup the manager, keyboard and mouse to handle input
    OIS::InputManager* inputManager = OIS::InputManager::createInputSystem(pl);
    mKeyboard = static_cast<OIS::Keyboard*>(inputManager->createInputObject(OIS::OISKeyboard, true));
    mMouse = static_cast<OIS::Mouse*>(inputManager->createInputObject(OIS::OISMouse, true));
 
    // tell OIS about the window's dimensions
    unsigned int width, height, depth;
    int top, left;
    mWindow->getMetrics(width, height, depth, left, top);
    const OIS::MouseState &ms = mMouse->getMouseState();
    ms.width = width;
    ms.height = height;

	// make sure OIS calls callbacks (keyPressed, mouseMoved etc) of this class:
	mKeyboard->setEventCallback(this);
	mMouse->setEventCallback(this);
}
void RemoteApp::quitOIS()
{
	delete mKeyboard;
	delete mMouse;
}

void RemoteApp::initRift()
{
	// try to initialize the Oculus Rift (ID 0):
	if (mRiftAvailable)
	{
		try {
			mRift = new ARLib::Rift(0);
		} catch(const char* e) {
			std::cout << ">> " << e << std::endl;
			mRiftAvailable = false;
			mRift = nullptr;
		}
	}
}
void RemoteApp::quitRift()
{
	std::cout << "Shutting down Oculus Rifts:" << std::endl;
	delete mRift;
	ARLib::Rift::shutdown();
}

ARLib::TRACKING_ERROR_CODE RemoteApp::initTracking(ARLib::TRACKING_METHOD method, bool enableDebugLog)
{
	mTracker = new ARLib::TrackingManager(method, 100, enableDebugLog);
	mTracker->setNatNetConnectionType(ConnectionType_Multicast);
	mTracker->setNatNetClientIP("128.176.181.34"); 
	mTracker->setNatNetServerIP("128.176.181.34");
	mTracker->setFrameEvaluationMethod(ARLib::FRAME_FLOOR);

	ARLib::TRACKING_ERROR_CODE error = mTracker->initialize();
	mTrackingAvailable = (error == ARLib::ARLIB_TRACKING_OK);

	if(!mTrackingAvailable)
	{
		mTracker->uninitialize();
		delete mTracker;
		mTracker = nullptr;
	}
	
	return error;
}
		
void RemoteApp::initTracking(bool enableDebugLog)
{
	ARLib::TRACKING_ERROR_CODE error;

	error = initTracking(ARLib::ARLIB_NATNET | ARLib::ARLIB_RIFT, enableDebugLog); // Try both first
	if (error == ARLib::ARLIB_TRACKING_OK)
		std::cout << "NatNet + Rift Tracking initialized." << std::endl;

	if (error == ARLib::ARLIB_TRACKING_NATNET_ERROR)
	{
		error = initTracking(ARLib::ARLIB_RIFT, enableDebugLog); // Rift Tracking only
		if (error == ARLib::ARLIB_TRACKING_OK)
			std::cout << "Rift Tracking initialized." << std::endl;
	}
	else if (error == ARLib::ARLIB_TRACKING_RIFT_ERROR)
	{
		error = initTracking(ARLib::ARLIB_NATNET, enableDebugLog); // NatNet Tracking only
		if (error == ARLib::ARLIB_TRACKING_OK)
			std::cout << "NatNet Tracking initialized." << std::endl;
	}

	if (error != ARLib::ARLIB_TRACKING_OK)
		std::cout << "Failed to Initialize Tracking Manager. ErrorCode:" << error << std::endl;
}
		
void RemoteApp::quitTracking()
{
	std::cout << "Shutting down Tracking System" << std::endl;
	mTracker->uninitialize();
	delete mTracker;
}

bool RemoteApp::frameRenderingQueued(const Ogre::FrameEvent& evt) 
{
	if (mShutdown) return false;

	// update the standard input devices
	mKeyboard->capture();
	mMouse->capture();

    if (mTrackingAvailable)
		mTracker->update(); //right place?

    mDynamicsWorld->stepSimulation(evt.timeSinceLastFrame, 5);
    mScene->update(evt.timeSinceLastFrame);
	return true; 
}

bool RemoteApp::keyPressed(const OIS::KeyEvent& e)
{
	mScene->keyPressed(e);

	if(e.key == OIS::KC_ESCAPE)
		quit();
	if(e.key == OIS::KC_P)
		mWindow->writeContentsToFile("Screenshot.png");

	return true;
}
bool RemoteApp::keyReleased(const OIS::KeyEvent& e)
{
	mScene->keyReleased(e);
	return true;
}
bool RemoteApp::mouseMoved(const OIS::MouseEvent& e)
{
	mScene->mouseMoved(e);
	return true;
}
bool RemoteApp::mousePressed(const OIS::MouseEvent& e, OIS::MouseButtonID id)
{
	mScene->mouseReleased(e, id);
	return true;
}
bool RemoteApp::mouseReleased(const OIS::MouseEvent& e, OIS::MouseButtonID id)
{
	mScene->mouseReleased(e, id);
	return true;
}

void RemoteApp::quit()
{
	std::cout << "QUIT." << std::endl;
	mShutdown = true;
}