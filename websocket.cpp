#include "websocket.h"

#include <string>
#include <stdio.h>

#include <regex>

using namespace remi::server;
using namespace remi;

remi_thread_result WebsocketClientInterface_threadEntry( remi_thread_param param ){
	((WebsocketClientInterface*)param)->_run();
	return 0;
}

WebsocketClientInterface::WebsocketClientInterface( remi_socket clientSock , struct sockaddr_in clientAddr ){
    _sock = clientSock;

    _stopFlag = false;
	_handshakeDone = false;

	_t = remi_createThread( (remi_thread_callback)&WebsocketClientInterface_threadEntry , this );

	_secondsSinceLastPing = remi_timestamp();
}

void WebsocketClientInterface::stop(){
	_stopFlag = true;
}

void* WebsocketClientInterface::_run(){
    //here the recv send is managed
    //  at first glance, the handshake have to be performed
    //  after that, new messages an managed in on_message
    while( !_stopFlag ){
        
		/*char buffer[64] = {0};

		int recv_bytes = recv( _sock , buffer , 64 , 0 );

		if( recv_bytes < 1 )
			continue;

		if( recv_bytes == SOCKET_ERROR )
            break;

		std::cout << buffer;*/

		if( !_handshakeDone ){
			handshake();
		} else {
			if( !readNextMessage() ){
				_handshakeDone = false;
				std::cout << "ws: read next message has failed";
			}
		}
    }

	return NULL;
}

bool WebsocketClientInterface::readNextMessage(){
	bool fin = false;
	unsigned char opcode = 0; //opcode==0 indicates continuationFrame
	std::list<char*> chunks;
	std::list<unsigned long long> sizes;
	unsigned long long entireMsgLen = 0;
	while (opcode==0){
		char length_buf[8] = { 0 };
		std::cout << "recv: " << "new msg" << endl;
		if (recv(_sock, length_buf, 2, 0) != 2){
			std::cout << "recv failed: " << "first two bytes not recv" << endl;
			return false;
		}
		fin = length_buf[0] & 1;
		opcode = (length_buf[0]>>4) & 0xf ;

		unsigned long long payload_len = 0;
		unsigned int _l = length_buf[1] & 127;
		payload_len = _l;

		bool mask_enable = (length_buf[1] & (1 << 7)) != 0;
		std::cout << "recv: " << "payload_len" << payload_len << endl;
		// manage payload len 126 / 127
		if (payload_len == 126){
			if (recv(_sock, length_buf, 2, 0) != 2){
				std::cout << "recv failed: " << "length_buf, 2" << endl;
				return false;
			}

			payload_len = 0;
			int _i = 0;
			payload_len += length_buf[_i++]; payload_len = (payload_len << 8);
			payload_len += length_buf[_i++];
			std::cout << "payload 127: " << payload_len << "      length_buf: " << length_buf[_i++] << " " << length_buf[_i++] << endl;

		}
		else if (payload_len == 127){
			if (recv(_sock, length_buf, 8, 0) != 8){
				std::cout << "recv failed: " << "length_buf, 8" << endl;
				return false;
			}

			payload_len = 0;
			int _i = 0;
			payload_len += (unsigned char)length_buf[_i++]; payload_len = (payload_len << 8);
			payload_len += (unsigned char)length_buf[_i++]; payload_len = (payload_len << 8);
			payload_len += (unsigned char)length_buf[_i++]; payload_len = (payload_len << 8);
			payload_len += (unsigned char)length_buf[_i++]; payload_len = (payload_len << 8);
			payload_len += (unsigned char)length_buf[_i++]; payload_len = (payload_len << 8);
			payload_len += (unsigned char)length_buf[_i++]; payload_len = (payload_len << 8);
			payload_len += (unsigned char)length_buf[_i++]; payload_len = (payload_len << 8);
			payload_len += (unsigned char)length_buf[_i++];
			_i = 0;
			std::cout << "payload 127: " << payload_len << "      length_buf: " <<
				length_buf[_i++] << " "
				<< length_buf[_i++] << " "
				<< length_buf[_i++] << " "
				<< length_buf[_i++] << " "
				<< length_buf[_i++] << " "
				<< length_buf[_i++] << " "
				<< length_buf[_i++] << " "
				<< length_buf[_i++] << endl;
		}

		char mask[4];
		std::cout << "recv: " << "mask enb:" << mask_enable << endl;
		if (mask_enable){
			if (recv(_sock, mask, 4, 0) != 4){
				std::cout << "recv failed: " << "mask recv not received" << endl;
				return false;
			}
		}

		char *buf = new char[payload_len + 1];
		std::memset(buf, 0, payload_len + 1);

		size_t _rv = -1;
		if ((_rv = recv(_sock, buf, payload_len, 0)) != (size_t)payload_len){
			std::cout << "recv failed: " << "recv size mismatch" << endl;
			delete[] buf;
			return false;
		}

		for (size_t l = 0; l < _rv; l++){
			buf[l] = buf[l] ^ mask[l % 4];
		}

		chunks.push_back(buf);
		sizes.push_back(_rv);
		entireMsgLen += _rv;
		//delete buf;
	}
	char* entireMsg = new char[entireMsgLen];
	unsigned long long _offset = 0;
	for (char* _pointer : chunks){
		memcpy(&entireMsg[_offset], _pointer, utils::list_at(sizes, 0));
		_offset += utils::list_at(sizes, 0);
		sizes.pop_front();
		delete[] _pointer;
	}

	char* convertedMessage;
	unsigned long long convertedMessageLen = 0;
	remi::utils::url_decode(entireMsg, entireMsgLen, convertedMessage, &convertedMessageLen);
	on_message( convertedMessage, convertedMessageLen );
	
	delete[] entireMsg;
	delete[] convertedMessage;

	return true;
}

