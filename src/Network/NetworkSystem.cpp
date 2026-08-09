#include "Network/NetworkSystem.h"
#include "Core/Log.h"
#include <cstring>
#include <cmath>
#include <algorithm>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>

namespace Frost {

NetworkSystem::NetworkSystem() {
    memset(connections_, 0, sizeof(connections_));
    memset(sendBuffer_, 0, sizeof(sendBuffer_));
    memset(deltaStates_, 0, sizeof(deltaStates_));
    memset(reliableCount_, 0, sizeof(reliableCount_));
    memset(fragmentBuffer_, 0, sizeof(fragmentBuffer_));
    memset(fragmentReceived_, 0, sizeof(fragmentReceived_));
    memset(fragmentExpected_, 0, sizeof(fragmentExpected_));
}

NetworkSystem::~NetworkSystem() { shutdown(); }

bool NetworkSystem::init(const NetworkConfig& config) {
    config_ = config;
    socket_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_ < 0) { FROST_LOG_ERROR("[NetworkSystem] Failed to create socket: %d", errno); return false; }
    int flags = fcntl(socket_, F_GETFL, 0);
    fcntl(socket_, F_SETFL, flags | O_NONBLOCK);
    int reuse = 1;
    setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config_.port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(socket_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        FROST_LOG_ERROR("[NetworkSystem] Failed to bind port %u: %d", config_.port, errno);
        close(socket_); socket_ = -1; return false;
    }
    FROST_LOG_INFO("[NetworkSystem] Initialized on port %u (max %u connections)", config_.port, config_.maxConnections);
    return true;
}

void NetworkSystem::shutdown() {
    disconnect();
    if (socket_ >= 0) { close(socket_); socket_ = -1; }
    connectionCount_ = 0;
    FROST_LOG_INFO("[NetworkSystem] Shutdown");
}

void NetworkSystem::update(f32 dt) {
    time_ += dt;
    processReceivedPackets();
    processReliableRetransmission(dt);

    for (u32 i = 0; i < connectionCount_; i++) {
        ConnectionState& conn = connections_[i];
        if (conn.state == ConnectionState::State::Disconnected) continue;
        if (conn.state == ConnectionState::State::Connected) {
            if (time_ - conn.lastRecvTime > config_.connectionTimeout) {
                conn.state = ConnectionState::State::Disconnected;
                if (connectionCallback_) connectionCallback_(i, false, connectionUserData_);
                continue;
            }
            if (time_ - lastKeepAlive_ > config_.keepAliveInterval) lastKeepAlive_ = time_;
        }
        if (conn.state == ConnectionState::State::Connecting) {
            if (time_ - conn.lastSendTime > 1.0f) {
                Packet pkt;
                pkt.header.flags = PacketHeader::FLAG_CONNECT;
                pkt.size = sizeof(PacketHeader);
                memcpy(pkt.data, &pkt.header, sizeof(PacketHeader));
                pkt.address = conn.address;
                sendPacket(i, pkt);
            }
        }
    }

    statsAccum_ += dt;
    if (statsAccum_ >= 1.0f) {
        stats_.bytesPerSecond = statsBytesSent_;
        stats_.packetsPerSecond = statsPacketsSent_;
        stats_.averageRTT = 0; stats_.averageJitter = 0;
        u32 active = 0;
        for (u32 i = 0; i < connectionCount_; i++) {
            if (connections_[i].state == ConnectionState::State::Connected) {
                stats_.averageRTT += connections_[i].rtt;
                stats_.averageJitter += connections_[i].jitter;
                active++;
            }
        }
        if (active > 0) { stats_.averageRTT /= active; stats_.averageJitter /= active; }
        statsAccum_ = 0; statsBytesSent_ = 0; statsPacketsSent_ = 0;
    }
}

bool NetworkSystem::connect(const char* host, u16 port) {
    if (connectionCount_ >= MAX_CONNECTIONS) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) { FROST_LOG_ERROR("[NetworkSystem] Invalid address: %s", host); return false; }
    u32 idx = connectionCount_++;
    ConnectionState& conn = connections_[idx];
    conn.address = (NetworkAddress)(*((u32*)&addr.sin_addr)) << 16 | port;
    conn.state = ConnectionState::State::Connecting;
    conn.lastSendTime = time_;
    conn.lastRecvTime = time_;
    conn.connectTime = time_;
    myConnectionId_ = idx;
    Packet pkt;
    pkt.header.flags = PacketHeader::FLAG_CONNECT;
    pkt.size = sizeof(PacketHeader);
    memcpy(pkt.data, &pkt.header, sizeof(PacketHeader));
    pkt.address = conn.address;
    sendPacket(idx, pkt);
    FROST_LOG_INFO("[NetworkSystem] Connecting to %s:%u", host, port);
    return true;
}

