#include "util/ChatGPTPlanData.h"
#include <cassert>
#include <cstdio>
#include <string>
void feed(ChatGPTPlanEvents &parser, const std::string &s)
{
    for (unsigned i = 0; i < s.size(); i++)
        assert(parser.write((const unsigned char *)s.data() + i, 1) == 1);
}
int main()
{
    char line[4096] = {}, out[32] = {};
    ChatGPTPlanEvents stream(line, sizeof(line), out, sizeof(out));
    feed(stream, "event: response.output_text.delta\r\ndata: {\"type\":\"response.output_text.delta\",\"delta\":\"Ciao, "
                 "perch\\u00e9?\"}\r\n\r\n");
    assert(!stream.finish());
    feed(stream, "data: {\"type\":\"response.completed\",\"response\":{\"status\":\"completed\"}}\n\n");
    assert(stream.finish() && !strcmp(out, "Ciao, perché?"));
    ChatGPTPlanEvents interrupted(line, sizeof(line), out, sizeof(out));
    feed(interrupted, "data: {\"type\":\"response.output_text.delta\",\"delta\":\"partial\"}\n\n");
    assert(!interrupted.finish());
    feed(interrupted,
         "data: "
         "{\"type\":\"response.failed\",\"response\":{\"error\":{\"code\":\"subscription_sharing_usage_limit_exceeded\"}}}\n\n");
    assert(!interrupted.finish() && interrupted.errorCode == PlanError::LIMIT);
    ChatGPTPlanEvents incomplete(line, sizeof(line), out, sizeof(out));
    feed(incomplete, "data: {\"type\":\"response.incomplete\"}\n\n");
    assert(!incomplete.finish());
    struct Guard {
        char left = 'L';
        char out[4] = {};
        char right = 'R';
    } guard;
    ChatGPTPlanEvents utf8(line, sizeof(line), guard.out, 4);
    feed(
        utf8,
        "data: {\"type\":\"response.output_text.delta\",\"delta\":\"\\u20acXY\"}\n\ndata: {\"type\":\"response.completed\"}\n\n");
    assert(utf8.finish() && !strcmp(guard.out, "€") && guard.left == 'L' && guard.right == 'R');
    char tiny[9] = {};
    ChatGPTPlanEvents overflow(tiny, sizeof(tiny), out, sizeof(out));
    std::string huge(20, 'A');
    assert(overflow.write((const unsigned char *)huge.data(), huge.size()) == 0 && !overflow.valid);
    assert(planHasScope("openid resource.invoke chatgpt.tokens.use.direct", "chatgpt.tokens.use.direct"));
    assert(!planHasScope("chatgpt.tokens.use.direct.extra", "chatgpt.tokens.use.direct"));
    cJSON *dates = cJSON_Parse(
        R"({"a":"2026-11-24T21:00:00Z","b":"2026-11-24T22:00:00+01:00","c":"2026-11-24T16:00:00-05:00","bad":"2026-02-31T21:00:00Z"})");
    assert(planEpoch(dates, "a") == planEpoch(dates, "b") && planEpoch(dates, "a") == planEpoch(dates, "c") &&
           planEpoch(dates, "a") > 1700000000 && planEpoch(dates, "bad") == 0);
    cJSON_Delete(dates);
    const char *host = "urn:uuid:11111111-2222-4333-8444-555555555555";
    cJSON *root = cJSON_Parse(
        R"({"issuer":"https://auth.openai.com","subject":"test-user","client_id":"oaiapp_unit","ext_agent_host_id":"urn:uuid:11111111-2222-4333-8444-555555555555","scope":"chatgpt.tokens.use.direct resource.invoke offline_access","model":"gpt-6.1-sol","access_token":"fake-access","refresh_token":"fake-refresh","id_token":"fake-identity","expires_at":1795550000})");
    assert(planCredentialValid(root, host));
    assert(!planCredentialValid(root, "wrong-host"));
    cJSON_ReplaceItemInObjectCaseSensitive(root, "client_id", cJSON_CreateString("dynamic_agent_client"));
    assert(!planCredentialValid(root, host));
    cJSON_Delete(root);
    assert(!planCredentialValid(nullptr, host));
    puts("PASS: ChatGPT plan credential bounds, host binding, scope boundaries, streamed UTF-8, overflow, completion and "
         "usage-limit errors.");
}
