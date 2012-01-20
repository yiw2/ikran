#include "DebugClient.h"
#include "csf_common.h"
#include "CSFLogStream.h"
#include "common.h"

//Global Message object
//CDebugClient debugClient;

static const char* logTag = "DebugClient";
static base::WaitableEvent _serverResponseEvent(true,false);



base::Lock _serverResponseMutex;


CDebugClient::CDebugClient():clientRecThread(NULL),base::SimpleThread("ClientThread") 
{
	m_bIsConnected = false;
}

void CDebugClient::Init(string sIpAddress, int iPort)
{

	m_sServerIPAddress = sIpAddress;
	m_iServerPort = iPort;
	struct hostent *hp;
	unsigned int addr;
	struct sockaddr_in server;
	
#ifdef WIN32
	WSADATA wsaData;

	int wsaret=WSAStartup(0x101,&wsaData);


	if(wsaret!=0)
	{
		return;
	}

	conn=socket(AF_INET,SOCK_STREAM,0);
	if(conn==INVALID_SOCKET)
		return;

	addr=inet_addr(m_sServerIPAddress.c_str());
	hp=gethostbyaddr((char*)&addr,sizeof(addr),AF_INET);
	
	if(hp==NULL)
	{
		closesocket(conn);
		return;
	}
#endif

	server.sin_addr.s_addr=*((unsigned long*)hp->h_addr);
	server.sin_family=AF_INET;
	server.sin_port=htons(m_iServerPort);
	if(connect(conn,(struct sockaddr*)&server,sizeof(server)))
	{
		closesocket(conn);
		return;	
	}
	m_bIsConnected = true;
	return;
}

CDebugClient::~CDebugClient()
{
	if(m_bIsConnected)
		closesocket(conn);
}

int CDebugClient::SendMessagePort(string sMessage)
{
		int iStat = 0;

		iStat = send(conn,sMessage.c_str(),sMessage.size()+1,0);
		if(iStat == -1)
			return 1;

		return 0;

}

int CDebugClient::RecMessagePort()
{
		char acRetData[4096];
		int iStat = 0;
		iStat = recv(conn,acRetData,4096,0);
		if(iStat == -1)
			return 1;
		cout<<"-->"<<acRetData<<"\n";
		//_serverResponseEvent.Signal();
		createSignal(eOriginatePhoneCall);
		return 0;
}


void CDebugClient::StartRecv()
{
		
	clientRecThread = new ClientRecThread(this);
	clientRecThread->Start();
	//CreateThread(NULL,0,(LPTHREAD_START_ROUTINE)ServerRecThread,(void *)m_SClient,0,NULL);

}


void CDebugClient::StopRecv()
{
	if(clientRecThread != NULL)
	{
		delete clientRecThread;
	}
}	
	//CreateThread(NULL,0,(LPTHREAD_START_ROUTINE)ServerRecThread,(void *)m_SClient,0,NULL);


void CDebugClient::ClientRecThread::Run()
{
	while(1)
	{
		if(m_debugClient != NULL )
			if(m_debugClient->RecMessagePort())
				break;
	}
}


void CDebugClient::Run()
{
	StartRecv();

	/*
	this->createUserOperation(eOriginatePhoneCall, new string("101"));
	this->createUserOperation(eEndFirstCallWithEndCallCaps);
	eUserOperationRequest e = eOriginatePhoneCall;
	
	for(int i=0;i<2;i++)
	{
		//_serverResponseEvent.TimedWait(base::TimeDelta::FromMilliseconds(1000));
		base::PlatformThread::Sleep(2000);
		createSignal(eOriginatePhoneCall);
		switch(e)
		{
			case eOriginatePhoneCall:
				if(_serverResponseEvent.TimedWait(base::TimeDelta::FromMilliseconds(40000))){
					base::AutoLock lock(_serverResponseMutex);
				    if(true)
					{
						base::PlatformThread::Sleep(2000);
						this->SendMessagePort("answercall");

						//base::PlatformThread::Sleep(10000);
						e = eEndFirstCallWithEndCallCaps;
						_serverResponseEvent.Reset();
						continue;
					}
					else
						break;
				}
				else
					break;
			case eEndFirstCallWithEndCallCaps:
				if(_serverResponseEvent.TimedWait(base::TimeDelta::FromMilliseconds(100000))){
					base::AutoLock lock(_serverResponseMutex);
				    if(true)
						
						continue;
					else
						break;
				}
				else
					break;
			default:
				break;
		}		

	}
	cout<<"client receive thread is break";
	*/
}

