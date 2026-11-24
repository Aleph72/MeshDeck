#pragma once
#include <stddef.h>

namespace tdeckchatgpt
{
int state();
bool haveConfig();
int configurationResult();
const char *reply();
const char *statusText();
const char *modelName();
const char *accountName();
const char *hostId();
bool ask(const char *);
void clear();
void service();
void disconnect();
bool importBegin();
bool importChunk(const char *);
bool importEnd(const char *);
int importState();
void importCancel();
bool probe();
} // namespace tdeckchatgpt
