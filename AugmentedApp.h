//Ogre3d includes
#include <OgreRoot.h>
#include <OISEvents.h>
#include <OISInputManager.h>
#include <OISKeyboard.h>
#include <OgreWindowEventUtilities.h>
#include "OgreHardwarePixelBuffer.h"
#include <OgrePixelFormat.h>
#include <OgreEntity.h>
#include <OgreAnimation.h>
#include <OgreManualObject.h>

//OpenNi includes
#include <XnCppWrapper.h>
#include <XnV3DVector.h>
#include <stdio.h>

#define WIDTH	640
#define	HEIGHT	480

using namespace xn;
using namespace Ogre;

//Global Variables
Context context;
DepthGenerator Xn_depth;
ImageGenerator Xn_image;
UserGenerator Xn_user;
XnStatus nRetVal = XN_STATUS_OK;
ImageMetaData rgb_md;
XnBool needPose = FALSE;
XnChar strPose[20] = "";
XnBool mirrored = FALSE;
class AugmentedApp : public Ogre::WindowEventListener, public Ogre::FrameListener
{
public:
    AugmentedApp(void);
    virtual ~AugmentedApp(void);
    bool go(void);
protected:
	//virtual void windowResized(Ogre::RenderWindow* rw);
	virtual void windowClosed(Ogre::RenderWindow* rw);
	virtual bool frameRenderingQueued(const Ogre::FrameEvent& evt);
private:
	//Augmeted App Members
    Root* mRoot;
    String mPluginsCfg;
	String mResourcesCfg;
	RenderWindow* mWindow;
	SceneManager* mSceneMgr;
	Camera* mCamera;
	TexturePtr mBackground;
	Entity* mCharacter;
	XnUserID m_precandidateID;
	XnUserID m_candidateID;
	AnimationState* mAnims[13];
	ManualObject* mpcl;

	// OIS Input devices
	OIS::InputManager* mInputManager;
	OIS::Keyboard* mKeyboard;
	
	//OpenNI Functions
	// Callback: New user was detected
	static void XN_CALLBACK_TYPE cb_newUser(xn::UserGenerator& generator, XnUserID nId, void* pCookie){
		AugmentedApp* This = (AugmentedApp*)pCookie;
		Xn_user.GetPoseDetectionCap().StartPoseDetection(strPose, nId);
		if (This->m_precandidateID == 0)
			This->m_precandidateID = nId;
	}

	// Callback: An existing user was lost
	static void XN_CALLBACK_TYPE cb_lostUser(xn::UserGenerator& generator, XnUserID nId, void* pCookie){
		AugmentedApp* This = (AugmentedApp*)pCookie;
		if(This->m_candidateID == nId )
		{
			This->m_candidateID = 0;
			This->resetBonesToInitialState();
		}
		if(This->m_precandidateID == nId )
			This->m_precandidateID = 0;
		
	}

	// Callback: Detected a pose
	static void XN_CALLBACK_TYPE cb_poseDetected(xn::PoseDetectionCapability& capability, const XnChar* strPose, XnUserID nId, void* pCookie){
		AugmentedApp* This = (AugmentedApp*)pCookie;
		// If we dont have an active candidate
		if(This->m_candidateID == 0)
		{
			This->m_candidateID = nId;
			Xn_user.GetPoseDetectionCap().StopPoseDetection(nId);
			Xn_user.GetSkeletonCap().RequestCalibration(nId, TRUE);
		}		
	}

	// Callback: Started calibration
	static void XN_CALLBACK_TYPE cb_calibrationStart(xn::SkeletonCapability& capability, XnUserID nId, void* pCookie){
	}

