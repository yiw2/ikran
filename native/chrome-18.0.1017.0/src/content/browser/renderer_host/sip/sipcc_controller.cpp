/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is the Cisco Systems SIP Stack.
 *
 * The Initial Developer of the Original Code is
 * Cisco Systems (CSCO).
 * Portions created by the Initial Developer are Copyright (C) 2002
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Enda Mannion <emannion@cisco.com>
 *  Suhas Nandakumar <snandaku@cisco.com>
 *  Ethan Hugg <ehugg@cisco.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include "sipcc_controller.h"
#include "base/bind.h"
#include "content/public/browser/browser_thread.h"
#include "sipcc_video_renderer.h"

#include <vector>
#ifndef WIN32
#include <sys/socket.h>
#include <netdb.h>
#endif
#include "Logger.h"
using namespace CSF;
using content::BrowserThread;


SipccController* SipccController::_instance = 0;

static int transformArray['D'+1] = { 0,         0,         0,         0,         0,         0,         0,         0,         0,         0,     //9
                          	  	  	 0,         0,         0,         0,         0,         0,         0,         0,         0,         0,     //19
                          	  	  	 0,         0,         0,         0,         0,         0,         0,         0,         0,         0,     //29
                          	  	  	 0,         0,         0,         0,         0,         KEY_POUND, 0,         0,         0,         0,     //39
                          	  	  	 0,         0,         KEY_STAR,  KEY_PLUS,  0,         0,         0,         0,         KEY_0,     KEY_1, //49
                          	  	  	 KEY_2,     KEY_3,     KEY_4,     KEY_5,     KEY_6,     KEY_7,     KEY_8,     KEY_9,     0,         0,     //59
                          	  	  	 0,         0,         0,         0,         0,         KEY_A,     KEY_B,     KEY_C,     KEY_D  };         //68


// Helper class to our implementation of WebRTC's External Renderer
// We do this to avoid circular inclusions

struct SipccController::VideoRenderer{
  VideoRenderer(SipccVideoRenderer* renderer_): video_renderer(renderer_)
  {
  }

  ~VideoRenderer() {}

  SipccVideoRenderer* video_renderer;

};


//Singleton instance generator
SipccController* SipccController::GetInstance() 
{
	if(_instance == 0)
	{
		_instance = new SipccController();
	} 
	return _instance;
}


SipccController::SipccController() : device_ptr_(NULL),
									localVoipPort("5060"),
									remoteVoipPort("5060"),
									videoDirection(CC_SDP_DIRECTION_SENDRECV),
									video_window(0),
									ext_renderer(0),
									capture_renderer(0),
									ccm_ptr_(NULL),
									observer_(NULL),
								    sipcc_thread_("SipccThread"),
								    hasCaptureRenderer(false),
									hasRemoteRenderer(false),
									isRegistered(false),
									inSession(false)
{
	Logger::Instance()->logIt(" In Sipcc Controller");
	// we need a thread to seperate the handling of signaling and
	// media buffers
    //sipcc_thread_.Start(); 
}

SipccController::~SipccController() {
}


// Sipcc Video renderer invocations
// Function to trigger letting the renderer process know a new buffer
// is created through Shared Memory

void SipccController::OnCaptureBufferCreated(base::SharedMemoryHandle handle,
                                        	int length,
                                        	int buffer_id)
{
        LOG(INFO) << " SipccController:: OnCaptureBufferCreated";
        observer_->DoSendCaptureBufferCreated(handle, length, buffer_id);
}


void SipccController::OnReceiveBufferCreated(base::SharedMemoryHandle handle,
                                        	int length,
                                        	int buffer_id)
{
        LOG(INFO) << " SipccController:: OnReceiveBufferCreated";
        observer_->DoSendReceiveBufferCreated(handle, length, buffer_id);
}

// Function to tell the renderer process on new video frame 
// filled up in the buffer pointed by buffer_id
void SipccController::OnCaptureBufferReady(int buffer_id,
                                     		unsigned int timestamp)
{
   LOG(INFO) << "SipccController:: OnCaptureBufferReady";
	observer_->DoSendCaptureBufferFilled(buffer_id, timestamp);
}

