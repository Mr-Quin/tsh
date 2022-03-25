#include <string.h>
#include <stdlib.h>
#include "utils.h"

char** splitString(char* str, const char* delim, unsigned int* numTokens) {
    // increment array size by 4 at a time
    size_t size = 4;
    unsigned int i = 0;
    // assume allocation is successful
    char** result = realloc(NULL, sizeof(char*) * size);
    char* tokenPtr = NULL;
    char* token = strtok_r(str, delim, &tokenPtr);

    while (token != NULL) {
        size_t tokenLen = strlen(token);
        // drop trailing newline
        if (token[tokenLen - 1] == '\n') {
            token[tokenLen - 1] = '\0';
        }
        result[i] = strdup(token);

        // reallocate if necessary, save space for null terminator
        i++;
        if (i >= size - 1) {
            size += 4;
            result = realloc(result, sizeof(char*) * size);
        }

        // get next token
        token = strtok_r(NULL, delim, &tokenPtr);
    }

    *numTokens = i;

    // zero unused space
    memset(result + i, 0, sizeof(char*) * (size - i));

    return result;
}

// replace the first occurrence of a substring in a string in place
int replaceString(char** dest, const char* subStr, const char* newSubStr) {
    char* origStrPtr = *dest;
    char* posPtr = strstr(origStrPtr, subStr);
    if (posPtr == NULL) {
        return 0;
    }

    size_t subStrLen = strlen(subStr);
    size_t newSubStrLen = strlen(newSubStr);
    size_t strLen = strlen(origStrPtr);

    // allocate space for new string
    char* newStr = calloc(strLen + newSubStrLen - subStrLen + 1, sizeof(char));
    if (newStr == NULL) {
        return -1;
    }
    char* currPos = newStr;

    // copy characters before the substring
    memcpy(currPos, origStrPtr, posPtr - origStrPtr);
    currPos += posPtr - origStrPtr;

    // copy new substring
    memcpy(currPos, newSubStr, newSubStrLen);
    currPos += newSubStrLen;

    // copy characters after the substring
    memcpy(currPos, posPtr + subStrLen, strLen - (posPtr - origStrPtr) - subStrLen + 1);

    // free original string
    free(origStrPtr);
    *dest = newStr;

    return 1;
}

int randInt(int min, int max) {
    return random() % (max - min + 1) + min;
}
