#include "ySocket.h"

#ifdef _WIN32
	#include <Winsock2.h>
	#include <process.h>
#else
	#include <netinet/in.h>
	#include <arpa/inet.h>
	//
	#include <sys/socket.h>
	//
	#include <signal.h>
	#include <unistd.h>
	#include <netdb.h>
	#include <errno.h>
	#include <string.h>
	#include <pthread.h>
#endif // def _WIN32
//
#include <sys/types.h>
#include <sys/timeb.h>

//#include "tcp_protocol.h"

//using namespace std;

#ifdef _WIN32
#  pragma comment(lib, "Ws2_32") // link with "Ws2_32.lib" (only MSVC)
#endif // def _WIN32

#ifdef _WIN32
#  define LONG_TYPE long
#else
#  define LONG_TYPE int
#endif // def _WIN32

//=============================================================================
struct YSOCKET_HANDLE {
#ifdef _WIN32
	SOCKET h;
#else
	int h;
#endif // ndef _WIN32
};

//=============================================================================
#define SOCKET_ADDRESS_FAMILY                   PF_INET
#define SOCKET_TYPE                             SOCK_STREAM
#define SOCKET_PROTOCOL                         "tcp"
#define SOCKET_BACKLOG                          5


//=============================================================================
#ifdef _WIN32
  #define YSOCKET_INVALID_SOCKET                INVALID_SOCKET
  #define YSOCKET_SOCKET_ERROR                  SOCKET_ERROR
  #define YSOCKET_SHUTDOWN_SOCKET_READ_WRITE    SD_BOTH
#else
  #define YSOCKET_INVALID_SOCKET                -1
  #define YSOCKET_SOCKET_ERROR                  -1
  #define YSOCKET_SHUTDOWN_SOCKET_READ_WRITE    SHUT_RDWR
#endif // def _WIN32


//=============================================================================
#define YSOCKET_CHECK_RESULT(iRes, LABEL)  \
if(0 != iRes)  \
{  \
	goto LABEL;  \
}
//=============================================================================
#define YSOCKET_CHECK_YSOCKET(iSocket, LABEL)  \
{  \
	if(YSOCKET_INVALID_SOCKET == iSocket)  \
	{  \
		goto LABEL;  \
	}  \
}
//=============================================================================
#define YSOCKET_CHECK_SOCKET_RESULT(iSocketRes, LABEL)  \
{  \
	if(YSOCKET_SOCKET_ERROR == iSocketRes)  \
	{  \
		goto LABEL;  \
	}  \
}


//=============================================================================
YSocket::YSocket()
	: m_iInitUseSocket(0)
{
	m_ySocket = new YSOCKET_HANDLE;
	m_ySocket->h = YSOCKET_INVALID_SOCKET;
	StartUseSOCKET();
}


//=============================================================================
YSocket::~YSocket()
{
	Close();
	EndUseSOCKET();
	delete m_ySocket;
}


