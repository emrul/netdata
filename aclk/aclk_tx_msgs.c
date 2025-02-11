// SPDX-License-Identifier: GPL-3.0-or-later

#include "aclk_tx_msgs.h"
#include "../daemon/common.h"
#include "aclk_util.h"
#include "aclk_stats.h"

#ifndef __GNUC__
#pragma region aclk_tx_msgs helper functions
#endif

static void aclk_send_message_subtopic(mqtt_wss_client client, json_object *msg, enum aclk_topics subtopic)
{
    uint16_t packet_id;
    const char *str = json_object_to_json_string_ext(msg, JSON_C_TO_STRING_PLAIN);
    const char *topic = aclk_get_topic(subtopic);

    if (unlikely(!topic)) {
        error("Couldn't get topic. Aborting mesage send");
        return;
    }

    mqtt_wss_publish_pid(client, topic, str, strlen(str),  MQTT_WSS_PUB_QOS1, &packet_id);
#ifdef NETDATA_INTERNAL_CHECKS
    aclk_stats_msg_published(packet_id);
#endif
#ifdef ACLK_LOG_CONVERSATION_DIR
#define FN_MAX_LEN 1024
    char filename[FN_MAX_LEN];
    snprintf(filename, FN_MAX_LEN, ACLK_LOG_CONVERSATION_DIR "/%010d-tx.json", ACLK_GET_CONV_LOG_NEXT());
    json_object_to_file_ext(filename, msg, JSON_C_TO_STRING_PRETTY);
#endif
}

static uint16_t aclk_send_message_subtopic_pid(mqtt_wss_client client, json_object *msg, enum aclk_topics subtopic)
{
    uint16_t packet_id;
    const char *str = json_object_to_json_string_ext(msg, JSON_C_TO_STRING_PLAIN);
    const char *topic = aclk_get_topic(subtopic);

    if (unlikely(!topic)) {
        error("Couldn't get topic. Aborting mesage send");
        return 0;
    }

    mqtt_wss_publish_pid(client, topic, str, strlen(str),  MQTT_WSS_PUB_QOS1, &packet_id);
#ifdef NETDATA_INTERNAL_CHECKS
    aclk_stats_msg_published(packet_id);
#endif
#ifdef ACLK_LOG_CONVERSATION_DIR
#define FN_MAX_LEN 1024
    char filename[FN_MAX_LEN];
    snprintf(filename, FN_MAX_LEN, ACLK_LOG_CONVERSATION_DIR "/%010d-tx.json", ACLK_GET_CONV_LOG_NEXT());
    json_object_to_file_ext(filename, msg, JSON_C_TO_STRING_PRETTY);
#endif
    return packet_id;
}

/* UNUSED now but can be used soon MVP1?
static void aclk_send_message_topic(mqtt_wss_client client, json_object *msg, const char *topic)
{
    if (unlikely(!topic || topic[0] != '/')) {
        error ("Full topic required!");
        return;
    }

    const char *str = json_object_to_json_string_ext(msg, JSON_C_TO_STRING_PLAIN);

    mqtt_wss_publish(client, topic, str, strlen(str),  MQTT_WSS_PUB_QOS1);
#ifdef NETDATA_INTERNAL_CHECKS
    aclk_stats_msg_published();
#endif
#ifdef ACLK_LOG_CONVERSATION_DIR
#define FN_MAX_LEN 1024
    char filename[FN_MAX_LEN];
    snprintf(filename, FN_MAX_LEN, ACLK_LOG_CONVERSATION_DIR "/%010d-tx.json", ACLK_GET_CONV_LOG_NEXT());
    json_object_to_file_ext(filename, msg, JSON_C_TO_STRING_PRETTY);
#endif
}
*/