void WebsocketClientInterface::handshake(){

	char buffer[1024] = {0};

	recv( _sock , buffer , 1024 , 0 );

	std::string sbuff = buffer;

	std::list<std::string> sec_pieces = utils::split( sbuff , "Sec-WebSocket-Key: " );
	if( sec_pieces.size() != 2 )
		return;

	std::list<std::string> pieces = utils::split( utils::list_at( sec_pieces, 1 ), "\r\n" );
	if( pieces.size() < 1 )
		return;

	std::string key = utils::list_at( pieces, 0 );
	key.append( "258EAFA5-E914-47DA-95CA-C5AB0DC85B11" );

	unsigned char key_sha1[64] = {0};

	std::string sha1_1(remi::utils::SHA1(key));

	std::string b64 = base64_encode((unsigned char*)sha1_1.c_str(), sha1_1.length());

	std::ostringstream response_s;
	response_s 
		<< "HTTP/1.1 101 Switching Protocols\r\n"
		<< "Upgrade: websocket\r\n"
		<< "Connection: Upgrade\r\n"
		<< "Sec-WebSocket-Accept: " << b64 << "\r\n\r\n";/* % digest.decode("utf-8");*/
	std::string response = response_s.str();

	send( _sock , response.c_str() , response.length() , 0 );

	_handshakeDone = true;
}


Dictionary<Event::PARAM*>	WebsocketClientInterface::parseParams(const char* paramString, unsigned long len){
	/*std::smatch match;
	std::string out = paramString;*/
	Dictionary<Event::PARAM*> ret;

	unsigned long long __start = 0;
	unsigned long long __pipe = 0;
	unsigned long long __eq = 0;
	unsigned long long dataLen = 0;

	__pipe = utils::searchIndexOf(paramString, '|', len, 0)-1;
	__eq = utils::searchIndexOf(paramString, '=', len, __pipe)-1;
	while (__start < __pipe && __pipe < __eq){
		std::string sdataLen(&paramString[__start], __pipe - __start);
		dataLen = atoll(sdataLen.c_str());
		if (dataLen > 0){
			unsigned long long fieldNameLen = __eq - __pipe - 1;
			unsigned long long fieldDataLen = dataLen - __eq + __pipe;
			std::string fieldName; fieldName.assign(&paramString[__pipe + 1], fieldNameLen);

			char* fieldData = new char[dataLen - __eq + __pipe];
			memcpy(fieldData, &paramString[__eq + 1], fieldDataLen);
			
			Event::PARAM* pValue = new Event::PARAM(fieldData, fieldDataLen);
			ret[fieldName] = pValue;

		}
		__start = __pipe + 2 + dataLen;
		__pipe = utils::searchIndexOf(paramString, '|', len, __start)-1;  //paramString.find_first_of("|", __start);
		__eq = utils::searchIndexOf(paramString, '=', len, __pipe)-1;  //paramString.find_first_of("=", __pipe);
	}
	
	return ret;
}

void WebsocketClientInterface::on_message( const char* message, unsigned long long len ){

	//std::cout << "ws: " << message;

	if( message == "pong" )
		return;

	send_message("ack");
	
	int _slashOffset1 = utils::searchIndexOf(message, '/', len, 0);
	int _slashOffset2 = utils::searchIndexOf(message, '/', len, _slashOffset1);
	int _slashOffset3 = utils::searchIndexOf(message, '/', len, _slashOffset2);

	if (_slashOffset3>_slashOffset2 && _slashOffset2>_slashOffset1){ // msgtype,widget,function,params

		if ( memcmp(message, "callback", _slashOffset1-1)==0 ){
			
			std::string s_widget_id; s_widget_id.assign(&message[_slashOffset1], _slashOffset2 - _slashOffset1-1);

			std::string function_name; function_name.assign(&message[_slashOffset2], _slashOffset3 - _slashOffset2-1);

			std::cout << "ws: call id = " <<  s_widget_id << "." << function_name << std::endl;

			int widget_id;
			if( utils::sscan( s_widget_id , "%d" , &widget_id  ) != 1 )
				return;

			

			Widget* widget = (Widget*)( (void*)widget_id );

			Event* event = new Event( function_name );
			event->source = widget;
			
			if (_slashOffset3 < len) //so there is a last chunk
				event->params = parseParams( &message[_slashOffset3], len-_slashOffset3 );

			//widget->propagate( event );
			widget->onEvent( event->name , event );

			for (std::string key: event->params.keys()){
				delete (event->params.get(key));
			}

			delete event;
		}

	}

}

