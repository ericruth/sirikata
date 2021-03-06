/*  Sirikata Network Utilities
 *  TCPStream.cpp
 *
 *  Copyright (c) 2009, Daniel Reiter Horn
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are
 *  met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name of Sirikata nor the names of its contributors may
 *    be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "util/Standard.hh"
#include "util/AtomicTypes.hpp"

#include "TCPDefinitions.hpp"
#include "TCPStream.hpp"
#include "util/ThreadSafeQueue.hpp"
#include "ASIOSocketWrapper.hpp"
#include "MultiplexedSocket.hpp"
#include "TCPSetCallbacks.hpp"
#include <boost/thread.hpp>
namespace Sirikata { namespace Network {

using namespace boost::asio::ip;
TCPStream::TCPStream(const std::tr1::shared_ptr<MultiplexedSocket>&shared_socket,const Stream::StreamID&sid):mSocket(shared_socket),mID(sid),mSendStatus(new AtomicValue<int>(0)) {

}

void TCPStream::send(const Chunk&data, StreamReliability reliability) {
    MultiplexedSocket::RawRequest toBeSent;
    // only allow 3 of the four possibilities because unreliable ordered is tricky and usually useless
    switch(reliability) {
      case Unreliable:
        toBeSent.unordered=true;
        toBeSent.unreliable=true;
        break;
      case ReliableOrdered:
        toBeSent.unordered=false;
        toBeSent.unreliable=false;
        break;
      case ReliableUnordered:
        toBeSent.unordered=true;
        toBeSent.unreliable=false;
        break;
    }
    toBeSent.originStream=getID();
    uint8 serializedStreamId[StreamID::MAX_SERIALIZED_LENGTH];
    unsigned int streamIdLength=StreamID::MAX_SERIALIZED_LENGTH;
    unsigned int successLengthNeeded=toBeSent.originStream.serialize(serializedStreamId,streamIdLength);
    ///this function should never return something larger than the  MAX_SERIALIZED_LEGNTH
    assert(successLengthNeeded<=streamIdLength);
    streamIdLength=successLengthNeeded;
    size_t totalSize=data.size();
    totalSize+=streamIdLength;
    uint30 packetLength=uint30(totalSize);
    uint8 packetLengthSerialized[uint30::MAX_SERIALIZED_LENGTH];
    unsigned int packetHeaderLength=packetLength.serialize(packetLengthSerialized,uint30::MAX_SERIALIZED_LENGTH);
    //allocate a packet long enough to take both the length of the packet and the stream id as well as the packet data. totalSize = size of streamID + size of data and
    //packetHeaderLength = the length of the length component of the packet
    toBeSent.data=new Chunk(totalSize+packetHeaderLength);
    
    uint8 *outputBuffer=&(*toBeSent.data)[0];
    std::memcpy(outputBuffer,packetLengthSerialized,packetHeaderLength);
    std::memcpy(outputBuffer+packetHeaderLength,serializedStreamId,streamIdLength);
    if (data.size())
        std::memcpy(&outputBuffer[packetHeaderLength+streamIdLength],
                    &data[0],
                    data.size());
    bool didsend=false;
    //indicate to other would-be TCPStream::close()ers that we are sending and they will have to wait until we give up control to actually ack the close and shut down the stream
    unsigned int sendStatus=++(*mSendStatus);
    if ((sendStatus&(3*SendStatusClosing))==0) {///max of 3 entities can close the stream at once (FIXME: should implement |= on atomic ints), but as of now at most the recv thread the sender responsible and a user close() is all that is allowed at once...so 3 is fine)
        MultiplexedSocket::sendBytes(mSocket,toBeSent);
        didsend=true;
    }
    //relinquish control to a potential closer
    --(*mSendStatus);
    if (!didsend) {
        //if the data was not sent, its our job to clean it up
        delete toBeSent.data;
        SILOG(tcpsst,debug,"printing to closed stream id "<<getID().read());
    }
}
///This function waits on the sendStatus clearing up so no outstanding sends are being made (and no further ones WILL be made cus of the SendStatusClosing flag that is on
void TCPStream::closeSendStatus(AtomicValue<int>&vSendStatus) {
    int sendStatus=vSendStatus.read();
    bool incd=false;
    if ((sendStatus&(SendStatusClosing*3))==0) {
        ///FIXME we want to |= here        
        vSendStatus+=SendStatusClosing;
        incd=true;
    }
    //Wait until it's a pure sendStatus value without any 'remainders' caused by outstanding sends
    while ((sendStatus=vSendStatus.read())!=SendStatusClosing&&
           sendStatus!=2*SendStatusClosing&&
           sendStatus!=3*SendStatusClosing) {
        
    }
}
void TCPStream::close() {
    //set the stream closed as soon as sends are done
    closeSendStatus(*mSendStatus);
    //obliterate all incoming callback to this stream
    mSocket->addCallbacks(getID(),NULL);
    //send out that the stream is now closed on all sockets
    MultiplexedSocket::closeStream(mSocket,getID());
}
TCPStream::TCPStream(IOService&io):mIO(&io),mSendStatus(new AtomicValue<int>(0)) {
}
void TCPStream::connect(const Address&addy,
                        const SubstreamCallback &substreamCallback,
                        const ConnectionCallback &connectionCallback,
                        const BytesReceivedCallback&bytesReceivedCallback) {
    mSocket=MultiplexedSocket::construct(mIO,substreamCallback);
    *mSendStatus=0;
    mID=StreamID(1);
    mSocket->addCallbacks(getID(),new Callbacks(connectionCallback,
                                                bytesReceivedCallback,
                                                mSendStatus));
    mSocket->connect(addy,3);
}
Stream* TCPStream::factory() {
    return new TCPStream(*mIO);
}
bool TCPStream::cloneFrom(Stream*otherStream,
                          const ConnectionCallback &connectionCallback,
                          const BytesReceivedCallback&bytesReceivedCallback) {
    TCPStream * toBeCloned=dynamic_cast<TCPStream*>(otherStream);
    if (NULL==toBeCloned)
        return false;
    mSocket=toBeCloned->mSocket;
    if (!mSocket) {
        return false;
    }
    StreamID newID=mSocket->getNewID();
    mID=newID;
    //check from addCallbacks if the socket is already disconnected--if so let the user know
    return mSocket->addCallbacks(newID,new Callbacks(connectionCallback,
                                                     bytesReceivedCallback,
                                                     mSendStatus))!=MultiplexedSocket::DISCONNECTED;
}


}  }