#define TOPIC_MAX_LEN 512
#define V2_BIN_PAYLOAD_SEPARATOR "\x0D\x0A\x0D\x0A"
static void aclk_send_message_with_bin_payload(mqtt_wss_client client, json_object *msg, const char *topic, const void *payload, size_t payload_len)
{
    uint16_t packet_id;
    const char *str;
    char *full_msg;
    int len;

    if (unlikely(!topic || topic[0] != '/')) {
        error ("Full topic required!");
        return;
    }

    str = json_object_to_json_string_ext(msg, JSON_C_TO_STRING_PLAIN);
    len = strlen(str);

    full_msg = mallocz(len + strlen(V2_BIN_PAYLOAD_SEPARATOR) + payload_len);

    memcpy(full_msg, str, len);
    memcpy(&full_msg[len], V2_BIN_PAYLOAD_SEPARATOR, strlen(V2_BIN_PAYLOAD_SEPARATOR));
    len += strlen(V2_BIN_PAYLOAD_SEPARATOR);
    memcpy(&full_msg[len], payload, payload_len);
    len += payload_len;

/* TODO
#ifdef ACLK_LOG_CONVERSATION_DIR
#define FN_MAX_LEN 1024
    char filename[FN_MAX_LEN];
    snprintf(filename, FN_MAX_LEN, ACLK_LOG_CONVERSATION_DIR "/%010d-tx.json", ACLK_GET_CONV_LOG_NEXT());
    json_object_to_file_ext(filename, msg, JSON_C_TO_STRING_PRETTY);
#endif */

    mqtt_wss_publish_pid(client, topic, full_msg, len,  MQTT_WSS_PUB_QOS1, &packet_id);
#ifdef NETDATA_INTERNAL_CHECKS
    aclk_stats_msg_published(packet_id);
#endif
    freez(full_msg);
}

/*
 * Creates universal header common for all ACLK messages. User gets ownership of json object created.
 * Usually this is freed by send function after message has been sent.
 */
static struct json_object *create_hdr(const char *type, const char *msg_id, time_t ts_secs, usec_t ts_us, int version)
{
    uuid_t uuid;
    char uuid_str[36 + 1];
    json_object *tmp;
    json_object *obj = json_object_new_object();

    tmp = json_object_new_string(type);
    json_object_object_add(obj, "type", tmp);

    if (unlikely(!msg_id)) {
        uuid_generate(uuid);
        uuid_unparse(uuid, uuid_str);
        msg_id = uuid_str;
    }

    if (ts_secs == 0) {
        ts_us = now_realtime_usec();
        ts_secs = ts_us / USEC_PER_SEC;
        ts_us = ts_us % USEC_PER_SEC;
    }

    tmp = json_object_new_string(msg_id);
    json_object_object_add(obj, "msg-id", tmp);

    tmp = json_object_new_int64(ts_secs);
    json_object_object_add(obj, "timestamp", tmp);

// TODO handle this somehow on older json-c
//    tmp = json_object_new_uint64(ts_us);
// probably jso->_to_json_strinf -> custom function
//          jso->o.c_uint64 -> map this with pointer to signed int
// commit that implements json_object_new_uint64 is 3c3b592
// between 0.14 and 0.15
    tmp = json_object_new_int64(ts_us);
    json_object_object_add(obj, "timestamp-offset-usec", tmp);

    tmp = json_object_new_int64(aclk_session_sec);
    json_object_object_add(obj, "connect", tmp);

// TODO handle this somehow see above
//    tmp = json_object_new_uint64(0 /* TODO aclk_session_us */);
    tmp = json_object_new_int64(aclk_session_us);
    json_object_object_add(obj, "connect-offset-usec", tmp);

    tmp = json_object_new_int(version);
    json_object_object_add(obj, "version", tmp);

    return obj;
}

static char *create_uuid()
{
    uuid_t uuid;
    char *uuid_str = mallocz(36 + 1);

    uuid_generate(uuid);
    uuid_unparse(uuid, uuid_str);

    return uuid_str;
}

#ifndef __GNUC__
#pragma endregion
#endif

#ifndef __GNUC__
#pragma region aclk_tx_msgs message generators
#endif