	// Callback: Finished calibration
	static void XN_CALLBACK_TYPE cb_calibrationEnd(xn::SkeletonCapability& skeleton, XnUserID nId, XnBool bSuccess, void* pCookie){
		AugmentedApp* This = (AugmentedApp*)pCookie;
		if (bSuccess){
			// Calibration succeeded
			skeleton.StartTracking(nId);
			// save torso position
			//Setup Character
			This->setupCharacter();
			//Setup Animation
			This->setupAnimations();
			/*
			XnSkeletonJointPosition torsoPos;
			skeleton.GetSkeletonJointPosition(nId, XN_SKEL_TORSO, torsoPos);
			This->m_origTorsoPos.x = -torsoPos.position.X;
			This->m_origTorsoPos.y = torsoPos.position.Y;
			This->m_origTorsoPos.z = -torsoPos.position.Z;
			*/
		}
		else {
			// Calibration failed
			This->m_candidateID = 0;
			Xn_user.GetPoseDetectionCap().StartPoseDetection(strPose, nId);
			}
	}

	void usergenerator_init(){
	//  Create a UserGenerator node
	nRetVal = Xn_user.Create(context);
	if (nRetVal != XN_STATUS_OK){
			printf("Failed to create user generator: %s\n", xnGetStatusString(nRetVal));
		}
	XnCallbackHandle hUser, hCalibration, hPose;
	//Register User Callbacks
	Xn_user.RegisterUserCallbacks(AugmentedApp::cb_newUser, AugmentedApp::cb_lostUser, this, hUser);
	//Register Skeleton Callbacks
	Xn_user.GetSkeletonCap().RegisterCalibrationCallbacks(AugmentedApp::cb_calibrationStart,AugmentedApp::cb_calibrationEnd, this, hCalibration);
	//Register Calibration Pose Callbacks
	if (Xn_user.GetSkeletonCap().NeedPoseForCalibration()){
		needPose = TRUE;
		Xn_user.GetPoseDetectionCap().RegisterToPoseCallbacks(AugmentedApp::cb_poseDetected, NULL, this, hPose);
		Xn_user.GetSkeletonCap().GetCalibrationPose(strPose);
		}
	Xn_user.GetSkeletonCap().SetSkeletonProfile(XN_SKEL_PROFILE_ALL);
	}

	void rgbd_init(){
	/// Initialize context object
	nRetVal = context.Init();
	if (nRetVal != XN_STATUS_OK){
			printf("Failed to initialize context: %s\n", xnGetStatusString(nRetVal));
		}

	/// Configure output
	XnMapOutputMode outputMode;
	outputMode.nXRes = WIDTH;
	outputMode.nYRes = HEIGHT;
	outputMode.nFPS = 30;

	/// Create a DepthGenerator node
	nRetVal = Xn_depth.Create(context);
	if (nRetVal != XN_STATUS_OK){
			printf("Failed to create depth generator: %s\n", xnGetStatusString(nRetVal));
		}
	Xn_depth.SetMapOutputMode(outputMode);
	Xn_depth.GetMirrorCap().SetMirror(mirrored);
	/// Create a ImageGenerator node
	nRetVal = Xn_image.Create(context);
	if (nRetVal != XN_STATUS_OK){
			printf("Failed to create image generator: %s\n", xnGetStatusString(nRetVal));
		}
	Xn_image.SetMapOutputMode(outputMode);
	Xn_image.GetMirrorCap().SetMirror(mirrored);
	// Initializes a UserGenerator node
	usergenerator_init();
	/// Make it start generating data
	nRetVal = context.StartGeneratingAll();
	if (nRetVal != XN_STATUS_OK){
			printf("Failed generating data: %s\n", xnGetStatusString(nRetVal));
		}
    
	/// Set the view point of the DepthGenerator to match the ImageGenerator
	nRetVal = Xn_depth.GetAlternativeViewPointCap().SetViewPoint(Xn_image);
	if (nRetVal != XN_STATUS_OK){
			printf("Failed to match Depth and RGB points of view: %s\n", xnGetStatusString(nRetVal));
		}
	m_candidateID=0;
	m_precandidateID=0;
	}
	