void NetworkSystem::disconnect() {
    for (u32 i = 0; i < connectionCount_; i++) {
        if (connections_[i].state != ConnectionState::State::Disconnected) {
            Packet pkt;
            pkt.header.flags = PacketHeader::FLAG_DISCONNECT;
            pkt.size = sizeof(PacketHeader);
            memcpy(pkt.data, &pkt.header, sizeof(PacketHeader));
            pkt.address = connections_[i].address;
            sendPacket(i, pkt);
            connections_[i].state = ConnectionState::State::Disconnected;
        }
    }
}

bool NetworkSystem::isConnected() const {
    for (u32 i = 0; i < connectionCount_; i++) {
        if (connections_[i].state == ConnectionState::State::Connected) return true;
    }
    return false;
}

bool NetworkSystem::startServer(u16 port) { config_.isServer = true; config_.port = port; return true; }
void NetworkSystem::stopServer() { config_.isServer = false; disconnect(); }

u32 NetworkSystem::clientCount() const {
    u32 count = 0;
    for (u32 i = 0; i < connectionCount_; i++) {
        if (connections_[i].state == ConnectionState::State::Connected) count++;
    }
    return count;
}

void NetworkSystem::sendReliable(u32 connectionId, const u8* data, u32 size) { addToSendQueue(connectionId, data, size, true); }
void NetworkSystem::sendUnreliable(u32 connectionId, const u8* data, u32 size) { addToSendQueue(connectionId, data, size, false); }

void NetworkSystem::broadcastReliable(const u8* data, u32 size) {
    for (u32 i = 0; i < connectionCount_; i++) {
        if (connections_[i].state == ConnectionState::State::Connected) sendReliable(i, data, size);
    }
}

void NetworkSystem::broadcastUnreliable(const u8* data, u32 size) {
    for (u32 i = 0; i < connectionCount_; i++) {
        if (connections_[i].state == ConnectionState::State::Connected) sendUnreliable(i, data, size);
    }
}

void NetworkSystem::setState(u32 connectionId, const u8* state, u32 size) {
    if (connectionId < MAX_CONNECTIONS) deltaStates_[connectionId].update(state, size);
}

bool NetworkSystem::getInterpolatedState(u32 connectionId, f32 time, u8* output, u32& outSize) {
    if (connectionId >= MAX_CONNECTIONS) return false;
    return interpBuffers_[connectionId].interpolate(time, output, outSize);
}

void NetworkSystem::addSnapshot(f32 time, const u8* state, u32 size) { lagBuffer_.addSnapshot(time, state, size); }
bool NetworkSystem::rewindState(f32 time, u8* output, u32& outSize) { return lagBuffer_.rewindTo(time, output, outSize); }

u32 NetworkSystem::computeDelta(const u8* from, u32 fromSize, const u8* to, u32 toSize, u8* delta, u32 deltaMax) {
    if (!from || !to || deltaMax < 4) return 0;
    u32 written = 0;
    delta[written++] = (u8)(fromSize & 0xFF); delta[written++] = (u8)((fromSize >> 8) & 0xFF);
    delta[written++] = (u8)(toSize & 0xFF); delta[written++] = (u8)((toSize >> 8) & 0xFF);
    u32 minSize = (fromSize < toSize) ? fromSize : toSize;
    u32 i = 0;
    while (i < minSize) {
        u32 runStart = i;
        while (i < minSize && from[i] == to[i]) i++;
        u32 skipCount = i - runStart;
        while (skipCount > 0 && written + 2 < deltaMax) {
            u32 chunk = (skipCount > 127) ? 127 : skipCount;
            delta[written++] = (u8)(chunk | 0x80); delta[written++] = (u8)0;
            skipCount -= chunk;
        }
        runStart = i;
        while (i < minSize && (i - runStart) < 127 && from[i] != to[i]) i++;
        u32 changeCount = i - runStart;
        if (changeCount > 0 && written + changeCount + 1 < deltaMax) {
            delta[written++] = (u8)changeCount;
            memcpy(delta + written, to + runStart, changeCount);
            written += changeCount;
        }
    }
    if (toSize > fromSize && written + (toSize - fromSize) + 2 < deltaMax) {
        u32 remain = toSize - fromSize;
        while (remain > 0) {
            u32 chunk = (remain > 127) ? 127 : remain;
            delta[written++] = (u8)chunk;
            u32 offset = written - 4;
            memcpy(delta + written, to + fromSize + offset, chunk);
            written += chunk; remain -= chunk;
        }
    }
    return written;
}