/*
 * This will send the /api/v1/info
 */
#define BUFFER_INITIAL_SIZE (1024 * 16)
void aclk_send_info_metadata(mqtt_wss_client client, int metadata_submitted, RRDHOST *host)
{
    BUFFER *local_buffer = buffer_create(BUFFER_INITIAL_SIZE);
    json_object *msg, *payload, *tmp;

    char *msg_id = create_uuid();
    buffer_flush(local_buffer);
    local_buffer->contenttype = CT_APPLICATION_JSON;

    // on_connect messages are sent on a health reload, if the on_connect message is real then we
    // use the session time as the fake timestamp to indicate that it starts the session. If it is
    // a fake on_connect message then use the real timestamp to indicate it is within the existing
    // session.
    if (metadata_submitted)
        msg = create_hdr("update", msg_id, 0, 0, aclk_shared_state.version_neg);
    else
        msg = create_hdr("connect", msg_id, aclk_session_sec, aclk_session_us, aclk_shared_state.version_neg);

    payload = json_object_new_object();
    json_object_object_add(msg, "payload", payload);

    web_client_api_request_v1_info_fill_buffer(host, local_buffer);
    tmp = json_tokener_parse(local_buffer->buffer);
    json_object_object_add(payload, "info", tmp);

    buffer_flush(local_buffer);

    charts2json(host, local_buffer, 1, 0);
    tmp = json_tokener_parse(local_buffer->buffer);
    json_object_object_add(payload, "charts", tmp);

    aclk_send_message_subtopic(client, msg, ACLK_TOPICID_METADATA);

    json_object_put(msg);
    freez(msg_id);
    buffer_free(local_buffer);
}

// TODO should include header instead
void health_active_log_alarms_2json(RRDHOST *host, BUFFER *wb);

void aclk_send_alarm_metadata(mqtt_wss_client client, int metadata_submitted)
{
    BUFFER *local_buffer = buffer_create(BUFFER_INITIAL_SIZE);
    json_object *msg, *payload, *tmp;

    char *msg_id = create_uuid();
    buffer_flush(local_buffer);
    local_buffer->contenttype = CT_APPLICATION_JSON;

    // on_connect messages are sent on a health reload, if the on_connect message is real then we
    // use the session time as the fake timestamp to indicate that it starts the session. If it is
    // a fake on_connect message then use the real timestamp to indicate it is within the existing
    // session.

    if (metadata_submitted)
        msg = create_hdr("connect_alarms", msg_id, 0, 0, aclk_shared_state.version_neg);
    else
        msg = create_hdr("connect_alarms", msg_id, aclk_session_sec, aclk_session_us, aclk_shared_state.version_neg);

    payload = json_object_new_object();
    json_object_object_add(msg, "payload", payload);

    health_alarms2json(localhost, local_buffer, 1);
    tmp = json_tokener_parse(local_buffer->buffer);
    json_object_object_add(payload, "configured-alarms", tmp);

    buffer_flush(local_buffer);

    health_active_log_alarms_2json(localhost, local_buffer);
    tmp = json_tokener_parse(local_buffer->buffer);
    json_object_object_add(payload, "alarms-active", tmp);

    aclk_send_message_subtopic(client, msg, ACLK_TOPICID_ALARMS);

    json_object_put(msg);
    freez(msg_id);
    buffer_free(local_buffer);
}