//=============================================================================
int YSocket::CreateServer(unsigned short uPort, const char *pchLocalAddr/* = 0*/)
{
	int iRetCode = -1; // Failed.
	int iRes = 0;
	sockaddr_in sockAddrSrv;

	iRes = StartUseSOCKET();
	YSOCKET_CHECK_RESULT(iRes, END_YSocket_CreateServer);

	Close();

	// Create listen socket.
	{
		int iProtocol = 0;
		// Get protocol number by transport name.
		protoent *pProtoEnt = getprotobyname(SOCKET_PROTOCOL);
		if(0 != pProtoEnt)
		{
			iProtocol = pProtoEnt->p_proto;
		}
	m_ySocket->h = socket(SOCKET_ADDRESS_FAMILY, SOCKET_TYPE, iProtocol);
	}
	YSOCKET_CHECK_YSOCKET(m_ySocket->h, END_YSocket_CreateServer);

	// Set SO_REUSEADDR option for listen socket.
#ifdef _WIN32
	// Do not set SO_REUSEADDR on WIN !!!!
#else
	{
		int iREUSEADDR = 1;
	iRes = setsockopt(m_ySocket->h, SOL_SOCKET, SO_REUSEADDR, (const char*)&iREUSEADDR, sizeof(iREUSEADDR));
	}
	YSOCKET_CHECK_SOCKET_RESULT(iRes, END_YSocket_CreateServer);
#endif // def _WIN32

	// Bind listen socket with address.
	memset(&sockAddrSrv, 0, sizeof(sockaddr_in));
	sockAddrSrv.sin_family = SOCKET_ADDRESS_FAMILY;
	sockAddrSrv.sin_port   = htons(uPort);
	sockAddrSrv.sin_addr.s_addr = htonl(INADDR_ANY);
	if(0 != pchLocalAddr)
	{
		hostent *pHostEnt = gethostbyname(pchLocalAddr);
		if(0 != pHostEnt)
		{
			memcpy(&sockAddrSrv.sin_addr, pHostEnt->h_addr, pHostEnt->h_length);
		}
		else // (0 == pHostEnt)
		{
			LONG_TYPE lLocalAddress = inet_addr(pchLocalAddr);
			if((LONG_TYPE)INADDR_NONE != lLocalAddress)
			{
				sockAddrSrv.sin_addr.s_addr = htonl(lLocalAddress);
			}
		}
	}
	//
	{
		int iSockAddrLen = sizeof(sockAddrSrv);
	iRes = bind(m_ySocket->h, (struct sockaddr*)&sockAddrSrv, iSockAddrLen);
	}
	YSOCKET_CHECK_SOCKET_RESULT(iRes, END_YSocket_CreateServer);

	// Start listening.
	iRes = listen(m_ySocket->h, SOCKET_BACKLOG);
	YSOCKET_CHECK_SOCKET_RESULT(iRes, END_YSocket_CreateServer);

	// Set "Local" address.
	if(0 != pchLocalAddr)
	{
		m_sLocalAddr = pchLocalAddr;
	}
	else
	{
		char *pchAddr = inet_ntoa(sockAddrSrv.sin_addr);
		if(0 != pchAddr)
		{
			m_sLocalAddr = pchAddr;
		}
	}

	iRetCode = 0; // Success.


END_YSocket_CreateServer:

	return iRetCode;
}


//=============================================================================
int YSocket::AcceptClientConnection(YSocket *pySocketClient, int iWaitSec/* = 0*/)
{
	int iRetCode = -1; // Failed.
	int iRes  = 0;
	int iWhile = 1;

	fd_set allfds;
	fd_set readfds;
	sockaddr_in sockAddrClnt;
	int iSockAddrLen = 0;
	char *pchAddr = 0;

	struct timeval *pTimeOut = 0;
	struct timeval timeOut = { 0, 0 };
	int iMillisecondsInSelect = 0;
	int i0MillisecondsInSelect = 0;
	timeb time1 = { 0, 0, 0, 0 };
	timeb time2 = { 0, 0, 0, 0 };

	memset(&sockAddrClnt, 0, sizeof(sockaddr_in));

	iRes = StartUseSOCKET();
	YSOCKET_CHECK_RESULT(iRes, END_YSocket_AcceptClientConnection);

	YSOCKET_CHECK_YSOCKET(m_ySocket->h, END_YSocket_AcceptClientConnection);

	pySocketClient->Close();

	FD_ZERO(&allfds);
	FD_SET(m_ySocket->h, &allfds);

	iWhile = 1;
	while(1 == iWhile)
	{
		readfds = allfds;
		//
		pTimeOut = 0;
		if(0 != iWaitSec)
		{
			double dMillisecondsTime1 = ((double)time1.time)*1000 + time1.millitm;
			double dMillisecondsTime2 = ((double)time2.time)*1000 + time2.millitm;
			iMillisecondsInSelect += (int)(dMillisecondsTime2 - dMillisecondsTime1);
			//
			int iLastTime = iWaitSec*1000 - iMillisecondsInSelect;
			if(iLastTime < 0 ||
			   (0 == iLastTime && 1 == i0MillisecondsInSelect))
			{
				iRetCode = -3; // Time-out interval elapsed.
				goto END_YSocket_AcceptClientConnection;
			}
			if(0 == iLastTime)
			{
				i0MillisecondsInSelect = 1;
			}
			timeOut.tv_sec  = iLastTime/1000;
			timeOut.tv_usec = iLastTime%1000;
			//
			pTimeOut = &timeOut;
		}

		ftime(&time1);
		//
		iRes = select(m_ySocket->h+1, &readfds, 0, 0, pTimeOut);
		//
		ftime(&time2);

		if(YSOCKET_SOCKET_ERROR == iRes)
		{
			// "select failed"
#if !defined(_WIN32)
			if(EINTR == errno) // Interruped with signal.
			{
				continue;
			}
#endif // ndef _WIN32
			iRetCode = -2;
			goto END_YSocket_AcceptClientConnection;
		}
		if(0 == iRes)
		{
			// Timeout expired.
			continue;
		}
		// (iRes > 0) // select success.

		if( ! FD_ISSET(m_ySocket->h, &readfds))
		{
			// "Wrong socket"
			iRetCode = -4;
			goto END_YSocket_AcceptClientConnection;
		}

		memset(&sockAddrClnt, 0, sizeof(sockaddr_in));
		iSockAddrLen = sizeof(sockAddrClnt);
#ifdef _WIN32
		pySocketClient->m_ySocket->h = accept(m_ySocket->h, (struct sockaddr*)&sockAddrClnt, &iSockAddrLen);
#else
		pySocketClient->m_ySocket->h = accept(m_ySocket->h, (struct sockaddr*)&sockAddrClnt, (socklen_t*)&iSockAddrLen);
#endif // def _WIN32

		if(YSOCKET_INVALID_SOCKET == pySocketClient->m_ySocket->h)
		{
			// "accept failed"
			iRetCode = -5;
			goto END_YSocket_AcceptClientConnection;
		}
		// (YSOCKET_INVALID_SOCKET != pySocketClient->m_ySocket->h) // Client connection accepted.
		break;
	}

	YSOCKET_CHECK_YSOCKET(pySocketClient->m_ySocket->h, END_YSocket_AcceptClientConnection);

	// Set SocketClient's "Local" address.
	pySocketClient->m_sLocalAddr = m_sLocalAddr;
	// Set SocketClient's "Remote" address.
	pchAddr = inet_ntoa(sockAddrClnt.sin_addr);
	if(0 != pchAddr)
	{
		pySocketClient->m_sRemoteAddr = pchAddr;
	}

	iRetCode = 0; // Success.


END_YSocket_AcceptClientConnection:

	return iRetCode;
}