void SipccController::OnReceiveBufferReady(int buffer_id,
                                     		unsigned int timestamp)
{
   LOG(INFO) << "SipccController:: OnReceiveBufferReady";
	observer_->DoSendReceiveBufferFilled(buffer_id, timestamp);
}

// Function invoked in response to Renderer process returing
// the processed buffer with video frame
void SipccController::ReturnCaptureBuffer(int buffer_id)
{
	LOG(INFO) << "SipccController:: Return Capture Buffer : " << buffer_id;
    capRenderer->video_renderer->ReturnBuffer(buffer_id);
}

void SipccController::ReturnReceiveBuffer(int buffer_id)
{
	LOG(INFO) << "SipccController:: Return Receive Buffer : " << buffer_id;
    vRenderer->video_renderer->ReturnBuffer(buffer_id);
}

//Internal function -- Executed on Sipcc Thread
void SipccController::InitInternal() {
	
	Logger::Instance()->logIt(" INIT INTERNAL");
	initDone = false;
	ccm_ptr_ = CSF::CallControlManager::create();
	ccm_ptr_->addCCObserver(this);
	ccm_ptr_->addECCObserver(this);
    ccm_ptr_->setLocalIpAddressAndGateway(local_ip_v4_address_,"");
	//ccm_ptr_->setSIPCCLoggingMask( GSM_DEBUG_BIT | FIM_DEBUG_BIT | SIP_DEBUG_MSG_BIT | CC_APP_DEBUG_BIT | SIP_DEBUG_REG_STATE_BIT );
	ccm_ptr_->setSIPCCLoggingMask(0); 

	// Set Config properties needed at initialization
	// required as calling setProperty before ccm_ptr is initilized will not set the values
	ccm_ptr_->setProperty(ConfigPropertyKeysEnum::eLocalVoipPort ,localVoipPort);
	ccm_ptr_->setProperty(ConfigPropertyKeysEnum::eRemoteVoipPort ,remoteVoipPort);
	ccm_ptr_->setProperty(ConfigPropertyKeysEnum::eTransport ,transport);

  CSFLogError("ikran", "SipccController:: Authentication user : %s", sip_user_.c_str());
  
	initDone = true;	
}

// Executed on Sipcc Thread
bool SipccController::RegisterInternal() {
	
	Logger::Instance()->logIt("RegisterInternal");	
    Logger::Instance()->logIt(sip_user_);
    Logger::Instance()->logIt(sip_credentials_);
    Logger::Instance()->logIt(device_);
    Logger::Instance()->logIt(sip_domain_); 
	if(ccm_ptr_->registerUser(device_, sip_user_, sip_credentials_, sip_domain_) == false) {
		Logger::Instance()->logIt("RegisterInternal - FAILED ");
		return false;
	}
    
	return true;
}


int SipccController::StartP2PMode(std::string sipUser) {
	int result = 0;
	sip_user_ = sipUser;
	Logger::Instance()->logIt("StartP2PMode");
	Logger::Instance()->logIt(sip_user_);
	GetLocalActiveInterfaceAddress();

	InitInternal();
	if(ccm_ptr_->startP2PMode(sip_user_) == false) {
		Logger::Instance()->logIt("StartP2PMode - FAILED ");
		return -1;
	}

	return result;
}

void SipccController::PlaceP2PCall(std::string dial_number,  std::string sipDomain) {

	Logger::Instance()->logIt(" SipccController::PlaceP2PCall");
	dial_number_ = dial_number;
	sipDomain_ = sipDomain;
	 if (ccm_ptr_ != NULL)
     {
		Logger::Instance()->logIt(" Dial Number is ");
		Logger::Instance()->logIt(dial_number_);
		Logger::Instance()->logIt(" Domain is ");
		Logger::Instance()->logIt(sipDomain);
        device_ptr_ = ccm_ptr_->getActiveDevice();
        outgoing_call_ = device_ptr_->createCall();
        outgoing_call_->setExternalRenderer(0,ext_renderer);
        if(outgoing_call_->originateP2PCall(videoDirection, dial_number_, sipDomain_)) {
			Logger::Instance()->logIt("SipccController::PlaceP2PCall: Call Setup Succeeded ");
        	return ;
        } else {
        }
    } else {
    }
}


