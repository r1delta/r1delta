#ifndef SCRON_H
#define SCRON_H

#include <string>

#ifdef _WIN32
#include <winsock2.h>
typedef SOCKET socket_t;
#define INVALID_SOCKET_VALUE INVALID_SOCKET
#else
typedef int socket_t;
#define INVALID_SOCKET_VALUE -1
#endif

#define SERVERDATA_AUTH 3
#define SERVERDATA_EXECCOMMAND 2
#define SERVERDATA_AUTH_RESPONSE 2
#define SERVERDATA_RESPONSE_VALUE 0

#define SRCON_DEFAULT_TIMEOUT 4
#define SRCON_HEADER_SIZE 14
#define SRCON_SLEEP_THRESHOLD 1024
#define SRCON_SLEEP_MILLISECONDS 500

struct srcon_addr
{
	std::string addr;
	int port;
	std::string pass;
};

class srcon
{
	const srcon_addr addr;
	socket_t sockfd;
	unsigned int id;
	bool connected;

public:
	srcon(const srcon_addr addr, const int timeout = SRCON_DEFAULT_TIMEOUT);
	srcon(const std::string address, const int port, const std::string password, const int timeout = SRCON_DEFAULT_TIMEOUT);
	virtual ~srcon();

	std::string send(const std::string message, int type = SERVERDATA_EXECCOMMAND);
    inline bool set_password(const std::string& password)  
    {  
       const_cast<std::string&>(addr.pass) = password;  
       if (connected)  
       {  
           send(addr.pass, SERVERDATA_AUTH);  
           unsigned char buffer[SRCON_HEADER_SIZE];  
           ::recv(sockfd, (char*)buffer, SRCON_HEADER_SIZE, SERVERDATA_RESPONSE_VALUE);  
		   return true;
       }  
	   return false;
    }

	inline void set_addr(const srcon_addr& address)
	{
		const_cast<srcon_addr&>(addr) = address;
		if (connected)
		{
			send(addr.pass, SERVERDATA_AUTH);
			unsigned char buffer[SRCON_HEADER_SIZE];
			::recv(sockfd, (char*)buffer, SRCON_HEADER_SIZE, SERVERDATA_RESPONSE_VALUE);
		}
	}

	inline bool get_connected() const
	{
		return connected;
	}

	inline bool is_connected() const
	{
		return get_connected();
	}

	inline srcon_addr get_addr() const
	{
		return addr;
	}

private:
	bool connect(int timeout = SRCON_DEFAULT_TIMEOUT) const;
	std::string recv(unsigned long) const;
	size_t read_packet_len() const;
	void pack(unsigned char packet[], std::string data, int packet_len, int id, int type) const;
	unsigned char *read_packet(unsigned int &, bool &) const;
	size_t byte32_to_int(unsigned char *) const;
};

#endif
