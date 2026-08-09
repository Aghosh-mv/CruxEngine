#pragma once

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/String.h"
#include "Core/Math.h"

namespace Frost {

using NetworkAddress = u64;

struct PacketHeader {
    u16 sequence = 0;
    u16 ack = 0;
    u32 ackBitfield = 0;
    u8 channel = 0;
    u8 flags = 0;
    u16 length = 0;
    static constexpr u8 FLAG_RELIABLE = 0x01;
    static constexpr u8 FLAG_ORDERED = 0x02;
    static constexpr u8 FLAG_ACK = 0x04;
    static constexpr u8 FLAG_DISCONNECT = 0x08;
    static constexpr u8 FLAG_CONNECT = 0x10;
    static constexpr u8 FLAG_FRAGMENT = 0x20;
    static constexpr u8 FLAG_ENCRYPTED = 0x40;
};

struct Packet {
    static constexpr u32 MAX_SIZE = 1200;
    static constexpr u32 HEADER_SIZE = sizeof(PacketHeader);
    PacketHeader header;
    u8 data[MAX_SIZE] = {};
    u32 size = 0;
    NetworkAddress address = 0;
    f32 sendTime = 0.0f;
    bool sent = false;
};

struct ConnectionState {
    enum class State : u8 { Disconnected, Connecting, Connected, Disconnecting };
    State state = State::Disconnected;
    NetworkAddress address = 0;
    u16 localSequence = 0;
    u16 remoteSequence = 0;
    u32 ackBitfield = 0;
    f32 lastSendTime = 0.0f;
    f32 lastRecvTime = 0.0f;
    f32 rtt = 0.0f;
    f32 jitter = 0.0f;
    f32 packetLoss = 0.0f;
    u32 bytesSent = 0;
    u32 bytesReceived = 0;
    u32 packetsSent = 0;
    u32 packetsReceived = 0;
    u32 packetsLost = 0;
    u32 reliableAcked = 0;
    u32 reliablePending = 0;
    u32 unreliableSent = 0;
    f32 connectTime = 0.0f;