//=============================================================================
int YSocket::CreateClient()
{
	int iRetCode = -1; // Failed.
	int iRes = 0;
	bClosed = false;

	iRes = StartUseSOCKET();
	YSOCKET_CHECK_RESULT(iRes, END_YSocket_CreateClient);

	Close();

	// Create socket.
	{
		int iProtocol = 0;
		// Get protocol number by transport name.
		protoent *pProtoEnt = getprotobyname(SOCKET_PROTOCOL);
		if(0 != pProtoEnt)
		{
			iProtocol = pProtoEnt->p_proto;
		}
	m_ySocket->h = socket(SOCKET_ADDRESS_FAMILY, SOCKET_TYPE, iProtocol);
	}
	YSOCKET_CHECK_YSOCKET(m_ySocket->h, END_YSocket_CreateClient);

	iRetCode = 0; // Success.


END_YSocket_CreateClient:

	return iRetCode;
}


//=============================================================================
int YSocket::ConnectToServer(unsigned short uPort, const char *pchRemoteAddr/* = 0*/, int iWaitSec/* = 0*/)
{
	int iRetCode = -1; // Failed.
	int iRes = 0;
	int iWhile = 1;
	bClosed = false;

	sockaddr_in sockAddrSrv;
	int iSockAddrLen = 0;

	int iMillisecondsInConnect = 0;
	timeb time1 = { 0, 0, 0, 0 };
	timeb time2 = { 0, 0, 0, 0 };

	iRes = StartUseSOCKET();
	YSOCKET_CHECK_RESULT(iRes, END_YSocket_ConnectToServer);

	YSOCKET_CHECK_YSOCKET(m_ySocket->h, END_YSocket_ConnectToServer)

	// Connect to the server.
	memset(&sockAddrSrv, 0, sizeof(sockaddr_in));
	sockAddrSrv.sin_family = SOCKET_ADDRESS_FAMILY;
	sockAddrSrv.sin_port   = htons(uPort);
	{
		const char *pchServAddr = pchRemoteAddr;
		if(0 == pchServAddr)
		{
			pchServAddr = "localHost"; // Connect to the local host.
		}
		hostent *pHostEnt = gethostbyname(pchServAddr);
		if(0 != pHostEnt)
		{
			memcpy(&sockAddrSrv.sin_addr, pHostEnt->h_addr, pHostEnt->h_length);
		}
		else // (0 == pHostEnt)
		{
			LONG_TYPE lRemoteAddress = inet_addr(pchServAddr);
			//
			iRes = ((LONG_TYPE)INADDR_NONE == lRemoteAddress ? -1 : 0);
			YSOCKET_CHECK_RESULT(iRes, END_YSocket_ConnectToServer);
			//
			sockAddrSrv.sin_addr.s_addr = htonl(lRemoteAddress);
		}
	}
	iSockAddrLen = sizeof(sockAddrSrv);

	iWhile = 1;
	while(1 == iWhile)
	{
		if(0 != iWaitSec)
		{
			iMillisecondsInConnect += (int)
				( ((double)time2.time)*1000 + time2.millitm - ((double)time1.time)*1000 + time1.millitm );
			//
			int iLastTime = iWaitSec*1000 - iMillisecondsInConnect;
			if(iLastTime < 0)
			{
				iRetCode = -3; // Time-out interval elapsed.
				goto END_YSocket_ConnectToServer;
			}
		}

		ftime(&time1);
		//
		iRes = connect(m_ySocket->h, (struct sockaddr*)&sockAddrSrv, iSockAddrLen);
		//
		ftime(&time2);

		if(YSOCKET_SOCKET_ERROR == iRes)
		{
			// "connect failed".
			if(0 == iWaitSec)
			{
				goto END_YSocket_ConnectToServer;
			}
			else
			{
				continue;
			}
		}
		// (YSOCKET_SOCKET_ERROR != iRes) // Connected.
		break;
	}

	// Set "Local" address.
	memset(&sockAddrSrv, 0, sizeof(sockaddr_in));
	iSockAddrLen = sizeof(sockAddrSrv);
#ifdef _WIN32
	iRes = getsockname(m_ySocket->h, (struct sockaddr*)&sockAddrSrv, &iSockAddrLen);
#else
	iRes = getsockname(m_ySocket->h, (struct sockaddr*)&sockAddrSrv, (socklen_t*)&iSockAddrLen);
#endif // def _WIN32
	if(YSOCKET_SOCKET_ERROR != iRes)
	{
		char *pchAddr = inet_ntoa(sockAddrSrv.sin_addr);
		if(0 != pchAddr)
		{
			m_sLocalAddr = pchAddr;
		}
	}
	// Set "Remote" address.
	if(0 != pchRemoteAddr)
	{
		m_sRemoteAddr = pchRemoteAddr;
	}
	else // (0 == pchRemoteAddr)
	{
		memset(&sockAddrSrv, 0, sizeof(sockaddr_in));
		iSockAddrLen = sizeof(sockAddrSrv);
#ifdef _WIN32
		iRes = getpeername(m_ySocket->h, (struct sockaddr*)&sockAddrSrv, &iSockAddrLen);
#else
		iRes = getpeername(m_ySocket->h, (struct sockaddr*)&sockAddrSrv, (socklen_t*)&iSockAddrLen);
#endif // def _WIN32
		if(YSOCKET_SOCKET_ERROR != iRes)
		{
			char *pchAddr = inet_ntoa(sockAddrSrv.sin_addr);
			if(0 != pchAddr)
			{
				m_sRemoteAddr = pchAddr;
			}
		}
	}

	iRetCode = 0; // Success.


END_YSocket_ConnectToServer:

	return iRetCode;
}