void SipccController::InitCaptureRenderer()
{
   LOG(INFO) << "SipccController::InitCaptureRenderer";
   SipccVideoRenderer* sipcc_video_capture_renderer_ = new SipccVideoRenderer(640,480,"local");
   capRenderer = new VideoRenderer(sipcc_video_capture_renderer_);
   capture_renderer = (void*) sipcc_video_capture_renderer_;
   hasCaptureRenderer = true;
}

void SipccController::InitExternalRenderer()
{
   LOG(INFO) << "SipccController::InitExternalRenderer";
   SipccVideoRenderer* sipcc_video_renderer_ = new SipccVideoRenderer(640,480,"remote");
   vRenderer = new VideoRenderer(sipcc_video_renderer_);
   ext_renderer = (void*) sipcc_video_renderer_;
   hasRemoteRenderer = true;
}

// API Functions
void SipccController::Register(std::string device, std::string sipUser, std::string sipCredentials, std::string sipDomain, bool isLocal) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
    LOG(INFO) << " SipccContoller:Register Invoked on the Browser IO Thread ";
	int result = 0;	
	sip_user_ = sipUser;
	sip_credentials_ = sipCredentials;
    device_ = device;
    sip_domain_ = sipDomain;
   LOG(INFO) << "SipccController::RegisterOnSipccThread()" ;
   LOG(INFO) << " Sip user " << sip_user_;
   LOG(INFO) << " Sip Device" << device_;
   LOG(INFO) << " Sip Domain" << sip_domain_;

	if(isRegistered == false)
	{
   		GetLocalActiveInterfaceAddress();
    	InitInternal();
    	if(RegisterInternal() == false) {
       	 	LOG(INFO)<< "Sipcc Registration Failed";
			isRegistered = false;
        	return;
    	} else {
			isRegistered = true;
		}

	} else {
			LOG(INFO) <<"SipccController::Register: ALREADY REGISTERED ";
	}


	//registration is done
	if(isLocal == true)
	{
		InitCaptureRenderer();
	} else {
		InitExternalRenderer();
	}
}

/***** THIS IS DEPRECATED **********/
void SipccController::RegisterOnSipccThread()
{
   LOG(INFO) << "SipccController::RegisterOnSipccThread()" ;
   LOG(INFO) << " Sip user " << sip_user_;
   LOG(INFO) << " Sip Device" << device_;
   LOG(INFO) << " Sip Domain" << sip_domain_;

   GetLocalActiveInterfaceAddress();

    InitInternal();
    if(RegisterInternal() == false) {
		LOG(INFO)<< "Sipcc Registration Failed";
    }   
   
}

int SipccController::StartROAPProxy(std::string device, std::string sipUser, std::string sipCredentials, std::string sipDomain) {
	int result = 0;
	sip_user_ = sipUser;
	sip_credentials_ = sipCredentials;
    device_ = device;
    sip_domain_ = sipDomain;
    Logger::Instance()->logIt(sip_user_);
    Logger::Instance()->logIt(device_);
    Logger::Instance()->logIt(sip_domain_);
    GetLocalActiveInterfaceAddress();

    InitInternal();
	if(ccm_ptr_->startROAPProxy(device_, sip_user_, sip_credentials_, sip_domain_) == false) {
		Logger::Instance()->logIt("startROAPProxy - FAILED ");
		return -1;
	}
	return result;
}

void SipccController::UnRegister() {
	if (ccm_ptr_ != NULL) {
            ccm_ptr_->disconnect();
            ccm_ptr_->destroy();
            ccm_ptr_ = NULL_PTR(CallControlManager);
            _instance = 0;
	}
}