u32 NetworkSystem::applyDelta(const u8* base, u32 baseSize, const u8* delta, u32 deltaSize, u8* output, u32 outputMax) {
    if (!base || !delta || deltaSize < 4 || outputMax < 4) return 0;
    u32 read = 4;
    u32 outPos = 0, basePos = 0;
    while (read < deltaSize && outPos < outputMax) {
        u8 cmd = delta[read++];
        if (cmd & 0x80) {
            u32 skip = cmd & 0x7F;
            for (u32 i = 0; i < skip && outPos < outputMax && basePos < baseSize; i++) output[outPos++] = base[basePos++];
        } else {
            u32 count = cmd;
            for (u32 i = 0; i < count && read < deltaSize && outPos < outputMax; i++) { output[outPos++] = delta[read++]; basePos++; }
        }
    }
    while (basePos < baseSize && outPos < outputMax) output[outPos++] = base[basePos++];
    return outPos;
}

ConnectionState* NetworkSystem::getConnection(u32 connectionId) { return (connectionId < MAX_CONNECTIONS) ? &connections_[connectionId] : nullptr; }
const ConnectionState* NetworkSystem::getConnection(u32 connectionId) const { return (connectionId < MAX_CONNECTIONS) ? &connections_[connectionId] : nullptr; }

void NetworkSystem::processReceivedPackets() {
    if (socket_ < 0) return;
    struct pollfd pfd; pfd.fd = socket_; pfd.events = POLLIN; pfd.revents = 0;
    while (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
        sockaddr_in fromAddr{}; socklen_t fromLen = sizeof(fromAddr);
        ssize_t received = recvfrom(socket_, recvBuffer_, sizeof(recvBuffer_), 0, (sockaddr*)&fromAddr, &fromLen);
        if (received <= 0) break;
        if (received < (ssize_t)sizeof(PacketHeader)) continue;
        NetworkAddress from = (NetworkAddress)(*((u32*)&fromAddr.sin_addr)) << 16 | ntohs(fromAddr.sin_port);
        if (config_.encryptionEnabled) scramblePacket(recvBuffer_, (u32)received);
        processPacket(recvBuffer_, (u32)received, from);
        stats_.totalBytesReceived += (u32)received;
    }
}

void NetworkSystem::processPacket(const u8* data, u32 size, NetworkAddress from) {
    if (size < sizeof(PacketHeader)) return;
    PacketHeader header;
    memcpy(&header, data, sizeof(PacketHeader));
    if (header.flags & PacketHeader::FLAG_CONNECT) { handleConnectionRequest(from, data + sizeof(PacketHeader), size - sizeof(PacketHeader)); return; }
    if (header.flags & PacketHeader::FLAG_DISCONNECT) { handleDisconnect(from); return; }
    if (header.flags & PacketHeader::FLAG_FRAGMENT) { for (u32 i = 0; i < connectionCount_; i++) { if (connections_[i].address == from) { processFragmentedPacket(i, data + sizeof(PacketHeader), size - sizeof(PacketHeader), header); return; } } return; }
    u32 connIdx = 0xFFFFFFFF;
    for (u32 i = 0; i < connectionCount_; i++) { if (connections_[i].address == from) { connIdx = i; break; } }
    if (connIdx == 0xFFFFFFFF) return;
    ConnectionState& conn = connections_[connIdx];
    conn.lastRecvTime = time_;
    conn.packetsReceived++;
    conn.bytesReceived += size;
    processReliability(connIdx, header);
    if (packetCallback_ && size > sizeof(PacketHeader)) packetCallback_(connIdx, data + sizeof(PacketHeader), size - sizeof(PacketHeader), packetUserData_);
    interpBuffers_[connIdx].addSample(time_, data + sizeof(PacketHeader), size - sizeof(PacketHeader));
}

void NetworkSystem::sendPacket(u32 connectionId, Packet& packet) {
    if (socket_ < 0 || connectionId >= MAX_CONNECTIONS) return;
    ConnectionState& conn = connections_[connectionId];
    if (config_.encryptionEnabled) scramblePacket(packet.data, packet.size);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u16)(conn.address & 0xFFFF));
    addr.sin_addr.s_addr = htonl((u32)(conn.address >> 16));
    ssize_t sent = sendto(socket_, packet.data, packet.size, 0, (sockaddr*)&addr, sizeof(addr));
    if (sent > 0) {
        conn.bytesSent += (u32)sent;
        conn.packetsSent++;
        conn.lastSendTime = time_;
        stats_.totalBytesSent += (u32)sent;
        statsBytesSent_ += (u32)sent;
        statsPacketsSent_++;
    }
}