    void updateRTT(f32 sample) {
        if (rtt < 0.001f) rtt = sample;
        else rtt = rtt * 0.9f + sample * 0.1f;
        jitter = jitter * 0.9f + std::abs(sample - rtt) * 0.1f;
    }
};

struct FragmentHeader {
    u16 id = 0;
    u8 fragmentIndex = 0;
    u8 fragmentCount = 0;
    u16 fragmentSize = 0;
};

struct ReliableMessage {
    u16 sequence = 0;
    f32 sendTime = 0.0f;
    u8 data[Packet::MAX_SIZE] = {};
    u32 size = 0;
    bool acknowledged = false;
    u32 sendCount = 0;
    static constexpr u32 MAX_RESENDS = 10;
};

struct DeltaState {
    static constexpr u32 MAX_STATE_SIZE = 8192;
    u8 previousState[MAX_STATE_SIZE] = {};
    u8 currentState[MAX_STATE_SIZE] = {};
    u32 previousSize = 0;
    u32 currentSize = 0;
    bool valid = false;
    void update(const u8* state, u32 size) {
        if (size > MAX_STATE_SIZE) size = MAX_STATE_SIZE;
        memcpy(previousState, currentState, currentSize);
        previousSize = currentSize;
        memcpy(currentState, state, size);
        currentSize = size;
        valid = true;
    }
};

struct InterpolationBuffer {
    static constexpr u32 MAX_SAMPLES = 64;
    struct Sample {
        f32 time = 0.0f;
        u8 state[DeltaState::MAX_STATE_SIZE] = {};
        u32 size = 0;
    };
    Sample samples[MAX_SAMPLES] = {};
    u32 sampleCount = 0;
    u32 writeIndex = 0;
    void addSample(f32 time, const u8* state, u32 size) {
        if (size > DeltaState::MAX_STATE_SIZE) size = DeltaState::MAX_STATE_SIZE;
        samples[writeIndex].time = time;
        memcpy(samples[writeIndex].state, state, size);
        samples[writeIndex].size = size;
        writeIndex = (writeIndex + 1) % MAX_SAMPLES;
        if (sampleCount < MAX_SAMPLES) sampleCount++;
    }
    bool interpolate(f32 targetTime, u8* output, u32& outSize) const {
        if (sampleCount < 2) return false;
        i32 idx0 = -1, idx1 = -1;
        for (u32 i = 0; i < sampleCount; i++) {
            u32 checkIdx = (writeIndex - 1 - i + MAX_SAMPLES) % MAX_SAMPLES;
            if (samples[checkIdx].time <= targetTime) {
                idx0 = checkIdx;
                idx1 = (checkIdx + 1) % MAX_SAMPLES;
                break;
            }
        }
        if (idx0 < 0 || idx1 < 0) return false;
        f32 t0 = samples[idx0].time, t1 = samples[idx1].time;
        f32 range = t1 - t0;
        if (range < 0.0001f) { outSize = samples[idx0].size; memcpy(output, samples[idx0].state, outSize); return true; }
        f32 t = (targetTime - t0) / range;
        u32 size0 = samples[idx0].size, size1 = samples[idx1].size;
        outSize = (size0 < size1) ? size0 : size1;
        for (u32 i = 0; i < outSize; i++) {
            f32 v0 = (f32)samples[idx0].state[i];
            f32 v1 = (f32)samples[idx1].state[i];
            output[i] = (u8)(v0 + (v1 - v0) * t);
        }
        return true;
    }
};

struct LagCompensationBuffer {
    static constexpr u32 MAX_SNAPSHOTS = 128;
    struct Snapshot { f32 time = 0.0f; u8 state[DeltaState::MAX_STATE_SIZE] = {}; u32 size = 0; };
    Snapshot snapshots[MAX_SNAPSHOTS] = {};
    u32 snapshotCount = 0;
    u32 writeIndex = 0;
    void addSnapshot(f32 time, const u8* state, u32 size) {
        if (size > DeltaState::MAX_STATE_SIZE) size = DeltaState::MAX_STATE_SIZE;
        snapshots[writeIndex].time = time;
        memcpy(snapshots[writeIndex].state, state, size);
        snapshots[writeIndex].size = size;
        writeIndex = (writeIndex + 1) % MAX_SNAPSHOTS;
        if (snapshotCount < MAX_SNAPSHOTS) snapshotCount++;
    }
    bool rewindTo(f32 time, u8* output, u32& outSize) const {
        for (u32 i = 0; i < snapshotCount; i++) {
            u32 idx = (writeIndex - 1 - i + MAX_SNAPSHOTS) % MAX_SNAPSHOTS;
            if (snapshots[idx].time <= time) { outSize = snapshots[idx].size; memcpy(output, snapshots[idx].state, outSize); return true; }
        }
        return false;
    }
};

struct NetworkConfig {
    u16 port = 27015;
    u32 maxConnections = 32;
    u32 sendBufferSize = 256;
    u32 recvBufferSize = 256;
    f32 tickRate = 64.0f;
    f32 connectionTimeout = 10.0f;
    f32 keepAliveInterval = 1.0f;
    bool isServer = false;
    u32 maxPacketSize = Packet::MAX_SIZE;
    bool encryptionEnabled = false;
    u8 encryptionKey = 0x5A;
    u32 maxFragments = 8;
};

class NetworkSystem {
public:
    static constexpr u32 MAX_CONNECTIONS = 32;
    static constexpr u32 MAX_RELIABLE_MESSAGES = 256;

    NetworkSystem();
    ~NetworkSystem();

    bool init(const NetworkConfig& config);
    void shutdown();
    void update(f32 dt);

    bool connect(const char* host, u16 port);
    void disconnect();
    bool isConnected() const;
    bool startServer(u16 port);
    void stopServer();
    u32 clientCount() const;

    void sendReliable(u32 connectionId, const u8* data, u32 size);
    void sendUnreliable(u32 connectionId, const u8* data, u32 size);
    void broadcastReliable(const u8* data, u32 size);
    void broadcastUnreliable(const u8* data, u32 size);