	void resetBonesToInitialState()
	{
		Skeleton* skel = mCharacter->getSkeleton();
		skel->getBone("Ulna.L")->resetToInitialState();
		skel->getBone("Ulna.R")->resetToInitialState();
		skel->getBone("Humerus.L")->resetToInitialState();
		skel->getBone("Humerus.R")->resetToInitialState();
		skel->getBone("Stomach")->resetToInitialState();
		skel->getBone("Chest")->resetToInitialState();
		skel->getBone("Thigh.L")->resetToInitialState();
		skel->getBone("Thigh.R")->resetToInitialState();
		skel->getBone("Calf.L")->resetToInitialState();
		skel->getBone("Calf.R")->resetToInitialState();
		skel->getBone("Root")->resetToInitialState();
	}

	void transformBone(const Ogre::String& modelBoneName, XnSkeletonJoint skelJoint, bool flip=false)
	{
		// Get the model skeleton bone info
		Skeleton* skel = mCharacter->getSkeleton();
		Bone* bone = skel->getBone(modelBoneName);
		Quaternion qI = bone->getInitialOrientation();
		Quaternion newQ = Quaternion::IDENTITY;

		// Get the openNI bone info
		SkeletonCapability pUserSkel = Xn_user.GetSkeletonCap();		
		XnSkeletonJointOrientation jointOri;
		pUserSkel.GetSkeletonJointOrientation(m_candidateID, skelJoint, jointOri);

		static float deg = 0;
		if(jointOri.fConfidence > 0 )
		{
			XnVector3D col1 = xnCreatePoint3D(jointOri.orientation.elements[0], jointOri.orientation.elements[3], jointOri.orientation.elements[6]);
			XnVector3D col2 = xnCreatePoint3D(jointOri.orientation.elements[1], jointOri.orientation.elements[4], jointOri.orientation.elements[7]);
			XnVector3D col3 = xnCreatePoint3D(jointOri.orientation.elements[2], jointOri.orientation.elements[5], jointOri.orientation.elements[8]);
	
			Ogre::Matrix3 matOri(jointOri.orientation.elements[0],-jointOri.orientation.elements[1],jointOri.orientation.elements[2],
								-jointOri.orientation.elements[3],jointOri.orientation.elements[4],-jointOri.orientation.elements[5],
								jointOri.orientation.elements[6],-jointOri.orientation.elements[7],jointOri.orientation.elements[8]);
			Quaternion q;
			
			newQ.FromRotationMatrix(matOri);
			
			bone->resetOrientation(); //in order for the conversion from world to local to work.
			newQ = bone->convertWorldToLocalOrientation(newQ);
			
			bone->setOrientation(newQ*qI);			
		} 
	}

void PSupdateBody(Real deltaTime)
	{
		static bool bNewUser = true;
		static Vector3 origTorsoPos;
		SkeletonCapability pUserSkel = Xn_user.GetSkeletonCap();
		//set smoothing according to the players request.
		pUserSkel.SetSmoothing(0.5);
		Skeleton* skel = mCharacter->getSkeleton();
		Bone* rootBone = skel->getBone("Root");
		XnSkeletonJointPosition torsoPos;

		if (pUserSkel.IsTracking(m_candidateID)){
			if(bNewUser){			
				pUserSkel.GetSkeletonJointPosition(m_candidateID, XN_SKEL_TORSO, torsoPos);
				if(torsoPos.fConfidence > 0.5){
					origTorsoPos.x = -torsoPos.position.X;
					origTorsoPos.y = torsoPos.position.Y;
					origTorsoPos.z = -torsoPos.position.Z;
					bNewUser = false;
				}
			}
			transformBone("Stomach",XN_SKEL_TORSO, true);
			transformBone("Waist", XN_SKEL_WAIST);
			transformBone("Root", XN_SKEL_WAIST);
			transformBone("Chest",XN_SKEL_TORSO, true);
			transformBone("Humerus.L",XN_SKEL_LEFT_SHOULDER);
			transformBone("Humerus.R",XN_SKEL_RIGHT_SHOULDER);
			transformBone("Ulna.L",XN_SKEL_LEFT_ELBOW);
			transformBone("Ulna.R",XN_SKEL_RIGHT_ELBOW);
			transformBone("Thigh.L",XN_SKEL_LEFT_HIP);
			transformBone("Thigh.R",XN_SKEL_RIGHT_HIP);
			transformBone("Calf.L",XN_SKEL_LEFT_KNEE);
			transformBone("Calf.R",XN_SKEL_RIGHT_KNEE);

			if(!bNewUser)
			{			 
				pUserSkel.GetSkeletonJointPosition(m_candidateID, XN_SKEL_TORSO, torsoPos);
				Vector3 newPos;
				newPos.x = -torsoPos.position.X;
				newPos.y = torsoPos.position.Y;
				newPos.z = -torsoPos.position.Z;

				Vector3 newPos2 = (newPos - origTorsoPos)/100;
				newPos2.y -= 0.3;
				 
				if (newPos2.y < 0){
					newPos2.y /= 2.5;
					if (newPos2.y < -1.5){
						newPos2.y = -1.5;
					}
				}

				if(torsoPos.fConfidence > 0.5){
					rootBone->setPosition(newPos2);
				}
			}
		} // end if player calibrated
		else{
			//return to initialState
			if(!bNewUser){
				rootBone->resetToInitialState();
			}
			bNewUser = true;
		}
	}
/*
	void update_background(){
		ImageMetaData rgb_md;
		TexturePtr mBackground = TextureManager::getSingleton().getByName("DynamicTexture");
		Xn_image.GetMetaData(rgb_md);
		// Get the pixel buffer
		HardwarePixelBufferSharedPtr pixelBuffer = mBackground->getBuffer();
		// Lock the pixel buffer and get a pixel box
		pixelBuffer->lock(HardwareBuffer::HBL_DISCARD); // for best performance use HBL_DISCARD!	
		const PixelBox& pixelBox = pixelBuffer->getCurrentLock();
		const XnUInt8* rgb_data = rgb_md.Data();
		char* pDest = static_cast<char*>(pixelBox.data);
		for (int j = 0; j < HEIGHT; j++){
			for (int i=0; i< WIDTH; i++){
				uint fixed_i = i;
				if(!mirrored){
					fixed_i = WIDTH - i;
				}
				pDest[j*WIDTH*4 + 4*i + 0] = rgb_data[j*WIDTH*3 + 3*fixed_i + 2]; // B
				pDest[j*WIDTH*4 + 4*i + 1] = rgb_data[j*WIDTH*3 + 3*fixed_i + 1]; // G
				pDest[j*WIDTH*4 + 4*i + 2] = rgb_data[j*WIDTH*3 + 3*fixed_i + 0]; // R
				pDest[j*WIDTH*4 + 4*i + 3] = 255;//A
			}
		}
		// Unlock the pixel buffer
		pixelBuffer->unlock();
	}*/
	void updatepcl(){
		//rgb data
		ImageMetaData rgb_md;
		Xn_image.GetMetaData(rgb_md);
		const XnUInt8* rgb_data = rgb_md.Data();
		//user data
		SceneMetaData smd;
		Xn_user.GetUserPixels(0, smd);
		const XnLabel* pUsersLBLs = smd.Data();
		//depth data
		DepthMetaData depth_md;
		Xn_depth.GetMetaData(depth_md);
		const XnDepthPixel* depth_data = depth_md.Data();
		XnPoint3D pt3d[1];
		
		mpcl->beginUpdate(0);
		for (int y=0; y<HEIGHT;y++){
			for (int x=0;x<WIDTH;x++){
				if(m_precandidateID != 0 && m_precandidateID == pUsersLBLs[y*WIDTH + x]){
					pt3d[0].X=x+1;
					pt3d[0].Y=y+1;
					pt3d[0].Z=depth_data[y*WIDTH + x];
					nRetVal = Xn_depth.ConvertProjectiveToRealWorld(1,pt3d,pt3d);
					mpcl->position(-pt3d[0].X,pt3d[0].Y,-pt3d[0].Z);
					mpcl->colour(
					(float)rgb_data[y*WIDTH*3 + 3*x + 0]/255,
					(float)rgb_data[y*WIDTH*3 + 3*x + 1]/255,
					(float)rgb_data[y*WIDTH*3 + 3*x + 2]/255);
				}
			}
		}
		mpcl->end();
	}