void NetworkSystem::sendAck(u32 connectionId, u16 sequence) {
    Packet pkt;
    pkt.header.flags = PacketHeader::FLAG_ACK;
    pkt.header.ack = sequence;
    pkt.size = sizeof(PacketHeader);
    memcpy(pkt.data, &pkt.header, sizeof(PacketHeader));
    pkt.address = connections_[connectionId].address;
    sendPacket(connectionId, pkt);
}

void NetworkSystem::handleConnectionRequest(NetworkAddress from, const u8* data, u32 size) {
    (void)data; (void)size;
    if (connectionCount_ >= MAX_CONNECTIONS) return;
    for (u32 i = 0; i < connectionCount_; i++) {
        if (connections_[i].address == from && connections_[i].state == ConnectionState::State::Connected) return;
    }
    u32 idx = connectionCount_++;
    ConnectionState& conn = connections_[idx];
    conn.address = from;
    conn.state = ConnectionState::State::Connected;
    conn.lastRecvTime = time_;
    conn.lastSendTime = time_;
    conn.connectTime = time_;
    Packet pkt;
    pkt.header.flags = PacketHeader::FLAG_CONNECT;
    pkt.size = sizeof(PacketHeader);
    memcpy(pkt.data, &pkt.header, sizeof(PacketHeader));
    pkt.address = from;
    sendPacket(idx, pkt);
    if (connectionCallback_) connectionCallback_(idx, true, connectionUserData_);
}

void NetworkSystem::handleConnectionAccept(NetworkAddress from, const u8* data, u32 size) {
    (void)data; (void)size;
    for (u32 i = 0; i < connectionCount_; i++) {
        if (connections_[i].address == from) {
            connections_[i].state = ConnectionState::State::Connected;
            connections_[i].lastRecvTime = time_;
            if (connectionCallback_) connectionCallback_(i, true, connectionUserData_);
            return;
        }
    }
}

void NetworkSystem::handleDisconnect(NetworkAddress from) {
    for (u32 i = 0; i < connectionCount_; i++) {
        if (connections_[i].address == from) {
            connections_[i].state = ConnectionState::State::Disconnected;
            if (connectionCallback_) connectionCallback_(i, false, connectionUserData_);
            if (i != connectionCount_ - 1) connections_[i] = connections_[--connectionCount_];
            return;
        }
    }
}

void NetworkSystem::processReliability(u32 connectionId, const PacketHeader& header) {
    ConnectionState& conn = connections_[connectionId];
    if (header.flags & PacketHeader::FLAG_ACK) {
        conn.updateRTT(time_ - conn.lastSendTime);
        for (u32 i = 0; i < reliableCount_[connectionId]; i++) {
            if (reliableMessages_[connectionId][i].sequence == header.ack) {
                reliableMessages_[connectionId][i].acknowledged = true;
                conn.reliableAcked++;
                break;
            }
        }
        return;
    }
    u16 seq = header.sequence;
    if (seq > conn.remoteSequence) {
        u32 diff = seq - conn.remoteSequence;
        conn.ackBitfield <<= diff;
        conn.ackBitfield |= 1;
        conn.remoteSequence = seq;
    } else {
        u32 diff = conn.remoteSequence - seq;
        if (diff < 32) conn.ackBitfield |= (1 << diff);
    }
    if (header.flags & PacketHeader::FLAG_RELIABLE) sendAck(connectionId, seq);
}