//=============================================================================
int YSocket::Close()
{
	bClosed = true;
	int iRes = StartUseSOCKET();
	if(0 != iRes)
	{
		return 0;
	}

	if(YSOCKET_INVALID_SOCKET != m_ySocket->h)
	{
		shutdown(m_ySocket->h, YSOCKET_SHUTDOWN_SOCKET_READ_WRITE);
#ifdef _WIN32
		closesocket(m_ySocket->h);
#else
		close(m_ySocket->h);
#endif // def _WIN32
		m_ySocket->h = YSOCKET_INVALID_SOCKET;
	}
	m_sLocalAddr  = "";
	m_sRemoteAddr = "";
	return 0;
}


//=============================================================================
int YSocket::WriteByte(unsigned char byVal)
{
	return WriteBytes((char*)&byVal, sizeof(unsigned char));
}
//=============================================================================
int YSocket::WriteInt(int iVal)
{
	return WriteBytes((const char*)&iVal, sizeof(int));
}
//=============================================================================
int YSocket::WriteDouble(double dVal)
{
	return WriteBytes((const char*)&dVal, sizeof(double));
}
//=============================================================================
int YSocket::WriteStr(const char *pchStr)
{
	int iSize = strlen(pchStr) + 1;
	int iRes = WriteInt(iSize);
	if(0 == iRes)
	{
		iRes = WriteBytes(pchStr, iSize);
	}
	return  iRes;
}
//=============================================================================
int YSocket::WriteCharStr(const char *pchStr)
{
	int iSize = strlen(pchStr);
	int iRes = WriteInt(iSize);
	if(0 == iRes)
	{
		iRes = WriteBytes(pchStr, iSize);
	}
	return  iRes;
}
//=============================================================================
int YSocket::WriteBytesArr(int iSize, const char *pbyVal)
{
	return WriteBytes(pbyVal, iSize);
}