void SipccController::PlaceCall(std::string dial_number, std::string ipAddress, int audioPort, int videoPort) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
	Logger::Instance()->logIt(" SipccController::PlaceCall invoked on Browser Thread");
	dial_number_ = dial_number;	
	if(inSession == true)
	{
		Logger::Instance()->logIt(" SipccController::PlaceCall ALREADY IN SESSION ");
		return;
	}
     if (ccm_ptr_ != NULL)
     {
    	Logger::Instance()->logIt(" Dial Number is ");
    	Logger::Instance()->logIt(dial_number_);
    	device_ptr_ = ccm_ptr_->getActiveDevice();
    	outgoing_call_ = device_ptr_->createCall();
		if(hasCaptureRenderer == true )
		{
			Logger::Instance()->logIt(" SipccController::PlaceCall Setting Capture Renderer");
   	 		outgoing_call_->setCaptureRenderer(0,capture_renderer);
		}

		if(hasRemoteRenderer == true)
		{
			Logger::Instance()->logIt(" SipccController::PlaceCall Setting Remote Renderer");
   	 		outgoing_call_->setExternalRenderer(0,ext_renderer);
		}

    	//defaulting to I420 video format retrieval
    	videoDirection = CC_SDP_DIRECTION_SENDRECV;
    	if(outgoing_call_->originateCall(videoDirection, dial_number_, (char *)"", 0, 0)) {
       		 LOG(INFO) << " SipccController::PlaceCall: Call Setup Succeeded ";
       	 	 return;
        } else {
        }
     } else {
		
    }

}

/*** THIS IS DEPRECATED ****/
void SipccController::PlaceCallOnSipccThread()
{
    LOG(INFO) << " SipccController::PlaceCallOnSipccThread() "; 
	 if (ccm_ptr_ != NULL)
     {
    Logger::Instance()->logIt(" Dial Number is ");
    Logger::Instance()->logIt(dial_number_);
    device_ptr_ = ccm_ptr_->getActiveDevice();
    outgoing_call_ = device_ptr_->createCall();
    //defaulting to I420 video format retrieval
    //ext_renderer = (void*) new SipccVideoRenderer(640,480);
    LOG(INFO) << " SipccController: External Renderer is " << ext_renderer;
    outgoing_call_->setExternalRenderer(0,ext_renderer);
    videoDirection = CC_SDP_DIRECTION_SENDRECV; 
    if(outgoing_call_->originateCall(videoDirection, dial_number_, (char *)"", 0, 0)) {
		LOG(INFO) << " SipccController::PlaceCallOnSipccThread: Call Setup Succeeded ";
       	return ;
        } else {
        }
    } else {
    }

}


void SipccController::EndCall() {

	if (ccm_ptr_ != NULL) {
		CC_CallPtr endableCall = GetFirstCallWithCapability(ccm_ptr_, CC_CallCapabilityEnum::canEndCall);
		if (endableCall != NULL) {
			if (!endableCall->endCall()) {
			}
		} else {
		}	
	} else {
	}	

}

void SipccController::AnswerCall() {
Logger::Instance()->logIt(" In Asnwer call ");
	if (ccm_ptr_ != NULL) {
		
		CC_CallPtr answerableCall = GetFirstCallWithCapability(ccm_ptr_, CC_CallCapabilityEnum::canAnswerCall);
		 videoDirection = CC_SDP_DIRECTION_SENDRECV;	
		if (answerableCall != NULL) {		
			if (!answerableCall->answerCall(videoDirection)) {
			} else {
				 if(hasCaptureRenderer == true )
				{
						Logger::Instance()->logIt(" SipccController::AnswerCall: Setting Capture Renderer");
						answerableCall->setCaptureRenderer(0,capture_renderer);
				}

				if(hasRemoteRenderer == true)
				{
						Logger::Instance()->logIt(" SipccController::AnswerCall Setting Remote Renderer");
						answerableCall->setExternalRenderer(0,ext_renderer);
				}

			}
		} else {
			Logger::Instance()->logIt("AnswerCall no active call");
        }
	} else {
		Logger::Instance()->logIt("AnswerCall: ccm pointer not created");
	}
}