	void UpdateUserInfo(){
		TexturePtr texture = TextureManager::getSingleton().getByName("UserInformation");
		// Get the pixel buffer
		HardwarePixelBufferSharedPtr pixelBuffer = texture->getBuffer();
		// Lock the pixel buffer and get a pixel box
		pixelBuffer->lock(HardwareBuffer::HBL_DISCARD); 
		const PixelBox& pixelBox = pixelBuffer->getCurrentLock();
		unsigned char* pDest = static_cast<unsigned char*>(pixelBox.data);
		// Get label map 
		SceneMetaData smd;
		Xn_user.GetUserPixels(0, smd);
		const XnLabel* pUsersLBLs = smd.Data();
		unsigned int color;
		for (size_t j = 0; j < HEIGHT; j++)
		{
			pDest = static_cast<unsigned char*>(pixelBox.data) + j*pixelBox.rowPitch*4;
			for(size_t i = 0; i < WIDTH; i++)
			{
				uint fixed_i = i;
				if(!mirrored){
					fixed_i = WIDTH - i;
				}		
				// if we have a candidate, filter out the rest
				if (m_precandidateID != 0){
					if  (m_precandidateID == pUsersLBLs[j*WIDTH + fixed_i]){
						color = 0xFFFF4500;
						if (Xn_user.GetSkeletonCap().IsTracking(m_candidateID)){
							//highlight user
							color = 0xFF00FF00;
						}
					}
					else{
						color = 0;
					}
				}				
				// write to output buffer
				*((unsigned int*)pDest) = color;
				pDest+=4;
			}
		}
		// Unlock the pixel buffer
		pixelBuffer->unlock();
	}

