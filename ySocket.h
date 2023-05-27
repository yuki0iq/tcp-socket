#ifndef YSOCKET_H__EDE4ADBD_0FD7_4b88_894D_C8460323A912__INCLUDED
#define YSOCKET_H__EDE4ADBD_0FD7_4b88_894D_C8460323A912__INCLUDED

#include <string>
#ifndef _WIN32
	#include <pthread.h>
#endif // ndef _WIN32


//=============================================================================
struct YSOCKET_HANDLE;

//=============================================================================
class YSocket
{
public:
	YSocket();
	~YSocket();

public:
	int CreateServer(unsigned short uPort, const char *pchLocalAddr = 0);

	// AcceptClientConnection accept connection during iWaitSec seconds.
	//        0: do accept till success.
	//        N: do accept during N seconds.
	//
	int AcceptClientConnection(YSocket *pySocketClient, int iWaitSec = 0);

	int CreateClient();

	// ConnectToServer connect to pchRemoteAddr (if 0 - localHost) server during
	// iWaitSec seconds:
	//        0: do connect till success.
	//        N: do connect during N seconds.
	//
	int ConnectToServer(unsigned short uPort, const char *pchRemoteAddr = 0, int iWaitSec = 0);

	int Close();

	// RETURN VALUE:
	//   true : closed or never opened
	//   false: opened
	inline bool isClosed() { return bClosed; }

public:
	inline const std::string& GetLocalAddress() { return m_sLocalAddr; }
	inline const std::string& GetRemoteAddress() { return m_sRemoteAddr; }

public:
	// Write char.
	int WriteByte(unsigned char byVal);
	// Write int.
	int WriteInt(int iVal);
	// Write double.
	int WriteDouble(double dVal);
	// Write including null-term str.
	int WriteStr(const char *pchStr);
	// Write char-str (excluding null-term).
	int WriteCharStr(const char *pchStr);
	// Write array of BYTEs.
	int WriteBytesArr(int iSize, const char *pbyVal);

public:
	// Read char.
	int ReadByte(unsigned char &r_byVal);
	// Read int.
	int ReadInt(int& r_iVal);
	// Read double.
	int ReadDouble(double& r_dVal);
	// Read including null-term str. The returned str contain null-term. Caller has to free memory with delete[].
	int ReadStr(char* &r_pchStr);
	// Read char-str. The returned str contain null-term. Caller has to free memory with delete[].
	int ReadCharStr(char* &r_pchStr);
	// Read array of BYTEs. The returned pointer must be freed with delete[].
	int ReadBytesArr(int iSize, char *pbyVal);

public:
	YSOCKET_HANDLE GetHANDLE();

private:
	int StartUseSOCKET();
	int EndUseSOCKET();
	int WriteBytes(const char *pchBuff, int iBuffSize);
	int ReadBytes(char *pchBuff, int iBuffSize);
	bool bClosed; //same as isClosed() func

private:
	int m_iInitUseSocket;
	YSOCKET_HANDLE* m_ySocket;
	std::string m_sLocalAddr;
	std::string m_sRemoteAddr; // Used in socket created with CreateClient.
};


#endif // ndef YSOCKET_H__EDE4ADBD_0FD7_4b88_894D_C8460323A912__INCLUDED