void SipccController::SetProperty(std::string key, std::string value)
{
	Logger::Instance()->logIt("In SetProperty");
	Logger::Instance()->logIt(key);
	Logger::Instance()->logIt(value);

	const int length = key.length();
	for(int i=0; i < length; ++i) {
		key[i] = tolower(key[i]);
	}

	if (key == "localvoipport") {
		if (ccm_ptr_ != NULL)
			ccm_ptr_->setProperty(ConfigPropertyKeysEnum::eLocalVoipPort ,value);
		else
			localVoipPort = value;
	} else if (key == "remotevoipport") {
		if (ccm_ptr_ != NULL)
			ccm_ptr_->setProperty(ConfigPropertyKeysEnum::eRemoteVoipPort ,value);
		else
			remoteVoipPort = value;
	} else if (key == "transport") {
		if (ccm_ptr_ != NULL)
			ccm_ptr_->setProperty(ConfigPropertyKeysEnum::eTransport ,value);
		else
			transport = value;
	}  else if (key == "video") {
		if (value == "true")
			videoDirection = CC_SDP_DIRECTION_SENDRECV;
		else
			videoDirection = CC_SDP_DIRECTION_INACTIVE;
	}
}


std::string SipccController::GetProperty(std::string key)
{
	Logger::Instance()->logIt("In GetProperty");
	Logger::Instance()->logIt(key);

	string returnValue = "NONESET";
	if (key == "localvoipport") {
		if (ccm_ptr_ != NULL)
			returnValue = ccm_ptr_->getProperty(ConfigPropertyKeysEnum::eLocalVoipPort);
	} else if (key == "remotevoipport") {
		if (ccm_ptr_ != NULL)
			returnValue = ccm_ptr_->getProperty(ConfigPropertyKeysEnum::eRemoteVoipPort);
	} else if (key == "version") {
		if (ccm_ptr_ != NULL)
			returnValue = ccm_ptr_->getProperty(ConfigPropertyKeysEnum::eVersion);
	} else if (key == "localIPAddress") {
		GetLocalActiveInterfaceAddress();	
		returnValue = local_ip_v4_address_;
	}

	return returnValue;
}

void SipccController::SendDigits(std::string digits)
{
	Logger::Instance()->logIt("In SendDigits");
	Logger::Instance()->logIt(digits);

	if (ccm_ptr_ != NULL) {

		unsigned int i=0;
		for(i=0; i < digits.length(); i++) {
			int asciiChar = static_cast<int>(digits[i]);

			if (((asciiChar >= '0') && (asciiChar <= '9')) ||
				(asciiChar == '#') || (asciiChar == '*') || (asciiChar == '+') ||
				((asciiChar >= 'A') && (asciiChar <= 'D'))) {

				Logger::Instance()->logIt("Valid DTMF digit");

				cc_digit_t dtmfDigit = (cc_digit_t) transformArray[asciiChar];
				CC_CallPtr endableCall = GetFirstCallWithCapability(ccm_ptr_, CC_CallCapabilityEnum::canSendDigit);
				if (endableCall != NULL) {
					if (!endableCall->sendDigit(dtmfDigit)) {
					}
				} else {
					Logger::Instance()->logIt("no active call");
				}
			} else {
				Logger::Instance()->logIt("non DTMF digit");
			}
		}
	} else {
		Logger::Instance()->logIt("SendDigits: ccm pointer not created");
	}
}

void SipccController::HoldCall() {

	Logger::Instance()->logIt("In HoldCall");
	if (ccm_ptr_ != NULL) {
		CC_CallPtr endableCall = GetFirstCallWithCapability(ccm_ptr_, CC_CallCapabilityEnum::canHold);
		if (endableCall != NULL) {
			if (!endableCall->hold(CC_HOLD_REASON_NONE)) {
				Logger::Instance()->logIt("HoldCall failed");
			}
		} else {
			Logger::Instance()->logIt("HoldCall no active call");
		}
	} else {
		Logger::Instance()->logIt("HoldCall: ccm pointer not created");
	}

}