	void setupBone(const String& name,const Ogre::Quaternion& q)
	{
		Ogre::Bone* bone = mCharacter->getSkeleton()->getBone(name);
		bone->setManuallyControlled(true);
		bone->setInheritOrientation(false);
		
		bone->resetOrientation();
		bone->setOrientation(q);
	
		bone->setInitialState();
	}

	void setupBone(const String& name,const Degree& yaw,const Degree& pitch,const Degree& roll)
	{
		Ogre::Bone* bone = mCharacter->getSkeleton()->getBone(name);
		bone->setManuallyControlled(true);
		bone->setInheritOrientation(false);
		
		bone->resetOrientation();
		
		bone->yaw(yaw);
		bone->pitch(pitch);
		bone->roll(roll);
	
		//Matrix3 mat = bone->getLocalAxes();
		bone->setInitialState();

	}
	void setupCharacter(){
		//
		XnPoint3D com;
		Xn_user.GetCoM(m_candidateID,com);
		Vector3 ogrecom;
		ogrecom.x = -com.X;
		ogrecom.y = com.Y+100;
		ogrecom.z = -com.Z;
		/// Model
		mCharacter = mSceneMgr->createEntity("Character", "Sinbad.mesh");
		SceneNode* CharacterNode = mSceneMgr->getRootSceneNode()->createChildSceneNode(ogrecom);
		CharacterNode->attachObject(mCharacter);
		CharacterNode->scale(150,150,150);
	}

