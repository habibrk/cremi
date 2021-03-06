
#ifndef _REMI_WEBSOCKET__
#define _REMI_WEBSOCKET__

#include "remi.h"
#include "base64.h"
#include "TinySHA1.hpp"
#include <iostream>
using namespace std;

namespace remi {

	namespace server {

		class WebsocketClientInterface{
        
		public:
			WebsocketClientInterface(remi_socket clientSock , struct sockaddr_in clientAddr );
			void* _run();

			void stop();

			void handshake();

			void on_message(const char* message, unsigned long long len);

			void send_message( std::string message);

			bool readNextMessage();
            
		private:

			Dictionary<Event::PARAM*>		parseParams(const char* paramString, unsigned long len);

			remi_thread		_t;

			remi_socket		_sock;

			bool	_handshakeDone;

			bool	_stopFlag;

			int		_secondsSinceLastPing;
            
		};
        
		//Listens asyncronously for client connection and for each client instantiate a WebsocketClientInterface
		class WebsocketServer {
        
		public:

			WebsocketServer( int port );

			void*	_listenAsync(void* data);

			void	stop();

			void	sendToAllClients(std::string message);
            
		private:
			remi_thread _t;

			//key, instance    key should be generated with address and port number
			Dictionary<WebsocketClientInterface*> _clients;

			struct sockaddr_in			_address;

			int				_port;

			remi_socket		_socketFd;

			bool			_stopFlag;

            
		};

	}

}

#endif