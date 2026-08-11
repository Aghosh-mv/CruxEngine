#include "FrostEngine/Scene/SceneSerializer.h"

#include <cstring>

namespace Frost {

namespace {

void appendBytes(Vector<u8>& out, const void* data, u32 size) {
    u32 start = (u32)out.size();
    out.resize((usize)start + size);
    std::memcpy(out.data() + start, data, size);
}

} // namespace

u32 SceneSerializer::serializeEntity(const void* entity) {
    if (!entity) return 0;

    const SceneNode* node = static_cast<const SceneNode*>(entity);

    SerializedEntity se;
    se.id = node->userData;
    se.position = node->localPosition;
    se.rotation = node->localRotation.euler();
    se.scale = node->localScale;

    se.componentTypes.pushBack(static_cast<u16>(SerializedComponentType::Transform));
    se.componentData.reserve(3 * sizeof(Vec3));
    appendBytes(se.componentData, &se.position, sizeof(Vec3));
    appendBytes(se.componentData, &se.rotation, sizeof(Vec3));
    appendBytes(se.componentData, &se.scale, sizeof(Vec3));

    serializedEntities_.pushBack(se);
    return se.id;
}

u32 SceneSerializer::serializeScene() {
    buffer_.clear();

    u32 componentCount = 0;
    u32 dataSize = 0;
    for (usize i = 0; i < serializedEntities_.size(); i++) {
        const SerializedEntity& e = serializedEntities_[i];
        componentCount += (u32)e.componentTypes.size();
        dataSize += 46u + 2u * (u32)e.componentTypes.size() + (u32)e.componentData.size();
    }

    SerializationHeader header;
    header.magic = SCENE_MAGIC;
    header.version = fileVersion_;
    header.entityCount = (u32)serializedEntities_.size();
    header.componentCount = componentCount;
    header.dataSize = dataSize;

    appendBytes(buffer_, &header, sizeof(header));

    for (usize i = 0; i < serializedEntities_.size(); i++) {
        const SerializedEntity& e = serializedEntities_[i];
        u16 compCount = (u16)e.componentTypes.size();
        u32 dataSize_i = (u32)e.componentData.size();

        appendBytes(buffer_, &e.id, sizeof(e.id));
        appendBytes(buffer_, &e.position, sizeof(e.position));
        appendBytes(buffer_, &e.rotation, sizeof(e.rotation));
        appendBytes(buffer_, &e.scale, sizeof(e.scale));
        appendBytes(buffer_, &compCount, sizeof(compCount));
        appendBytes(buffer_, &dataSize_i, sizeof(dataSize_i));
        for (usize j = 0; j < e.componentTypes.size(); j++) {
            u16 t = e.componentTypes[j];
            appendBytes(buffer_, &t, sizeof(t));
        }
        if (dataSize_i > 0) appendBytes(buffer_, e.componentData.data(), dataSize_i);
    }

    savedCount_ = header.entityCount;
    lastWriteOk_ = false;
    return (u32)buffer_.size();
}

u32 SceneSerializer::deserializeEntity(const void* buffer, u32 offset) {
    if (!buffer) return 0;

    const u8* p = static_cast<const u8*>(buffer);
    const u32 startOffset = offset;

    if (deserializeSize_ > 0 && (usize)offset + 46u > deserializeSize_) return 0;

    SerializedEntity se;
    std::memcpy(&se.id, p + offset, 4); offset += 4;
    std::memcpy(&se.position, p + offset, 12); offset += 12;
    std::memcpy(&se.rotation, p + offset, 12); offset += 12;
    std::memcpy(&se.scale, p + offset, 12); offset += 12;

    u16 compCount = 0;
    std::memcpy(&compCount, p + offset, 2); offset += 2;

    u32 dataSize = 0;
    std::memcpy(&dataSize, p + offset, 4); offset += 4;

    if (compCount > MAX_COMPONENTS_PER_ENTITY) return 0;
    if (dataSize > MAX_COMPONENT_BYTES) return 0;
    if (deserializeSize_ > 0 &&
        (usize)offset + (usize)compCount * 2u + (usize)dataSize > deserializeSize_) {
        return 0;
    }

    for (u16 i = 0; i < compCount; i++) {
        u16 t = 0;
        std::memcpy(&t, p + offset, 2); offset += 2;
        se.componentTypes.pushBack(t);
    }
    if (dataSize > 0) {
        se.componentData.resize(dataSize);
        std::memcpy(se.componentData.data(), p + offset, dataSize);
        offset += dataSize;
    }

    serializedEntities_.pushBack(se);
    return offset - startOffset;
}

u32 SceneSerializer::deserializeScene(const void* buffer, u32 size) {
    lastReadOk_ = false;
    if (!buffer) return 0;

    deserializeSize_ = size;

    const u8* p = static_cast<const u8*>(buffer);
    if (size < sizeof(SerializationHeader)) {
        FROST_LOG_ERROR("[SceneSerializer] buffer too small for header (%u bytes)", size);
        return 0;
    }

    SerializationHeader header;
    std::memcpy(&header, p, sizeof(header));

    if (header.magic != SCENE_MAGIC) {
        FROST_LOG_ERROR("[SceneSerializer] invalid magic 0x%08X", header.magic);
        return 0;
    }
    if (header.version < 1 || header.version > SCENE_VERSION) {
        FROST_LOG_ERROR("[SceneSerializer] unsupported file version %u (max %u)",
                        header.version, SCENE_VERSION);
        return 0;
    }
    if (header.entityCount > MAX_ENTITIES) {
        FROST_LOG_ERROR("[SceneSerializer] entity count %u exceeds limit %u",
                        header.entityCount, MAX_ENTITIES);
        return 0;
    }
    if ((u64)sizeof(SerializationHeader) + header.dataSize != size) {
        FROST_LOG_ERROR("[SceneSerializer] size mismatch: header+data=%llu, buffer=%u",
                        (unsigned long long)((u64)sizeof(SerializationHeader) + header.dataSize),
                        size);
        return 0;
    }

    serializedEntities_.clear();
    loadedCount_ = 0;

    u32 offset = sizeof(SerializationHeader);
    for (u32 i = 0; i < header.entityCount; i++) {
        u32 consumed = deserializeEntity(p, offset);
        if (consumed == 0 || (u64)offset + consumed > size) {
            FROST_LOG_ERROR("[SceneSerializer] corrupt entity %u at offset %u", i, offset);
            return 0;
        }
        offset += consumed;
    }
    if (offset != size) {
        FROST_LOG_ERROR("[SceneSerializer] trailing bytes after entities (%u of %u)", offset, size);
        return 0;
    }

    loadedCount_ = header.entityCount;
    bytesRead_ = offset;
    lastReadOk_ = true;
    return loadedCount_;
}

bool SceneSerializer::writeToFile(const char* path) {
    lastWriteOk_ = false;
    if (!path) return false;

    if (buffer_.empty()) {
        if (serializedEntities_.empty()) {
            FROST_LOG_ERROR("[SceneSerializer] nothing to write to %s", path);
            return false;
        }
        serializeScene();
    }

    FILE* f = fopen(path, "wb");
    if (!f) {
        FROST_LOG_ERROR("[SceneSerializer] cannot open %s for writing", path);
        return false;
    }

    usize written = fwrite(buffer_.data(), 1, buffer_.size(), f);
    int rc = fclose(f);
    if (written == buffer_.size() && rc == 0) {
        bytesWritten_ = (u32)written;
        lastWriteOk_ = true;
        FROST_LOG_INFO("[SceneSerializer] wrote %u bytes to %s", bytesWritten_, path);
        return true;
    }

    FROST_LOG_ERROR("[SceneSerializer] failed to write %s", path);
    return false;
}

bool SceneSerializer::readFromFile(const char* path) {
    if (!path) return false;

    FILE* f = fopen(path, "rb");
    if (!f) {
        FROST_LOG_ERROR("[SceneSerializer] cannot open %s", path);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long endPos = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (endPos <= 0) {
        fclose(f);
        FROST_LOG_ERROR("[SceneSerializer] empty file %s", path);
        return false;
    }

    Vector<u8> fileData;
    fileData.resize((usize)endPos);
    usize read = fread(fileData.data(), 1, (usize)endPos, f);
    fclose(f);

    if (read != (usize)endPos) {
        FROST_LOG_ERROR("[SceneSerializer] short read on %s", path);
        return false;
    }

    deserializeScene(fileData.data(), (u32)endPos);
    return lastReadOk_;
}

void SceneSerializer::resetStats() {
    savedCount_ = 0;
    loadedCount_ = 0;
    bytesWritten_ = 0;
    bytesRead_ = 0;
    lastWriteOk_ = false;
    lastReadOk_ = false;
}

} // namespace Frost