void CDebugClient::ExecuteCmd(const char* cmd)
{

	switch (*cmd)
	{
	case 'd':
		//createUserOperationAndSignal(eOriginatePhoneCall, new string(getInput("Enter DN# to call: ")));
		createUserOperationAndSignal(eOriginatePhoneCall, new string("1038"));
		break;
	case 'p':
		//createUserOperationAndSignal(eOriginateP2PPhoneCall, new string(getInput("Enter DN# to call P2P: ")));
		createUserOperationAndSignal(eOriginateP2PPhoneCall, new string("1038"));
		break;
	case 'a':
		createUserOperationAndSignal(eAnswerCall);
		break;
	case 'e':
		createUserOperationAndSignal(eEndFirstCallWithEndCallCaps);
		break;
	case 'h':
		createUserOperationAndSignal(eHoldFirstCallWithHoldCaps);
		break;
	case 'r':
		createUserOperationAndSignal(eResumeFirstCallWithResumeCaps);
		break;
	case 'm':
		createUserOperationAndSignal(eMuteAudioForConnectedCall);
		break;
	case 'n':
		createUserOperationAndSignal(eUnmuteAudioForConnectedCall);
		break;
	case 'k':
		createUserOperationAndSignal(eMuteVideoForConnectedCall);
		break;
	case 'j':
		createUserOperationAndSignal(eUnmuteVideoForConnectedCall);
		break;
	case 'v':
		createUserOperationAndSignal(eCycleThroughVideoPrefOptions);
		break;
	case 'l':
		createUserOperationAndSignal(ePrintActiveCalls);
		break;
	case '+':
		createUserOperationAndSignal(eVolumeUp);
		break;
	case '-':
		createUserOperationAndSignal(eVolumeDown);
		break;
	case 't':
		createUserOperationAndSignal(eToggleAutoAnswer);
		break;
	case 'z':
		createUserOperationAndSignal(eToggleShowVideoAutomatically);
		break;
	case 'x':
		createUserOperationAndSignal(eAddVideoToConnectedCall);
		break;
	case 'y':
		createUserOperationAndSignal(eRemoveVideoFromConnectedCall);
		break;
	case 'w':
		createUserOperationAndSignal(eDestroyAndCreateWindow);
		break;
	case 'q':
		createUserOperationAndSignal(eQuit);
		break;
	case '?':
		CSFLogDebugS(logTag, "\nl = print list of all calls");
		CSFLogDebugS(logTag, "\nd = dial");
		CSFLogDebugS(logTag, "\np = dialP2P");
		CSFLogDebugS(logTag, "\na = answer (incoming call)");
		CSFLogDebug(logTag, "\nv = cycle to next video pref option (current=%s)", getUserFriendlyNameForVideoPref(getActiveVideoPref()));
		CSFLogDebugS(logTag, "\ne = end call");
		CSFLogDebugS(logTag, "\nh = hold");
		CSFLogDebugS(logTag, "\nr = resume");
		CSFLogDebugS(logTag, "\nm = audio mute");
		CSFLogDebugS(logTag, "\nn = audio unmute");
		CSFLogDebugS(logTag, "\nk = video mute");
		CSFLogDebugS(logTag, "\nj = video unmute");
		CSFLogDebugS(logTag, "\n+ = volume up");
		CSFLogDebugS(logTag, "\n- = volume down");
#ifndef NOVIDEO
		CSFLogDebugS(logTag, "\nx = Add video to connected call (escalate)");
		CSFLogDebugS(logTag, "\ny = Remove video from connected call (de-escalate)");
		CSFLogDebugS(logTag, "\nz = toggle show-video-automatically");
#endif
		CSFLogDebugS(logTag, "\nt = toggle auto-answer");
		CSFLogDebugS(logTag, "\nw = destroy video window and create a new one");
		CSFLogDebugS(logTag, "\n0123456789#*ABCD = send digit");
		CSFLogDebugS(logTag, "\nq = quit");
		CSFLogDebugS(logTag, "\n");
		break;
	}
}


void CDebugClient::createUserOperationAndSignal (eUserOperationRequest request, void * pData)
{
	UserOperationRequestDataPtr pOperation(new UserOperationRequestData(request, pData));
	if(callback != NULL)
	{
		callback->onUserRequest(pOperation);
	}
}

void CDebugClient::createUserOperation (eUserOperationRequest request, void * pData)
{
	UserOperationRequestDataPtr pOperation(new UserOperationRequestData(request, pData));
	if(callback != NULL)
	{
		callback->onAddUserOperation(pOperation);
	}
}


void CDebugClient::createSignal (eUserOperationRequest request, void * pData)
{
	UserOperationRequestDataPtr pOperation(new UserOperationRequestData(request, pData));
	if(callback != NULL)
	{
		callback->onAddSignal(pOperation);
	}
}


void CDebugClient::setCallback(UserOperationCallback* callback)
{
	this->callback = callback;
}