void NetworkSystem::addToSendQueue(u32 connectionId, const u8* data, u32 size, bool reliable) {
    if (connectionId >= MAX_CONNECTIONS) return;
    if (size + sizeof(PacketHeader) > Packet::MAX_SIZE) {
        u32 remaining = size;
        u32 offset = 0;
        u16 fragId = (u16)std::rand();
        u8 fragCount = (u8)((size + Packet::MAX_SIZE - sizeof(PacketHeader) - sizeof(FragmentHeader) - 1) / (Packet::MAX_SIZE - sizeof(PacketHeader) - sizeof(FragmentHeader)));
        for (u8 f = 0; f < fragCount && remaining > 0; f++) {
            u32 fragSize = Mathf::min(remaining, Packet::MAX_SIZE - sizeof(PacketHeader) - sizeof(FragmentHeader));
            Packet pkt;
            pkt.header.flags = PacketHeader::FLAG_FRAGMENT | (reliable ? PacketHeader::FLAG_RELIABLE : 0);
            pkt.header.sequence = connections_[connectionId].localSequence++;
            pkt.header.length = (u16)(sizeof(FragmentHeader) + fragSize);
            FragmentHeader frag;
            frag.id = fragId;
            frag.fragmentIndex = f;
            frag.fragmentCount = fragCount;
            frag.fragmentSize = (u16)fragSize;
            u32 pos = 0;
            memcpy(pkt.data + pos, &pkt.header, sizeof(PacketHeader)); pos += sizeof(PacketHeader);
            memcpy(pkt.data + pos, &frag, sizeof(FragmentHeader)); pos += sizeof(FragmentHeader);
            memcpy(pkt.data + pos, data + offset, fragSize); pos += fragSize;
            pkt.size = pos;
            pkt.address = connections_[connectionId].address;
            sendPacket(connectionId, pkt);
            remaining -= fragSize;
            offset += fragSize;
        }
        return;
    }
    ConnectionState& conn = connections_[connectionId];
    Packet pkt;
    pkt.header.sequence = conn.localSequence++;
    pkt.header.ack = conn.remoteSequence;
    pkt.header.ackBitfield = conn.ackBitfield;
    pkt.header.channel = 0;
    pkt.header.flags = reliable ? PacketHeader::FLAG_RELIABLE : 0;
    pkt.header.length = (u16)size;
    pkt.size = sizeof(PacketHeader) + size;
    memcpy(pkt.data, &pkt.header, sizeof(PacketHeader));
    if (size > 0) memcpy(pkt.data + sizeof(PacketHeader), data, size);
    pkt.address = conn.address;
    pkt.sendTime = time_;
    sendPacket(connectionId, pkt);
    if (reliable && reliableCount_[connectionId] < MAX_RELIABLE_MESSAGES) {
        ReliableMessage& rm = reliableMessages_[connectionId][reliableCount_[connectionId]++];
        rm.sequence = pkt.header.sequence;
        rm.sendTime = time_;
        rm.size = pkt.size;
        memcpy(rm.data, pkt.data, pkt.size);
        rm.acknowledged = false;
        rm.sendCount = 1;
    }
}

void NetworkSystem::processReliableRetransmission(f32 dt) {
    for (u32 c = 0; c < connectionCount_; c++) {
        if (connections_[c].state != ConnectionState::State::Connected) continue;
        f32 retransmitTimeout = connections_[c].rtt * 2.0f + 0.1f;
        retransmitTimeout = Mathf::max(retransmitTimeout, 0.25f);
        for (u32 i = 0; i < reliableCount_[c]; i++) {
            ReliableMessage& rm = reliableMessages_[c][i];
            if (rm.acknowledged) continue;
            if (time_ - rm.sendTime > retransmitTimeout && rm.sendCount < ReliableMessage::MAX_RESENDS) {
                Packet pkt;
                memcpy(pkt.data, rm.data, rm.size);
                pkt.size = rm.size;
                pkt.address = connections_[c].address;
                sendPacket(c, pkt);
                rm.sendCount++;
                rm.sendTime = time_;
            }
        }
    }
}

void NetworkSystem::scramblePacket(u8* data, u32 size) {
    for (u32 i = sizeof(PacketHeader); i < size; i++) {
        data[i] ^= config_.encryptionKey;
    }
}

void NetworkSystem::processFragmentedPacket(u32 connectionId, const u8* data, u32 size, const PacketHeader& header) {
    if (size < sizeof(FragmentHeader)) return;
    FragmentHeader frag;
    memcpy(&frag, data, sizeof(FragmentHeader));
    u32 payloadSize = size - sizeof(FragmentHeader);
    if (frag.fragmentIndex == 0) {
        fragmentReceived_[connectionId] = 0;
        fragmentExpected_[connectionId] = frag.fragmentCount;
        memset(fragmentBuffer_[connectionId], 0, sizeof(fragmentBuffer_[connectionId]));
    }
    u32 offset = frag.fragmentIndex * (Packet::MAX_SIZE - sizeof(PacketHeader) - sizeof(FragmentHeader));
    if (offset + payloadSize <= sizeof(fragmentBuffer_[connectionId])) {
        memcpy(fragmentBuffer_[connectionId] + offset, data + sizeof(FragmentHeader), payloadSize);
        fragmentReceived_[connectionId]++;
    }
    if (fragmentReceived_[connectionId] >= fragmentExpected_[connectionId]) {
        u32 totalSize = frag.fragmentCount * (Packet::MAX_SIZE - sizeof(PacketHeader) - sizeof(FragmentHeader));
        if (packetCallback_) packetCallback_(connectionId, fragmentBuffer_[connectionId], totalSize, packetUserData_);
        fragmentReceived_[connectionId] = 0;
        fragmentExpected_[connectionId] = 0;
    }
}

}