void aclk_hello_msg(mqtt_wss_client client)
{
    json_object *tmp, *msg;

    char *msg_id = create_uuid();

    ACLK_SHARED_STATE_LOCK;
    aclk_shared_state.version_neg = 0;
    aclk_shared_state.version_neg_wait_till = now_monotonic_usec() + USEC_PER_SEC * VERSION_NEG_TIMEOUT;
    ACLK_SHARED_STATE_UNLOCK;

    //Hello message is versioned separatelly from the rest of the protocol
    msg = create_hdr("hello", msg_id, 0, 0, ACLK_VERSION_NEG_VERSION);

    tmp = json_object_new_int(ACLK_VERSION_MIN);
    json_object_object_add(msg, "min-version", tmp);

    tmp = json_object_new_int(ACLK_VERSION_MAX);
    json_object_object_add(msg, "max-version", tmp);

#ifdef ACLK_NG
    tmp = json_object_new_string("Next Generation");
#else
    tmp = json_object_new_string("Legacy");
#endif
    json_object_object_add(msg, "aclk-implementation", tmp);

    aclk_send_message_subtopic(client, msg, ACLK_TOPICID_METADATA);

    json_object_put(msg);
    freez(msg_id);
}

void aclk_http_msg_v2(mqtt_wss_client client, const char *topic, const char *msg_id, usec_t t_exec, usec_t created, int http_code, const char *payload, size_t payload_len)
{
    json_object *tmp, *msg;

    msg = create_hdr("http", msg_id, 0, 0, 2);

    tmp = json_object_new_int64(t_exec);
    json_object_object_add(msg, "t-exec", tmp);

    tmp = json_object_new_int64(created);
    json_object_object_add(msg, "t-rx", tmp);

    tmp = json_object_new_int(http_code);
    json_object_object_add(msg, "http-code", tmp);

    aclk_send_message_with_bin_payload(client, msg, topic, payload, payload_len);
    json_object_put(msg);
}

void aclk_chart_msg(mqtt_wss_client client, RRDHOST *host, const char *chart)
{
    json_object *msg, *payload;
    BUFFER *tmp_buffer;
    RRDSET *st;
    
    st = rrdset_find(host, chart);
    if (!st)
        st = rrdset_find_byname(host, chart);
    if (!st) {
        info("FAILED to find chart %s", chart);
        return;
    }

    tmp_buffer = buffer_create(BUFFER_INITIAL_SIZE);
    rrdset2json(st, tmp_buffer, NULL, NULL, 1);
    payload = json_tokener_parse(tmp_buffer->buffer);
    if (!payload) {
        error("Failed to parse JSON from rrdset2json");
        buffer_free(tmp_buffer);
        return;
    }

    msg = create_hdr("chart", NULL, 0, 0, aclk_shared_state.version_neg);
    json_object_object_add(msg, "payload", payload);

    aclk_send_message_subtopic(client, msg, ACLK_TOPICID_CHART);

    buffer_free(tmp_buffer);
    json_object_put(msg);
}

void aclk_alarm_state_msg(mqtt_wss_client client, json_object *msg)
{
    // we create header here on purpose (and not send message with it already as `msg` param)
    // one is version_neg is guaranteed to be done here
    // other are timestamps etc. which in ACLK legacy would be wrong (because ACLK legacy
    // send message with timestamps already to Query Queue they would be incorrect at time
    // when query queue would get to send them)
    json_object *obj = create_hdr("status-change", NULL, 0, 0, aclk_shared_state.version_neg);
    json_object_object_add(obj, "payload", msg);

    aclk_send_message_subtopic(client, obj, ACLK_TOPICID_ALARMS);
    json_object_put(obj);
}

/*
 * Will generate disconnect message.
 * @param message if NULL it will generate LWT message (unexpected).
 *        Otherwise string pointed to by this parameter will be used as
 *        reason.
 */
json_object *aclk_generate_disconnect(const char *message)
{
    json_object *tmp, *msg;

    msg = create_hdr("disconnect", NULL, 0, 0, 2);

    tmp = json_object_new_string(message ? message : "unexpected");
    json_object_object_add(msg, "payload", tmp);

    return msg;
}

int aclk_send_app_layer_disconnect(mqtt_wss_client client, const char *message)
{
    int pid;
    json_object *msg = aclk_generate_disconnect(message);
    pid = aclk_send_message_subtopic_pid(client, msg, ACLK_TOPICID_METADATA);
    json_object_put(msg);
    return pid;
}

#ifndef __GNUC__
#pragma endregion
#endif