//=============================================================================
int YSocket::ReadByte(unsigned char &r_byVal)
{
	return ReadBytes((char*)&r_byVal, sizeof(unsigned char));
}
//=============================================================================
int YSocket::ReadInt(int& r_iVal)
{
	return ReadBytes((char*)&r_iVal, sizeof(int));
}
//=============================================================================
int YSocket::ReadDouble(double& r_dVal)
{
	return ReadBytes((char*)&r_dVal, sizeof(double));
}
//=============================================================================
int YSocket::ReadStr(char* &r_pchStr)
{
	int iRetCode = -1; // Failed.
	r_pchStr = 0;

	int iSize = 0;
	int iRes = 0;

	iRes = ReadInt(iSize);
		YSOCKET_CHECK_RESULT(iRes, END_YSocket_ReadStr)
	//
	// At least 1 (null-term) symbol must be.
	iRes = (iSize <= 0 ? -1 : 0);
		YSOCKET_CHECK_RESULT(iRes, END_YSocket_ReadStr)

	r_pchStr = new char[iSize];
	memset(r_pchStr, 0, iSize);
	iRes = ReadBytes(r_pchStr, iSize);
		YSOCKET_CHECK_RESULT(iRes, END_YSocket_ReadStr)
	//
	// Last symbol must be is null-term.
	iRes = ('\0' != r_pchStr[iSize-1] ? -1 : 0);
		YSOCKET_CHECK_RESULT(iRes, END_YSocket_ReadStr)

	iRetCode = 0; // Success.

END_YSocket_ReadStr:

	if(0 != iRetCode)
	{
		if(0 != r_pchStr)
		{
			delete[] r_pchStr;
			r_pchStr = 0;
		}
	}

	return iRetCode;
}
//=============================================================================
int YSocket::ReadCharStr(char* &r_pchStr)
{
	int iRetCode = -1; // Failed.
	r_pchStr = 0;

	int iSize = 0;
	int iRes = 0;

	iRes = ReadInt(iSize);
		YSOCKET_CHECK_RESULT(iRes, END_YSocket_ReadCharStr)
	//
	iRes = (iSize < 0 ? -1 : 0);
		YSOCKET_CHECK_RESULT(iRes, END_YSocket_ReadCharStr)
	iSize++;

	r_pchStr = new char[iSize];
	memset(r_pchStr, 0, iSize);
	iRes = ReadBytes(r_pchStr, iSize-1);
		YSOCKET_CHECK_RESULT(iRes, END_YSocket_ReadCharStr)

	iRetCode = 0; // Success.

END_YSocket_ReadCharStr:

	if(0 != iRetCode)
	{
		if(0 != r_pchStr)
		{
			delete[] r_pchStr;
			r_pchStr = 0;
		}
	}

	return iRetCode;
}
//=============================================================================
int YSocket::ReadBytesArr(int iSize, char *pbyVal)
{
	return ReadBytes(pbyVal, iSize);
}


//=============================================================================
YSOCKET_HANDLE YSocket::GetHANDLE()
{
  return *m_ySocket;
}


//=============================================================================
int YSocket::StartUseSOCKET()
{
	if(1 == m_iInitUseSocket)
	{
		return 0;
	}

	int iRetCode = -1; // Failed.

#ifdef _WIN32
	// Initiate use of Ws2_32.dll (ver2.2) by the process.
	WORD wVersionRequested = MAKEWORD(2, 2);
	WSADATA wsaData;
	int iRes = ::WSAStartup(wVersionRequested, &wsaData);
	if(0 == iRes)
	{
		// Confirm that the WinSock DLL supports ver2.2.
		if( LOBYTE( wsaData.wVersion ) == 2 &&
		    HIBYTE( wsaData.wVersion ) == 2 )
		{
			iRetCode = 0; // Success.
		}
	}
#else
	iRetCode = 0; // Success.
#endif // def _WIN32

	m_iInitUseSocket = (0 == iRetCode ? 1 : 0);

	return iRetCode;
}