	void setupAnimations(){	

		//set all to manualy controlled
		Ogre::Quaternion q = Quaternion::IDENTITY;
		Quaternion q2,q3;
		Vector3 xAxis,yAxis,zAxis;
		q.FromAngleAxis(Ogre::Degree(90),Vector3(0,0,-1));
		q.ToAxes(xAxis,yAxis,zAxis);
		q2.FromAngleAxis(Ogre::Degree(90),xAxis);
		setupBone("Humerus.L",q*q2);
		q.FromAngleAxis(Ogre::Degree(90),Vector3(0,0,1));
		q.ToAxes(xAxis,yAxis,zAxis);
		q2.FromAngleAxis(Ogre::Degree(90),xAxis);
		setupBone("Humerus.R",q*q2);
		
		q.FromAngleAxis(Ogre::Degree(90),Vector3(0,0,-1));	 
		q2.FromAngleAxis(Ogre::Degree(45),Vector3(0,-1,0));
		
		setupBone("Ulna.L",q*q2);

		q.FromAngleAxis(Ogre::Degree(90),Vector3(0,0,1));	 	
		setupBone("Ulna.R",q*q2.Inverse());
		
		q.FromAngleAxis(Ogre::Degree(180),Vector3(0,1,0));
		setupBone("Chest",q);
		setupBone("Stomach",q);
		q.FromAngleAxis(Ogre::Degree(180),Vector3(1,0,0));	 	
		q2.FromAngleAxis(Ogre::Degree(180),Vector3(0,1,0));
		setupBone("Thigh.L",q*q2);
		setupBone("Thigh.R",q*q2);
		setupBone("Calf.L",q*q2);
		setupBone("Calf.R",q*q2);
		setupBone("Root",Degree(0),Degree(0),Degree(0));

		// this is very important due to the nature of the exported animations
		/*
		mCharacter->getSkeleton()->setBlendMode(ANIMBLEND_CUMULATIVE);
		Skeleton* skel = mCharacter->getSkeleton();
		Ogre::Skeleton::BoneIterator bi = skel->getBoneIterator();
		String animNames[] =
		{"IdleBase", "IdleTop", "RunBase", "RunTop", "HandsClosed", "HandsRelaxed", "DrawSwords",
		"SliceVertical", "SliceHorizontal", "Dance", "JumpStart", "JumpLoop", "JumpEnd"};
		// populate our animation list
		for (int i = 0; i < 13; i++)
		{
			mAnims[i] = mCharacter->getAnimationState(animNames[i]);
			// disable animation updates
			Animation* anim = mCharacter->getSkeleton()->getAnimation(animNames[i]);
			anim->destroyNodeTrack(mCharacter->getSkeleton()->getBone("Ulna.L")->getHandle());
			anim->destroyNodeTrack(mCharacter->getSkeleton()->getBone("Ulna.R")->getHandle());
			anim->destroyNodeTrack(mCharacter->getSkeleton()->getBone("Humerus.L")->getHandle());
			anim->destroyNodeTrack(mCharacter->getSkeleton()->getBone("Humerus.R")->getHandle());
			anim->destroyNodeTrack(mCharacter->getSkeleton()->getBone("Stomach")->getHandle());
			anim->destroyNodeTrack(mCharacter->getSkeleton()->getBone("Chest")->getHandle());
			anim->destroyNodeTrack(mCharacter->getSkeleton()->getBone("Thigh.L")->getHandle());
			anim->destroyNodeTrack(mCharacter->getSkeleton()->getBone("Thigh.R")->getHandle());
			anim->destroyNodeTrack(mCharacter->getSkeleton()->getBone("Calf.L")->getHandle());
			anim->destroyNodeTrack(mCharacter->getSkeleton()->getBone("Calf.R")->getHandle());
			anim->destroyNodeTrack(mCharacter->getSkeleton()->getBone("Root")->getHandle());
		}
		*/
	}

	void addTime(Real deltaTime){
		context.WaitAndUpdateAll();
	//	update_background();
		UpdateUserInfo();
		updatepcl();
		if (Xn_user.GetSkeletonCap().IsTracking(m_candidateID)){
			PSupdateBody(deltaTime);
		}
	}
};