void SipccController::ResumeCall() {

	Logger::Instance()->logIt("In ResumeCall");
	if (ccm_ptr_ != NULL) {
		CC_CallPtr endableCall = GetFirstCallWithCapability(ccm_ptr_, CC_CallCapabilityEnum::canResume);
		if (endableCall != NULL) {
			if (!endableCall->resume(videoDirection)) {
				Logger::Instance()->logIt("ResumeCall failed");
			}
		} else {
			Logger::Instance()->logIt("ResumeCall no active call");
		}
	} else {
		Logger::Instance()->logIt("ResumeCall: ccm pointer not created");
	}

}

// Device , Line Events notification handlers
void SipccController::onDeviceEvent (ccapi_device_event_e deviceEvent, CC_DevicePtr device, CC_DeviceInfoPtr info) {
}

void SipccController::onFeatureEvent (ccapi_device_event_e deviceEvent, CC_DevicePtr device, CC_FeatureInfoPtr info) {
}

void SipccController::onLineEvent (ccapi_line_event_e lineEvent, CC_LinePtr line, CC_LineInfoPtr info) {
}

void SipccController::onAvailablePhoneEvent	(AvailablePhoneEventType::AvailablePhoneEvent event,const PhoneDetailsPtr availablePhoneDetails)
{}

void SipccController::onAuthenticationStatusChange	(AuthenticationStatusEnum::AuthenticationStatus)
{}

//SipStack Callbacks for changes in call status.
void SipccController::onCallEvent (ccapi_call_event_e callEvent, CC_CallPtr call, CC_CallInfoPtr info, char* sdp) 
{
	if (callEvent == CCAPI_CALL_EV_STATE) {
		
		if (info->getCallState() == RINGIN) {
			Logger::Instance()->logIt("SipccController::onCallEvent RINGIN");
			std::string callingPartyName = info->getCallingPartyName();
			std::string callingPartyNumber = info->getCallingPartyNumber();
			Logger::Instance()->logIt(callingPartyName);
			Logger::Instance()->logIt(callingPartyNumber);
			if(callingPartyName.length() == 0 || callingPartyName == "")
			{
				callingPartyName = "Dummy";
			}
			if(observer_ != NULL) {
				observer_->OnIncomingCall(callingPartyName, callingPartyNumber);
			}
        } 
		  else if (info->getCallState() == ONHOOK ) {
			Logger::Instance()->logIt("SipccController::onCallEvent ONHOOK");
			inSession = false;
			if(observer_ != NULL) {
               	observer_->OnCallTerminated();
			}
		} else if (info->getCallState() == CONNECTED ) {
			Logger::Instance()->logIt("SipccController::onCallEvent CONNECTED");
			inSession = true;
			if(observer_ != NULL)
            	observer_->OnCallConnected(sdp);
		} else if (info->getCallState() == RINGOUT ) {
			Logger::Instance()->logIt("SipccController::onCallEvent RINGOUT");
			if(observer_ != NULL)
            	observer_->OnCallConnected(sdp);
		} else if (info->getCallState() == HOLD ) {
			Logger::Instance()->logIt("SipccController::onCallEvent HOLD");
			if(observer_ != NULL)
				observer_->OnCallHeld();
		} else if (info->getCallState() == RESUME ) {
			Logger::Instance()->logIt("SipccController::onCallEvent RESUME");
			if(observer_ != NULL)
				observer_->OnCallResume();
		}
    } else if (callEvent == CCAPI_CALL_EV_CALLINFO) {
    	observer_->OnCallConnected(sdp);
    }
}

//SipStack Callbacks for changes in connection status.
void SipccController::onConnectionStatusChange	(ConnectionStatusEnum::ConnectionStatus status)
{
	std::string registrationState;
	if (status == ConnectionStatusEnum::eIdle) {
		registrationState = "no-registrar";
		Logger::Instance()->logIt( " NO_REGISTRAR  ");
		isRegistered = false;
	}
	else if (status == ConnectionStatusEnum::eRegistering) {
		registrationState = "registering";
		Logger::Instance()->logIt( " REGISTERING  ");
	}
	else if (status == ConnectionStatusEnum::eReady) {
		registrationState = "registered";
		Logger::Instance()->logIt( " REGISTERED  ");
		isRegistered = true;
	}
	else if (status == ConnectionStatusEnum::eFailed) {
			registrationState = "registration-failed";	
		Logger::Instance()->logIt(" REGISTRATION FAILED ");
		isRegistered = false;
	}
	
	if(observer_ != NULL)
		observer_->OnRegisterStateChange(registrationState);
	else
		Logger::Instance()->logIt(" SipccController::onConnectionStatusChange : Observer -> NULL ");
}

