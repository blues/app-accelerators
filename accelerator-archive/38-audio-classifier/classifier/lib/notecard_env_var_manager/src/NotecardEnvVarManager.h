#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    NEVM_WAITING = -2,
    NEVM_FAILURE = -1,
    NEVM_SUCCESS = 0
};

#define NEVM_ENV_VAR_ALL ((size_t)-1)

struct NotecardEnvVarManager;
typedef struct NotecardEnvVarManager NotecardEnvVarManager;

typedef void (*envVarCb)(const char *var, const char *val, void *ctx);

NotecardEnvVarManager *NotecardEnvVarManager_alloc(void);
int NotecardEnvVarManager_fetch(NotecardEnvVarManager *man, const char **vars,
                                size_t numVars);
void NotecardEnvVarManager_free(NotecardEnvVarManager *man);
int NotecardEnvVarManager_setEnvVarCb(NotecardEnvVarManager *man,
                                      envVarCb userCb, void *userCtx);

#ifdef __cplusplus
}
#endif
