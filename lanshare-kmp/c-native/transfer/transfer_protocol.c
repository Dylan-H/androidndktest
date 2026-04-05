/*
    LAN-Share Transfer Protocol Encoding/Decoding
    Protocol frame serialization and deserialization
    
    Copyright (C) 2026 LAN-Share Project
*/

#include <stdlib.h>
#include <string.h>
#include "lanshare_protocol.h"

/* Encode frame header */
int lanshare_encode_header(const lanshare_frame_header_t* header,
                            uint8_t* buffer, size_t buffer_size)
{
    if (!header || !buffer) {
        return -1;
    }
    
    size_t required_size = LANSHARE_FRAME_HEADER_SIZE;
    if (buffer_size < required_size) {
        return -1;
    }
    
    size_t offset = 0;
    
    /* Magic */
    memcpy(buffer + offset, &header->magic, sizeof(header->magic));
    offset += sizeof(header->magic);
    
    /* Version */
    buffer[offset++] = header->version;
    
    /* Type */
    buffer[offset++] = header->type;
    
    /* Flags */
    memcpy(buffer + offset, &header->flags, sizeof(header->flags));
    offset += sizeof(header->flags);
    
    /* Data length */
    memcpy(buffer + offset, &header->data_length, sizeof(header->data_length));
    offset += sizeof(header->data_length);
    
    /* Timestamp */
    memcpy(buffer + offset, &header->timestamp, sizeof(header->timestamp));
    offset += sizeof(header->timestamp);
    
    return (int)offset;
}

/* Decode frame header */
int lanshare_decode_header(const uint8_t* buffer, size_t buffer_size,
                            lanshare_frame_header_t* header)
{
    if (!buffer || !header) {
        return -1;
    }
    
    size_t required_size = LANSHARE_FRAME_HEADER_SIZE;
    if (buffer_size < required_size) {
        return -1;
    }
    
    size_t offset = 0;
    
    /* Magic */
    memcpy(&header->magic, buffer + offset, sizeof(header->magic));
    offset += sizeof(header->magic);
    
    /* Version */
    header->version = buffer[offset++];
    
    /* Type */
    header->type = buffer[offset++];
    
    /* Flags */
    memcpy(&header->flags, buffer + offset, sizeof(header->flags));
    offset += sizeof(header->flags);
    
    /* Data length */
    memcpy(&header->data_length, buffer + offset, sizeof(header->data_length));
    offset += sizeof(header->data_length);
    
    /* Timestamp */
    memcpy(&header->timestamp, buffer + offset, sizeof(header->timestamp));
    offset += sizeof(header->timestamp);
    
    /* Validate magic */
    if (header->magic != LANSHARE_MAGIC) {
        return -1;
    }
    
    /* Validate version */
    if (header->version != LANSHARE_PROTOCOL_VERSION) {
        return -1;
    }
    
    return (int)offset;
}

/* Encode header packet (JSON format) */
int lanshare_encode_header_packet(const lanshare_header_t* header,
                                   uint8_t* buffer, size_t buffer_size)
{
    if (!header || !buffer) {
        return -1;
    }
    
    /* For now, use JSON format */
    char json_buffer[512];
    int len = snprintf(json_buffer, sizeof(json_buffer),
        "{\"name\":\"%s\",\"folder\":\"%s\",\"size\":%lld}",
        header->filename, header->folder, header->file_size);
    
    if (len < 0 || len >= (int)sizeof(json_buffer)) {
        return -1;
    }
    
    if ((size_t)len >= buffer_size) {
        return -1;
    }
    
    memcpy(buffer, json_buffer, len);
    return len;
}

/* Decode header packet (JSON format) */
int lanshare_decode_header_packet(const uint8_t* buffer, size_t buffer_size,
                                   lanshare_header_t* header)
{
    if (!buffer || !header) {
        return -1;
    }
    
    /* For now, use simple JSON parsing */
    /* TODO: Implement proper JSON parsing */
    
    /* This is a placeholder - implement proper JSON parsing */
    /* For production, use a lightweight JSON library like json-c or simdjson */
    
    return -1;  /* Not implemented yet */
}

/* Encode data packet (raw data) */
int lanshare_encode_data_packet(const uint8_t* data, size_t data_size,
                                 uint8_t* buffer, size_t buffer_size)
{
    if (!data || !buffer) {
        return -1;
    }
    
    if (data_size > buffer_size) {
        return -1;
    }
    
    memcpy(buffer, data, data_size);
    return (int)data_size;
}

/* Decode data packet (raw data) */
int lanshare_decode_data_packet(const uint8_t* buffer, size_t buffer_size,
                                 uint8_t* data, size_t* data_size)
{
    if (!buffer || !data || !data_size) {
        return -1;
    }
    
    *data_size = buffer_size;
    memcpy(data, buffer, buffer_size);
    return (int)buffer_size;
}

/* Create a complete frame */
int lanshare_create_frame(lanshare_packet_type_t type,
                           const uint8_t* payload, size_t payload_size,
                           uint8_t* buffer, size_t buffer_size)
{
    if (!buffer || !payload) {
        return -1;
    }
    
    /* Check buffer size for header */
    size_t required_size = LANSHARE_FRAME_HEADER_SIZE + payload_size;
    if (buffer_size < required_size) {
        return -1;
    }
    
    /* Create header */
    lanshare_frame_header_t header;
    memset(&header, 0, sizeof(header));
    
    header.magic = LANSHARE_MAGIC;
    header.version = LANSHARE_PROTOCOL_VERSION;
    header.type = (uint8_t)type;
    header.data_length = (uint64_t)payload_size;
    header.timestamp = 0;  /* TODO: Add timestamp */
    
    /* Encode header */
    int header_size = lanshare_encode_header(&header, buffer, buffer_size);
    if (header_size < 0) {
        return -1;
    }
    
    /* Copy payload */
    memcpy(buffer + header_size, payload, payload_size);
    
    return header_size + (int)payload_size;
}

/* Parse frame and extract payload */
int lanshare_parse_frame(const uint8_t* buffer, size_t buffer_size,
                          lanshare_packet_type_t* type,
                          uint8_t* payload, size_t* payload_size)
{
    if (!buffer || !type || !payload || !payload_size) {
        return -1;
    }
    
    /* Decode header */
    lanshare_frame_header_t header;
    int header_size = lanshare_decode_header(buffer, buffer_size, &header);
    if (header_size < 0) {
        return -1;
    }
    
    /* Validate packet size */
    size_t total_size = (size_t)header_size + header.data_length;
    if (total_size > buffer_size) {
        return -1;  /* Incomplete frame */
    }
    
    /* Extract payload */
    *type = (lanshare_packet_type_t)header.type;
    *payload_size = header.data_length;
    
    if (header.data_length > 0) {
        memcpy(payload, buffer + header_size, header.data_length);
    }
    
    return (int)total_size;
}