//=============================================================================
int YSocket::EndUseSOCKET()
{
	if(1 == m_iInitUseSocket)
	{
#ifdef _WIN32
		::WSACleanup();
#endif // def _WIN32
		m_iInitUseSocket = 0;
	}
	return 0;
}


//=============================================================================
int YSocket::WriteBytes(const char *pchBuff, int iBuffSize)
{
	int iRetCode = -1; // Failed.
	int iRes = 0;
	int iSendBytes = 0;
	int iCnt = iBuffSize;
	int iWhile = 1;

	fd_set allfds;
	fd_set writefds;
	timeval timeout;

	iRes = StartUseSOCKET();
	YSOCKET_CHECK_RESULT(iRes, END_YSocket_WriteBytes);

	YSOCKET_CHECK_YSOCKET(m_ySocket->h, END_YSocket_WriteBytes);

	FD_ZERO(&allfds);
	FD_SET((unsigned int)m_ySocket->h, &allfds);

	iSendBytes = 0;
	iWhile = 1;
	while(1 == iWhile)
	{
		iCnt -= iSendBytes;
		if(0 == iCnt)
		{
			break;
		}

		writefds = allfds;
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		iRes = select(m_ySocket->h+1, 0, &writefds, 0, &timeout);
		{
			// "select failed"
			iRetCode = -2;
			YSOCKET_CHECK_SOCKET_RESULT(iRes, END_YSocket_WriteBytes)
		}
		if(0 == iRes)
		{
			// Timeout expired.
			// "Connection died"
			iRetCode = -3;
			goto END_YSocket_WriteBytes;
		}

		if( ! FD_ISSET(m_ySocket->h, &writefds))
		{
			// "Wrong socket"
			iRetCode = -4;
			goto END_YSocket_WriteBytes;
		}

		iSendBytes = send(m_ySocket->h, (pchBuff + iBuffSize - iCnt), iCnt, 0);
		{
			// "send failed"
			iRetCode = -5;
			YSOCKET_CHECK_SOCKET_RESULT(iSendBytes, END_YSocket_WriteBytes)
		}
	}

	iRetCode = 0; // Success.

END_YSocket_WriteBytes:

	return iRetCode;
}


//=============================================================================
int YSocket::ReadBytes(char *pchBuff, int iBuffSize)
{
	int iRetCode = -1; // Failed.
	int iRes = 0;
	int iRecvBytes = 0;
	int iCnt = iBuffSize;
	int iWhile = 1;

	fd_set allfds;
	fd_set readfds;
	timeval timeout;

	iRes = StartUseSOCKET();
	YSOCKET_CHECK_RESULT(iRes, END_YSocket_ReadBytes);

	YSOCKET_CHECK_YSOCKET(m_ySocket->h, END_YSocket_ReadBytes);

	FD_ZERO(&allfds);
	FD_SET((unsigned int)m_ySocket->h, &allfds);

	iRecvBytes = 0;
	iWhile = 1;
	while(1 == iWhile)
	{
		iCnt -= iRecvBytes;
		if(0 == iCnt)
		{
			break;
		}

		readfds = allfds;
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		iRes = select(m_ySocket->h+1, &readfds, 0, 0, &timeout);
		{
			// "select failed"
			iRetCode = -2;
			YSOCKET_CHECK_SOCKET_RESULT(iRes, END_YSocket_ReadBytes)
		}
		if(0 == iRes)
		{
			// Timeout expired.
			// "Connection died"
			iRetCode = -3;
			goto END_YSocket_ReadBytes;
		}

		if( ! FD_ISSET(m_ySocket->h, &readfds))
		{
			// "Wrong socket"
			iRetCode = -4;
			goto END_YSocket_ReadBytes;
		}

		iRecvBytes = recv(m_ySocket->h, pchBuff + iBuffSize - iCnt, iCnt, 0);
		{
			// "recv failed"
			iRetCode = -5;
			YSOCKET_CHECK_SOCKET_RESULT(iRecvBytes, END_YSocket_ReadBytes)
		}
		if(0 == iRecvBytes)
		{
			// "Connection has been closed"
			iRetCode = -6;
			goto END_YSocket_ReadBytes;
		}
	}

	iRetCode = 0; // Success.


END_YSocket_ReadBytes:

	return iRetCode;
}
