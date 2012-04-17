#include "packet_receiver.hpp"
#ifndef CODE_INLINE
#include "packet_receiver.ipp"
#endif

#include "network/address.hpp"
#include "network/bundle.hpp"
#include "network/channel.hpp"
#include "network/socket.hpp"
#include "network/event_dispatcher.hpp"
#include "network/network_interface.hpp"

namespace KBEngine { 
namespace Mercury
{
//-------------------------------------------------------------------------------------
PacketReceiver::PacketReceiver(Socket & socket,
	   NetworkInterface & networkInterface	) :
	socket_(socket),
	networkInterface_(networkInterface),
	pNextPacket_(new Packet())
{
}

//-------------------------------------------------------------------------------------
PacketReceiver::~PacketReceiver()
{
}

//-------------------------------------------------------------------------------------
int PacketReceiver::handleInputNotification(int fd)
{
	if(fd == socket_)
	{
		Socket* pNewSocket = socket_.accept();
		if(pNewSocket == NULL){
			WARNING_MSG("PacketReceiver::handleInputNotification: accept socketID(%d) %s!\n",
				 fd, strerror(errno));
			
			this->dispatcher().errorReporter().reportException(
					REASON_GENERAL_NETWORK);
		}
		else
		{
			Channel* pchannel = new Channel(networkInterface_, pNewSocket, Channel::INTERNAL);
			networkInterface_.registerChannel(*pchannel);
		}
	}
	else if (this->processSocket(/*expectingPacket:*/true))
	{
		while (this->processSocket(/*expectingPacket:*/false))
		{
			/* pass */;
		}
	}

	return 0;
}

//-------------------------------------------------------------------------------------
bool PacketReceiver::processSocket(bool expectingPacket)
{
	int len = pNextPacket_->recvFromEndPoint(socket_);
	if (len <= 0)
	{
		return this->checkSocketErrors(len, expectingPacket);
	}

	PacketPtr curPacket = pNextPacket_;
	pNextPacket_ = new Packet();
	Reason ret = this->processPacket(curPacket.get());

	if ((ret != REASON_SUCCESS) &&
			networkInterface_.isVerbose())
	{
		this->dispatcher().errorReporter().reportException(ret, socket_.addr());
	}

	return true;
}

//-------------------------------------------------------------------------------------
bool PacketReceiver::checkSocketErrors(int len, bool expectingPacket)
{
	// is len weird?
	if (len == 0)
	{
		WARNING_MSG("PacketReceiver::processPendingEvents: "
			"Throwing REASON_GENERAL_NETWORK (1)- %s\n",
			strerror(errno));

		this->dispatcher().errorReporter().reportException(
				REASON_GENERAL_NETWORK);

		return true;
	}
		// I'm not quite sure what it means if len is 0
		// (0 => 'end of file', but with dgram sockets?)

#ifdef _WIN32
	DWORD wsaErr = WSAGetLastError();
#endif //def _WIN32

	// is the buffer empty?
	if (
#ifdef _WIN32
		wsaErr == WSAEWOULDBLOCK
#else
		errno == EAGAIN && !expectingPacket
#endif
		)
	{
		return false;
	}

#ifdef unix
	// is it telling us there's an error?
	if (errno == EAGAIN ||
		errno == ECONNREFUSED ||
		errno == EHOSTUNREACH)
	{
#if defined(PLAYSTATION3)
		this->dispatcher().errorReporter().reportException(
				REASON_NO_SUCH_PORT);
		return true;
#else
		Mercury::Address offender;

		if (socket_.getClosedPort(offender))
		{
			// If we got a NO_SUCH_PORT error and there is an internal
			// channel to this address, mark it as remote failed.  The logic
			// for dropping external channels that get NO_SUCH_PORT
			// exceptions is built into BaseApp::onClientNoSuchPort().
			if (errno == ECONNREFUSED)
			{
				// δʵ��
			}

			this->dispatcher().errorReporter().reportException(
					REASON_NO_SUCH_PORT, offender);

			return true;
		}
		else
		{
			WARNING_MSG("PacketReceiver::processPendingEvents: "
				"getClosedPort() failed\n");
		}
#endif
	}
#else
	if (wsaErr == WSAECONNRESET)
	{
		return true;
	}
#endif // unix

	// ok, I give up, something's wrong
#ifdef _WIN32
	WARNING_MSG("PacketReceiver::processPendingEvents: "
				"Throwing REASON_GENERAL_NETWORK - %d\n",
				wsaErr);
#else
	WARNING_MSG("PacketReceiver::processPendingEvents: "
				"Throwing REASON_GENERAL_NETWORK - %s\n",
			strerror(errno));
#endif
	this->dispatcher().errorReporter().reportException(
			REASON_GENERAL_NETWORK);

	return true;
}

//-------------------------------------------------------------------------------------
Reason PacketReceiver::processPacket(Packet * p)
{
	Channel * pChannel = networkInterface_.findChannel(&socket_);

	if (pChannel != NULL)
	{
		pChannel->onPacketReceived(p->totalSize());

		if (pChannel->pFilter())
		{
			return pChannel->pFilter()->recv(*this, p);
		}
	}

	return this->processFilteredPacket(p);
}

//-------------------------------------------------------------------------------------
Reason PacketReceiver::processFilteredPacket(Packet * p)
{
	return REASON_SUCCESS;
}

//-------------------------------------------------------------------------------------
EventDispatcher & PacketReceiver::dispatcher()
{
	return networkInterface_.dispatcher();
}

//-------------------------------------------------------------------------------------
}
}