    void setState(u32 connectionId, const u8* state, u32 size);
    bool getInterpolatedState(u32 connectionId, f32 time, u8* output, u32& outSize);
    void addSnapshot(f32 time, const u8* state, u32 size);
    bool rewindState(f32 time, u8* output, u32& outSize);

    u32 computeDelta(const u8* from, u32 fromSize, const u8* to, u32 toSize, u8* delta, u32 deltaMax);
    u32 applyDelta(const u8* base, u32 baseSize, const u8* delta, u32 deltaSize, u8* output, u32 outputMax);

    ConnectionState* getConnection(u32 connectionId);
    const ConnectionState* getConnection(u32 connectionId) const;
    u32 getMyConnectionId() const { return myConnectionId_; }

    using PacketCallback = void(*)(u32 connectionId, const u8* data, u32 size, void* userData);
    using ConnectionCallback = void(*)(u32 connectionId, bool connected, void* userData);
    void setPacketCallback(PacketCallback cb, void* userData) { packetCallback_ = cb; packetUserData_ = userData; }
    void setConnectionCallback(ConnectionCallback cb, void* userData) { connectionCallback_ = cb; connectionUserData_ = userData; }

    const NetworkConfig& config() const { return config_; }
    void setSendRate(f32 hz) { config_.tickRate = hz; }

    struct Stats {
        u32 bytesPerSecond = 0;
        u32 packetsPerSecond = 0;
        f32 averageRTT = 0.0f;
        f32 averageJitter = 0.0f;
        u32 droppedPackets = 0;
        u32 totalBytesSent = 0;
        u32 totalBytesReceived = 0;
    };
    const Stats& stats() const { return stats_; }

private:
    void processReceivedPackets();
    void processPacket(const u8* data, u32 size, NetworkAddress from);
    void sendPacket(u32 connectionId, Packet& packet);
    void sendAck(u32 connectionId, u16 sequence);
    void handleConnectionRequest(NetworkAddress from, const u8* data, u32 size);
    void handleConnectionAccept(NetworkAddress from, const u8* data, u32 size);
    void handleDisconnect(NetworkAddress from);
    void processReliability(u32 connectionId, const PacketHeader& header);
    void addToSendQueue(u32 connectionId, const u8* data, u32 size, bool reliable);
    void processReliableRetransmission(f32 dt);
    void scramblePacket(u8* data, u32 size);
    void processFragmentedPacket(u32 connectionId, const u8* data, u32 size, const PacketHeader& header);

    int socket_ = -1;
    NetworkConfig config_;
    ConnectionState connections_[MAX_CONNECTIONS];
    u32 connectionCount_ = 0;
    u32 myConnectionId_ = 0;

    Packet sendBuffer_[256];
    u32 sendBufferCount_ = 0;
    u8 recvBuffer_[65536] = {};
    u32 recvSize_ = 0;

    ReliableMessage reliableMessages_[MAX_CONNECTIONS][MAX_RELIABLE_MESSAGES];
    u32 reliableCount_[MAX_CONNECTIONS] = {};

    DeltaState deltaStates_[MAX_CONNECTIONS];
    InterpolationBuffer interpBuffers_[MAX_CONNECTIONS];
    LagCompensationBuffer lagBuffer_;

    u8 fragmentBuffer_[MAX_CONNECTIONS][Packet::MAX_SIZE * 4] = {};
    u32 fragmentReceived_[MAX_CONNECTIONS] = {};
    u32 fragmentExpected_[MAX_CONNECTIONS] = {};

    PacketCallback packetCallback_ = nullptr;
    ConnectionCallback connectionCallback_ = nullptr;
    void* packetUserData_ = nullptr;
    void* connectionUserData_ = nullptr;

    Stats stats_;
    f32 time_ = 0.0f;
    f32 lastKeepAlive_ = 0.0f;
    f32 statsAccum_ = 0.0f;
    u32 statsBytesSent_ = 0;
    u32 statsPacketsSent_ = 0;
};

}