void WebsocketClientInterface::send_message( std::string message){

	size_t message_length = message.length();

	// Prepare the buffer length for the maximum size condition
	// +1 byte for fix 129
	// +1 for length
	// +8 for extreme payload condition
	// buffer_length will be decreased later
	size_t buffer_length = message_length +1 +1 +8;

	unsigned char* buf = new unsigned char[ buffer_length ];

	unsigned char* lpmsg = &buf[0];

	*lpmsg = 129;
	lpmsg++;

	if( message_length <= 125 ) {

		*lpmsg = message_length;
		lpmsg++;
		buffer_length -= 8;

	} else if( message_length >= 126 && message_length <= 65535 ){

		buffer_length -= 6;
		*lpmsg = 126;lpmsg++;
		unsigned short _m = message_length;
		lpmsg[0] = (_m >> 8) & 0xff;
		lpmsg[1] = (_m >> 0) & 0xff;
		lpmsg += 2;

	} else {

		*lpmsg = 127;lpmsg++;
		unsigned long long int _m = message_length;
		memcpy( lpmsg , &_m , 8 );
		lpmsg += 8;

	}

	memcpy( lpmsg , message.c_str() , message_length );

	send( _sock , (const char*)buf, buffer_length, 0 );

	delete[] buf;
	
}



remi_thread_result WebsocketServer_threadEntry( remi_thread_param WebsocketServerInstance ){
	((WebsocketServer*)WebsocketServerInstance)->_listenAsync(NULL);
	return 0;
}

WebsocketServer::WebsocketServer( int port ){
    _port = port;
    _stopFlag = false;

    _socketFd = socket(AF_INET, SOCK_STREAM, 0);

    if(_socketFd < 0){
        //cerr << "WebsocketServer::WebsocketServer - cannot open socket" << endl;
        //return 0;
		return;
    }
    
    memset((void*) &_address, 0, sizeof(_address));
    _address.sin_family = AF_INET;
	remi_socket_setaddr( _address.sin_addr, INADDR_ANY );
	_address.sin_port = htons(_port);

	if( bind( _socketFd , (SOCKADDR*)&_address, sizeof( SOCKADDR_IN ) ) < 0 ){
		return;
	}

    if( listen( _socketFd , SOMAXCONN ) < 0 ){
        return;
    }
    
    _t = remi_createThread( (remi_thread_callback)&WebsocketServer_threadEntry, (void*)this );
}

void* WebsocketServer::_listenAsync(void* data){

	while( true ){
 
        SOCKADDR_IN         clientSock;
        remi_socket_len		clientLen = sizeof( clientSock );
 
        remi_socket		client = accept( _socketFd , (SOCKADDR*)&clientSock , &clientLen );

        if( client != INVALID_SOCKET ){

#ifdef WIN32
			// Clients keys are "[IP_ADDRESS]:[PORT]"
			std::string key = utils::sformat( "%d.%d.%d.%d:%d" , 
				(int)clientSock.sin_addr.S_un.S_un_b.s_b1,
				(int)clientSock.sin_addr.S_un.S_un_b.s_b2,
				(int)clientSock.sin_addr.S_un.S_un_b.s_b3,
				(int)clientSock.sin_addr.S_un.S_un_b.s_b4,
				(int)clientSock.sin_port
			);
#else
			std::string key = utils::sformat( "%s:%d" , 
				inet_ntoa( clientSock.sin_addr ),
				clientSock.sin_port );
#endif

            _clients.set(key , new WebsocketClientInterface( client , clientSock ) );

        }
 
        Sleep( 100 );
	}

	return NULL;
}

void WebsocketServer::sendToAllClients(std::string message){
	//for (std::list<std::string>::iterator key_iterator = _clients.keys().begin(); key_iterator != _clients.keys().end(); key_iterator++){
	for (std::string key : _clients.keys()){
		WebsocketClientInterface* ws = _clients.get(key);

		//a client may die, destroy here the instance of send fails
		//try{
		//cout << message << endl;
			ws->send_message(message.c_str());
		/*}
		catch (Exception e){}*/
	}
}