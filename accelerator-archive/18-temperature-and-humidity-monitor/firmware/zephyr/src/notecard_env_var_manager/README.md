# Notecard Environment Variable Manager

This is a C library for fetching environment variables from a [Notecard](https://blues.io/products/notecard/).

## Dependencies

This library uses [note-c](https://github.com/blues/note-c) to communicate with the Notecard.

## Usage

The core of the library is the `NotecardEnvVarManager` object. To create a `NotecardEnvVarManager`, call `NotecardEnvVarManager_alloc`:

```c
NotecardEnvVarManager *manager = NotecardEnvVarManager_alloc();
if (manager == NULL) {
    // Handle failed allocation.
}
```

If successful, `NotecardEnvVarManager_alloc` returns a pointer to a `NotecardEnvVarManager`. This is an opaque struct; users are not intended to access the members of the struct directly. To free manager's memory, call `NotecardEnvVarManager_free`:

```c
NotecardEnvVarManager_free(manager);
```

After allocating a manager, fetch environment variables by calling `NotecardEnvVarManager_fetch` with an array of C-strings indicating the variables of interest:

```c
const char *vars[] = {
    "variable_a",
    "variable_b",
    "variable_c"
};
const size_t numVars = sizeof(vars) / sizeof(vars[0]);

int ret = NotecardEnvVarManager_fetch(manager, vars, numVars);
if (ret != NEVM_SUCCESS) {
    // Handle failure.
}
```

This function returns `NEVM_SUCCESS` on success and `NEVM_FAILURE` on failure. It makes an `env.get` request to the Notecard for the specified variables and calls a user-provided callback on each variable:value pair in the response. This callback must have the following signature:

```c
typedef void (*envVarCb)(const char *var, const char *val, void *ctx);
```

To set the callback, call `NotecardEnvVarManager_setEnvVarCb`. This function also allows the user to set a pointer to an arbitrary "user context," which will be passed to the callback by the manager. In the example below, the user context is a pointer to a struct used to cache the values of environment variables.

```c
typedef struct {
    int valueA;
    int valueB;
    int valueC;
} EnvVarCache;

void envVarManagerCb(const char *var, const char *val, void *userCtx)
{
    // Cast the userCtx to the appropriate type.
    EnvVarCache *cache = (EnvVarCache *)userCtx;

    if (strcmp(var, "variable_a") == 0) {
        printf("variable_a has value %s\n", val);
        cache->valueA = atoi(val);
    }
    else if (strcmp(var, "variable_b") == 0) {
        printf("variable_b has value %s\n", val);
        cache->valueB = atoi(val);
    }
    else if (strcmp(var, "variable_c") == 0) {
        printf("variable_c has value %s\n", val);
        cache->valueC = atoi(val);
    }
}

int main(void)
{
    EnvVarCache myCache;
    if (NotecardEnvVarManager_setEnvVarCb(manager, envVarManagerCb, &myCache) != NEVM_SUCCESS)
    {
        // Handle failure.
    }
}
```

`NotecardEnvVarManager_setEnvVarCb` returns `NEVM_SUCCESS` on success and `NEVM_FAILURE` on failure.

Note that if an environment variable is requested by the user that doesn't exist, nothing for that variable will be returned by the Notecard, and the user's callback won't be called for that variable. Thus, the user doesn't need to worry about their callback being called with the `var` or `val` parameters set to NULL.

### `NEVM_ENV_VAR_ALL`

`NotecardEnvVarManager_fetch` supports a special value for the number of variables, `NEVM_ENV_VAR_ALL`. Using this value will cause ALL environment variables to be fetched from the Notecard (i.e. an `env.get` request with no `names` field will be made).

```c
// Fetch all environment variables.
int ret = NotecardEnvVarManager_fetch(manager, NULL, NEVM_ENV_VAR_ALL);
if (ret != NEVM_SUCCESS) {
    // Handle failure.
}
```

## Unit Tests

### Dependencies

- [CMake](https://cmake.org/)
- [Catch2](https://github.com/catchorg/Catch2)
- [lcov](https://github.com/linux-test-project/lcov) (optional; used for coverage reporting)

note-c is downloaded from GitHub as part of the CMake build, so you don't need to download it up front. You can also download and build Catch2 from GitHub instead of installing it up front by adding `-DNEVM_BUILD_CATCH=1` to your `cmake` command.

### Running the Tests

From the root directory, run this script:

```bash
./scripts/run_unit_tests.sh
```

#### Check for Memory Errors

```bash
./scripts/run_unit_tests.sh --mem-check
```

#### Generate Coverage Data

```bash
./scripts/run_unit_tests.sh --coverage
```