//Private Implemenation
CC_CallPtr SipccController::GetFirstCallWithCapability (CallControlManagerPtr ccmPtr, CC_CallCapabilityEnum::CC_CallCapability cap)
{
    if (ccmPtr == NULL) {
        return NULL_PTR(CC_Call);
    }
	
    CC_DevicePtr devicePtr = ccmPtr->getActiveDevice();
	
    if (devicePtr == NULL) {
        return NULL_PTR(CC_Call);
    }
	
    CC_DeviceInfoPtr deviceInfoPtr = devicePtr->getDeviceInfo();
	
    if (deviceInfoPtr == NULL) {
        return NULL_PTR(CC_Call);
    }
	
	std::vector<CC_CallPtr> calls = deviceInfoPtr->getCalls();
	
    for (std::vector<CC_CallPtr>::iterator it = calls.begin(); it != calls.end(); it++) {
        CC_CallPtr call = *it;
		
        if (call == NULL) {
            continue;
        }
		
        CC_CallInfoPtr callInfoPtr = call->getCallInfo();
		
        if (callInfoPtr == NULL) {
            continue;
        }
		
        if (callInfoPtr->hasCapability(cap)) {
            return call;
        }
    }//end for (calls)
	
    return NULL_PTR(CC_Call);
}

// POSIX Only Implementation
bool SipccController::GetLocalActiveInterfaceAddress() 
{
	std::string local_ip_address = "0.0.0.0";
#ifndef WIN32
	SOCKET sock_desc_ = INVALID_SOCKET;
	sock_desc_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	struct sockaddr_in proxy_server_client;
 	proxy_server_client.sin_family = AF_INET;
	proxy_server_client.sin_addr.s_addr	= inet_addr("10.0.0.1");
	proxy_server_client.sin_port = 12345;
	fcntl(sock_desc_,F_SETFL,  O_NONBLOCK);
	int ret = connect(sock_desc_, reinterpret_cast<sockaddr*>(&proxy_server_client),
                    sizeof(proxy_server_client));

	if(ret == SOCKET_ERROR)
	{
	}
 
	struct sockaddr_storage source_address;
	socklen_t addrlen = sizeof(source_address);
	ret = getsockname(
			sock_desc_, reinterpret_cast<struct sockaddr*>(&source_address),&addrlen);

	
	//get the  ip address 
	local_ip_address = NetAddressToString(
						reinterpret_cast<const struct sockaddr*>(&source_address),
						sizeof(source_address));
	local_ip_v4_address_ = local_ip_address;
	Logger::Instance()->logIt(" IP Address Is ");
	Logger::Instance()->logIt(local_ip_v4_address_);
	close(sock_desc_);
#else
	hostent* localHost;
	localHost = gethostbyname("");
	local_ip_v4_address_ = inet_ntoa (*(struct in_addr *)*localHost->h_addr_list);
	Logger::Instance()->logIt(" SipccController:GetLocalActiveInterfaceAddress inet address : ");;
	Logger::Instance()->logIt(local_ip_v4_address_);

#endif
	return true;
}

//Only POSIX Complaint as of 7/6/11
#ifndef WIN32
std::string SipccController::NetAddressToString(const struct sockaddr* net_address,
                               socklen_t address_len) {

  // This buffer is large enough to fit the biggest IPv6 string.
  char buffer[128];
  int result = getnameinfo(net_address, address_len, buffer, sizeof(buffer),
                           NULL, 0, NI_NUMERICHOST);
  if (result != 0) {
	Logger::Instance()->logIt("SipccController::NetAddressToString: getnameinfo() Failed ");
    buffer[0] = '\0';
  }
  return std::string(buffer);
}
#endif

